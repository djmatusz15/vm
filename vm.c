#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "list.h"
#include "pagetable.h"
#include "pagefault.h"
#include "threads.h"
#include "globals.h"
#include <time.h>
#pragma comment(lib, "advapi32.lib")

#define PAGE_SIZE                   4096

// #define MB(x)                       ((x) * 1024 * 1024)
// #define GB(x)                       ((x) * 1024 * 1024 * 1024)

//
// This is intentionally a power of two so we can use masking to stay
// within bounds.
//

// #define VIRTUAL_ADDRESS_SIZE        MB(16)

#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS        (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))

//
// Deliberately use a physical page pool that is approximately 1% of the
// virtual address space !
//

// #define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)

BOOL
GetPrivilege  (
    VOID
    )
{
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privilege [1];
    } Info;

    //
    // This is Windows-specific code to acquire a privilege.
    // Understanding each line of it is not so important for
    // our efforts.
    //

    HANDLE hProcess;
    HANDLE Token;
    BOOL Result;

    //
    // Open the token.
    //

    hProcess = GetCurrentProcess ();

    Result = OpenProcessToken (hProcess,
                               TOKEN_ADJUST_PRIVILEGES,
                               &Token);

    if (Result == FALSE) {
        printf ("Cannot open process token.\n");
        return FALSE;
    }

    //
    // Enable the privilege. 
    //

    Info.Count = 1;
    Info.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

    //
    // Get the LUID.
    //

    Result = LookupPrivilegeValue (NULL,
                                   SE_LOCK_MEMORY_NAME,
                                   &(Info.Privilege[0].Luid));

    if (Result == FALSE) {
        printf ("Cannot get privilege\n");
        return FALSE;
    }

    //
    // Adjust the privilege.
    //

    Result = AdjustTokenPrivileges (Token,
                                    FALSE,
                                    (PTOKEN_PRIVILEGES) &Info,
                                    0,
                                    NULL,
                                    NULL);

    //
    // Check the result.
    //

    if (Result == FALSE) {
        printf ("Cannot adjust token privileges %u\n", GetLastError ());
        return FALSE;
    } 

    if (GetLastError () != ERROR_SUCCESS) {
        printf ("Cannot enable the SE_LOCK_MEMORY_NAME privilege - check local policy\n");
        return FALSE;
    }

    CloseHandle (Token);

    return TRUE;
}


#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

HANDLE
CreateSharedMemorySection (
    VOID
    )
{
    HANDLE section;
    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Create an AWE section.  Later we deposit pages into it and/or
    // return them.
    //

    parameter.Type = MemSectionExtendedParameterUserPhysicalFlags;
    parameter.ULong = 0;

    section = CreateFileMapping2 (INVALID_HANDLE_VALUE,
                                  NULL,
                                  SECTION_MAP_READ | SECTION_MAP_WRITE,
                                  PAGE_READWRITE,
                                  SEC_RESERVE,
                                  0,
                                  NULL,
                                  &parameter,
                                  1);

    return section;
}

#endif


VOID
malloc_test (
    VOID
    )
{
    unsigned i;
    PULONG_PTR p;
    unsigned random_number;

    p = malloc (VIRTUAL_ADDRESS_SIZE);

    if (p == NULL) {
        printf ("malloc_test : could not malloc memory\n");
        return;
    }

    srand (time (NULL));

    for (i = 0; i < MB (1); i += 1) {

        //
        // Randomly access different portions of the virtual address
        // space we obtained above.
        //
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation.  Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).
        //

        random_number = rand ();

        random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;

        //
        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.
        //

        *(p + random_number) = (ULONG_PTR) (p + random_number);
    }

    printf ("malloc_test : finished accessing %u random virtual addresses\n", i);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    free (p);

    return;
}

