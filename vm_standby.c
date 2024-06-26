#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "list.h"
#include "pagetable.h"
#pragma comment(lib, "advapi32.lib")

#define PAGE_SIZE                   4096

#define MB(x)                       ((x) * 1024 * 1024)

//
// This is intentionally a power of two so we can use masking to stay
// within bounds.
//

#define VIRTUAL_ADDRESS_SIZE        MB(16)

#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS        (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))

//
// Deliberately use a physical page pool that is approximately 1% of the
// virtual address space !
//

#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)

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


page_t freelist;
page_t standby_list;
page_t modified_list;
PVOID p;
page_t* base_pfn;

VOID
full_virtual_memory_test (
    VOID
    )
{
    unsigned i;
    // PULONG_PTR p;
    PULONG_PTR arbitrary_va;
    unsigned random_number;
    BOOL allocated;
    BOOL page_faulted;
    BOOL privilege;
    BOOL obtained_pages;
    ULONG_PTR physical_page_count;
    PULONG_PTR physical_page_numbers;
    HANDLE physical_page_handle;
    ULONG_PTR virtual_address_size;
    ULONG_PTR virtual_address_size_in_unsigned_chunks;

    //
    // Allocate the physical pages that we will be managing.
    //
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    //

    privilege = GetPrivilege ();

    if (privilege == FALSE) {
        printf ("full_virtual_memory_test : could not get privilege\n");
        return;
    }    

    physical_page_handle = GetCurrentProcess ();

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

    virtual_address_size = 64 * physical_page_count * PAGE_SIZE;

    //
    // Round down to a PAGE_SIZE boundary.
    //

    virtual_address_size &= ~PAGE_SIZE;

    virtual_address_size_in_unsigned_chunks =
                        virtual_address_size / sizeof (ULONG_PTR);

    p = VirtualAlloc (NULL,
                      virtual_address_size,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

    if (p == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory\n");
        return;
    }

    //
    // Now perform random accesses.
    //

    ULONG64 highest = 0x0;
    for (unsigned i = 0; i < physical_page_count; i++) {
        if (physical_page_numbers[i] > highest) {
            highest = physical_page_numbers[i];
        }
    }

    base_pfn = VirtualAlloc(NULL, (highest + 1) * sizeof(page_t), MEM_RESERVE, PAGE_READWRITE);
    if (base_pfn == NULL) {
        printf("Couldn't alloc base\n");
        return;
    }

    // DM: Switch standby, physical_page_numbers and pgtb to global
    ULONG64 num_VAs = virtual_address_size / PAGE_SIZE;
    PAGE_TABLE* pgtb = instantiatePagetable(num_VAs, base_pfn);
    if (pgtb == NULL) {
        printf("Couldn't instantiate Pagetable\n");
        return;
    }

    instantiateFreeList(physical_page_numbers, physical_page_count, base_pfn);
    instantiateStandyList();
    instantiateModifiedList();

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

        random_number = rand () * rand () * rand ();

        random_number %= virtual_address_size_in_unsigned_chunks;

        //
        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.
        //

        page_faulted = FALSE;

        random_number = random_number &~ 7;
        arbitrary_va = (PULONG_PTR)((ULONG_PTR)p + random_number);

        __try {

            *arbitrary_va = (ULONG_PTR) arbitrary_va;

        } __except (EXCEPTION_EXECUTE_HANDLER) {

            page_faulted = TRUE;
        }



        if (page_faulted) {


            PTE* curr_pte = va_to_pte(arbitrary_va, pgtb);
            if (curr_pte == NULL) {
                printf("Could not get a valid PTE given VA\n");
                return;
            }

            page_t* curr_page;
            BOOL get_from_modified = FALSE;

            EnterCriticalSection(&pgtb->pte_lock);

            if (curr_pte->memory_format.frame_number != 0) {
                if (curr_pte->memory_format.valid == 1) {
                    DebugBreak();
                    printf("Already active by another thread, continue on\n");
                    LeaveCriticalSection(&pgtb->pte_lock);
                    continue;
                }

                else if (curr_pte->disc_format.valid == 0 && curr_pte->disc_format.in_memory == 0) {
                    printf("Given page is on disc, go get it\n");
                    // Get the frame from disk,
                    // write to new frame and give back

                    // Get pfn stored in this PTE
                    ULONG64 pte_pfn = curr_pte->disc_format.disc_number;

                    //EnterCriticalSection(&standby_list->list_lock);

                    curr_page = pfn_to_page(pte_pfn, pgtb);


                    // DM: Remove from standby list
                    //EnterCriticalSection(&standby_list);
                    //popFromAnywhere(standby_list, curr_page);
                    //LeaveCriticalSection(&standby_list);


                    // Got PFN from standby, return to active 
                    curr_pte->memory_format.age = 0;
                    curr_pte->memory_format.valid = 1;

                    curr_pte->memory_format.frame_number = pte_pfn;

                    //LeaveCriticalSection(&standby_list->list_lock);

                    // DM: Move page to freelist again
                    //addToHead(freelist, curr_page);

                }

                else if (curr_pte->transition_format.valid == 0 && curr_pte->transition_format.in_memory == 1) {
                    // Given frame on modified list or standby
                    if (curr_pte->transition_format.is_modified == 1) {
                        get_from_modified = TRUE;
                    }
                }


                if (get_from_modified) {
                    ULONG64 pte_pfn = curr_pte->transition_format.frame_number;
                    // EnterCriticalSection(&modified_list->list_lock);
                    curr_page = popFromAnywhere(&modified_list, pfn_to_page(pte_pfn, pgtb));
                    // LeaveCriticalSection(&modified_list->list_lock);
                }
                else {
                    ULONG64 pte_pfn = curr_pte->transition_format.frame_number;
                    // EnterCriticalSection(&standby_list->list_lock);
                    curr_page = popFromAnywhere(&standby_list, pfn_to_page(pte_pfn, pgtb));
                    // LeaveCriticalSection(&standby_list->list_lock);
                }
            }


            
            // Seems like a strange way to do this
            page_t* free = &freelist;
            EnterCriticalSection(&free->list_lock);

            curr_page = popHeadPage(&freelist);


            if (curr_page == NULL) {
                LeaveCriticalSection(&free->list_lock);

                // DM: trim 1 page, if available
                // Right now, just finds first ones that are active

                unsigned i = 0;
                BOOL got = FALSE;
                while (i < pgtb->num_ptes) {
                    if (got == TRUE) {
                        break;
                    }

                    PTE valid_pte_contents;
                    PTE transition_pte_contents;

                    PTE* new_pte = &pgtb->pte_array[i];
                    valid_pte_contents = *new_pte;
                    if (valid_pte_contents.memory_format.valid == 1) {
                        // TRIM THIS ONE
                        transition_pte_contents.entire_field = 0;

                        // Do i do this? If moving to standby, even though
                        // it should be on disk, still setting in memory to 1?
                        transition_pte_contents.transition_format.in_memory = 1;

                        transition_pte_contents.transition_format.frame_number =
                            valid_pte_contents.memory_format.frame_number;

                        *new_pte = transition_pte_contents;

                        addToHead(&standby_list, pfn_to_page(transition_pte_contents.transition_format.frame_number, pgtb));

                        PULONG_PTR conv_va = pte_to_va(new_pte, pgtb);

                        if (MapUserPhysicalPages(conv_va, 1, NULL) == FALSE) {
                            DebugBreak();
                            printf("Could not unmap VA %p\n", conv_va);
                            return;
                        }

                        got = TRUE;
                    }
                    i++;
                }

                page_t* standby = &standby_list;
                EnterCriticalSection(&standby->list_lock);

                curr_page = popHeadPage(&standby_list);
                if (curr_page == NULL) {
                    printf("Also could not get from standby\n");

                    LeaveCriticalSection(&standby->list_lock);
                    return;
                }
                else {
                    LeaveCriticalSection(&standby->list_lock);
                }
            }
            else {
                LeaveCriticalSection(&free->list_lock);
            }
            

            ULONG64 popped_pfn = page_to_pfn(curr_page);
            curr_page->pte = curr_pte;

            curr_pte->memory_format.age = 0;
            curr_pte->memory_format.valid = 1;
            curr_pte->memory_format.frame_number = popped_pfn;

            if (MapUserPhysicalPages (arbitrary_va, 1, &popped_pfn) == FALSE) {

                DebugBreak();
                printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, popped_pfn);

                return;
            }

            *arbitrary_va = (ULONG_PTR) arbitrary_va;

            LeaveCriticalSection(&pgtb->pte_lock);

        }
    }

    printf ("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);

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