VOID
commit_at_fault_time_test (
    VOID
    )
{
    unsigned i;
    PULONG_PTR p;
    PULONG_PTR committed_va;
    unsigned random_number;
    BOOL page_faulted;

    p = VirtualAlloc (NULL,
                      VIRTUAL_ADDRESS_SIZE,
                      MEM_RESERVE,
                      PAGE_NOACCESS);

    if (p == NULL) {
        printf ("commit_at_fault_time_test : could not reserve memory\n");
        return;
    }

    srand (time (NULL));

    for (i = 0; i < MB (1); i += 1) {

        //
        // Randomly access different portions of the virtual address
        // space we obtained above.
        //
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation.  Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).
        //

        random_number = rand ();

        random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;

        //
        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.
        //

        page_faulted = FALSE;

        __try {

            *(p + random_number) = (ULONG_PTR) (p + random_number);

        } __except (EXCEPTION_EXECUTE_HANDLER) {

            page_faulted = TRUE;
        }

        if (page_faulted) {

            //
            // Commit the virtual address now - if that succeeds then
            // we'll be able to access it from now on.
            //

            committed_va = p + random_number;

            committed_va = VirtualAlloc (committed_va,
                                         sizeof (ULONG_PTR),
                                         MEM_COMMIT,
                                         PAGE_READWRITE);

            if (committed_va == NULL) {
                printf ("commit_at_fault_time_test : could not commit memory\n");
                return;
            }

            //
            // No exception handler needed now since we are guaranteed
            // by virtue of our commit that the operating system will
            // honor our access.
            //

            *committed_va = (ULONG_PTR) committed_va;
        }
    }

    printf ("commit_at_fault_time_test : finished accessing %u random virtual addresses\n", i);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    VirtualFree (p, 0, MEM_RELEASE);

    return;
}


HANDLE physical_page_handle;
ULONG_PTR virtual_address_size;
ULONG_PTR virtual_address_size_in_unsigned_chunks;

page_t freelist;
page_t standby_list;
page_t modified_list;
page_t zero_list;
PVOID p;
page_t* base_pfn;
HANDLE trim_now;
HANDLE aging_event;
PAGE_TABLE* pgtb;
ULONG64 num_pagefile_blocks;

// Global counts for faults
int rescues;
int read_from_disk;
int new_ptes;
int ran_into_active_ptes;

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

#pragma comment(lib, "onecore.lib")

#endif

VOID
full_virtual_memory_test (
    VOID
    )
{
    unsigned i;
    BOOL allocated;
    BOOL page_faulted;
    BOOL privilege;
    BOOL obtained_pages;
    ULONG_PTR physical_page_count;
    PULONG_PTR physical_page_numbers;
    clock_t start_t, end_t;


    rescues = 0;
    read_from_disk = 0;
    new_ptes = 0;
    ran_into_active_ptes = 0;
    

    //
    // Allocate the physical pages that we will be managing.
    //
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    //

    start_t = clock();

    privilege = GetPrivilege ();

    if (privilege == FALSE) {
        printf ("full_virtual_memory_test : could not get privilege\n");
        return;
    }


    // Set this to support multiple VAs being set to the 
    // same physical page
    #if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    physical_page_handle = CreateSharedMemorySection ();

    if (physical_page_handle == NULL) {
        printf ("CreateFileMapping2 failed, error %#x\n", GetLastError ());
        return;
    }

    #else    

    physical_page_handle = GetCurrentProcess ();

    #endif

    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

    physical_page_numbers = malloc (physical_page_count * sizeof (ULONG_PTR));

    if (physical_page_numbers == NULL) {
        printf ("full_virtual_memory_test : could not allocate array to hold physical page numbers\n");
        return;
    }

    allocated = AllocateUserPhysicalPages (physical_page_handle,
                                           &physical_page_count,
                                           physical_page_numbers);

    if (allocated == FALSE) {
        printf ("full_virtual_memory_test : could not allocate physical pages\n");
        return;
    }

    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {

        printf ("full_virtual_memory_test : allocated only %llu pages out of %u pages requested\n",
                physical_page_count,
                NUMBER_OF_PHYSICAL_PAGES);
    }

    //
    // Reserve a user address space region using the Windows kernel
    // AWE (address windowing extensions) APIs.
    //
    // This will let us connect physical pages of our choosing to
    // any given virtual address within our allocated region.
    //
    // We deliberately make this much larger than physical memory
    // to illustrate how we can manage the illusion.
    //



    // #if 0

    virtual_address_size = FRACTION_OF_VA_SPACE_ALLOCATED_TO_PHYSICAL * NUMBER_OF_PHYSICAL_PAGES * PAGE_SIZE;       // * 16

    //
    // Round down to a PAGE_SIZE boundary.
    //

    virtual_address_size &= ~PAGE_SIZE;


    // Calculates the number of pagefile blocks dynamically.
    // +2 is because we save PTE bits;
    // don't use the first pagefile block of i = 0
    // so that we can lose a pagefile block instead of 
    // using a precious PTE bit. We have to make it +2 
    // so that we have an extra pagefile slot for the trade;
    // we can still put the contents in, even though all the 
    // slots are filled, to take one out

    num_pagefile_blocks = (virtual_address_size / PAGE_SIZE) - physical_page_count + 2;


    virtual_address_size_in_unsigned_chunks =
                        virtual_address_size / sizeof (ULONG_PTR);
    
    // #endif

    #if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section
    // created above.
    //

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;

    p = VirtualAlloc2 (NULL,
                       NULL,
                       virtual_address_size,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &parameter,
                       1);

    #else

    p = VirtualAlloc (NULL,
                      virtual_address_size,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

    
    #endif


    if (p == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory\n");
        return;
    }

    //
    // Now perform random accesses.
    //

    ULONG64 lowest = 0x7fffffff;
    ULONG64 highest = 0x0;
    for (unsigned i = 0; i < physical_page_count; i++) {
        if (physical_page_numbers[i] > highest) {
            highest = physical_page_numbers[i];
        }
        if (physical_page_numbers[i] < lowest) {
            lowest = physical_page_numbers[i];
        }
    }
    printf("Lowest VA referenced: %lld\n", lowest);
    printf("Highest VA referenced: %lld\n", highest);

    base_pfn = VirtualAlloc(NULL, ((highest + 1)) * sizeof(page_t), MEM_RESERVE, PAGE_READWRITE);
    if (base_pfn == NULL) {
        printf("Couldn't alloc base\n");
        return;
    }

    ULONG64 num_VAs = virtual_address_size / PAGE_SIZE;
    pgtb = instantiatePagetable(num_VAs, base_pfn);
    if (pgtb == NULL) {
        printf("Couldn't instantiate Pagetable\n");
        return;
    }


    // DM: have to change up the random seed for each thread

    // srand (time (NULL));

    instantiateFreeList(physical_page_numbers, physical_page_count, base_pfn);
    instantiateStandyList();
    instantiateModifiedList();
    instantiateZeroList();

    HANDLE* thread_handles = initialize_threads();


    WaitForMultipleObjects(NUM_OF_FAULTING_THREADS, &thread_handles[3], TRUE, INFINITE);


    // SetEvent(global_exit_event);
    

    printf ("full_virtual_memory_test : finished accessing random virtual addresses\n");

    end_t = clock();
    double total_time =  ((double) (end_t - start_t)) / CLOCKS_PER_SEC;

    printf("Total CPU time used: %.5f\n", total_time);
    printf("Total Rescues: %d\n", rescues);
    printf("Total disk reads: %d\n", read_from_disk);
    printf("Total new PTEs handled: %d\n", new_ptes);
    printf("Total active PTEs ran into: %d\n", ran_into_active_ptes);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    VirtualFree (p, 0, MEM_RELEASE);

    return;
}

VOID 
main (
    int argc,
    char** argv
    )
{
    //
    // Test our very complicated usermode virtual implementation.
    // 
    // We will control the virtual and physical address space management
    // ourselves with the only two exceptions being that we will :
    //
    // 1. Ask the operating system for the physical pages we'll use to
    //    form our pool.
    //
    // 2. Ask the operating system to connect one of our virtual addresses
    //    to one of our physical pages (from our pool).
    //
    // We would do both of those operations ourselves but the operating
    // system (for security reasons) does not allow us to.
    //
    // But we will do all the heavy lifting of maintaining translation
    // tables, PFN data structures, management of physical pages,
    // virtual memory operations like handling page faults, materializing
    // mappings, freeing them, trimming them, writing them out to backing
    // store, bringing them back from backing store, protecting them, etc.
    //
    // This is where we can be as creative as we like, the sky's the limit !
    //

    full_virtual_memory_test ();

    return;
}
