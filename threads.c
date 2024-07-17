#include "threads.h"
#include "globals.h"


// Handles trimming PTEs that are active, and moving them
// to modified list.

// DM:
// Must add to head and pop from tail!!
// Removing and adding to head! Not efficient
// and not going to work in some places!

LPTHREAD_START_ROUTINE handle_trimming() {

    ULONG64 ptes_per_region = pgtb->num_ptes / NUM_PTE_REGIONS;
    unsigned trimmed_pages_count = 0;

    while (1) {
        WaitForSingleObject(trim_now, INFINITE);
        ULONG64 count;
        ULONG64 region;
        PTE* curr_pte;
        BOOL trimmed_enough = FALSE;
        unsigned i;

        for (i = 0; i < NUM_PTE_REGIONS; i++) {
            
            if (trimmed_enough == TRUE) {
                break;
            }

            LockPagetable(i);
            //EnterCriticalSection(&pgtb->lock);

            for (unsigned j = i * ptes_per_region; j < ((i+1) * (ptes_per_region)); j++) {

                if (pgtb->pte_array[j].memory_format.valid == 1) {
                    count = j;
                    region = i;

                    curr_pte = &pgtb->pte_array[count];

                    PTE local_contents;
                    local_contents.entire_format = 0;

                    // Set the PTE so the its modified
                    local_contents.transition_format.valid = 0;
                    local_contents.transition_format.in_memory = 1;
                    local_contents.transition_format.frame_number = curr_pte->memory_format.frame_number;


                    page_t* curr_page = pfn_to_page(curr_pte->memory_format.frame_number, pgtb);
                    if (curr_page->pagefile_num != 0) {
                        DebugBreak();
                    }

                    WriteToPTE(curr_pte, local_contents);

                    PULONG_PTR conv_va = pte_to_va(curr_pte, pgtb);


                    if (MapUserPhysicalPages(conv_va, 1, NULL) == FALSE) {
                        DebugBreak();
                        printf("Couldn't unmap VA %p (handle_trimming)\n", conv_va);
                        return NULL;
                    }


                    // Was originally above the Mapping. If this was the
                    // case, we would have left the standby lock, and someone
                    // else could have swept in and took it before we unmapped it
                    EnterCriticalSection(&modified_list.list_lock);

                    addToHead(&modified_list, pfn_to_page(curr_pte->transition_format.frame_number, pgtb));

                    LeaveCriticalSection(&modified_list.list_lock);


                    SetEvent(modified_list_notempty);

                    
                    if ((standby_list.num_of_pages + modified_list.num_of_pages) > (2 * (pgtb->num_ptes)) / 7) {
                        trimmed_enough = TRUE;
                        break;
                    }

                    trimmed_pages_count++;
                }
            }
            
            UnlockPagetable(i);
            //LeaveCriticalSection(&pgtb->lock);
        }

        if (i == NUM_PTE_REGIONS) {
            //printf("Didn't fill enough: %d\n", trimmed_pages_count);
            continue;
        }
    }
}

#define FREE 0
#define IN_USE 1

LPTHREAD_START_ROUTINE handle_modifying() {


     while (1) {

        EnterCriticalSection(&modified_list.list_lock);

        page_t* curr_page = popHeadPage(&modified_list);
        if (curr_page == NULL) {
            //printf("Could not pop head from modified list\n");

            LeaveCriticalSection(&modified_list.list_lock);
            WaitForSingleObject(modified_list_notempty, INFINITE);
            continue;
        }

        // Leave out first disk space so that 
        // there are no collisions with frame numbers
        // in valid or transition PTEs; no ambiguity
        unsigned i;
        for (i = 1 ; i < num_pagefile_blocks; i++) {
            if (pagefile_state[i] == FREE) {
                pagefile_state[i] = IN_USE;
                break;
            }
        }

        if (i == num_pagefile_blocks) {
            addToHead(&modified_list, curr_page);

            LeaveCriticalSection(&modified_list.list_lock);

            // DM: When it is INFINITE (waits for signal), 
            // I get to a point where it repeats the same VA
            // over and over. I believe (not certain) that the
            // read from disk keeps failing for a given
            // page since we say its on the pagefile but it
            // can't find it

            WaitForSingleObject(pagefile_blocks_available, INFINITE);
            continue;
        }

        ULONG64 conv_pfn = curr_page->pte->transition_format.frame_number;
        if (MapUserPhysicalPages (modified_page_va, 1, &conv_pfn) == FALSE) {

            LeaveCriticalSection(&modified_list.list_lock);
            printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_page_va, conv_pfn);
            DebugBreak();

        }

        memcpy(&pagefile_contents[i * PAGE_SIZE], modified_page_va, PAGE_SIZE);


        if (MapUserPhysicalPages (modified_page_va, 1, NULL) == FALSE) {

            LeaveCriticalSection(&modified_list.list_lock);
            printf ("full_virtual_memory_test : could not unmap VA %p\n", modified_page_va);
            DebugBreak();

        }




        // Add the curr page to standby
        EnterCriticalSection(&standby_list.list_lock);

        curr_page->pagefile_num = i;
        addToHead(&standby_list, curr_page);

        LeaveCriticalSection(&standby_list.list_lock);

        LeaveCriticalSection(&modified_list.list_lock);

        // DM: Never had this! Added to set event to pagefault again
        SetEvent(pages_available);

     }
}

LPTHREAD_START_ROUTINE handle_aging() {
    ULONG64 ptes_per_region = pgtb->num_ptes / NUM_PTE_REGIONS;

    while (1) {
        WaitForSingleObject(aging_event, INFINITE);

        // if (DWORD signal_received != aging_event) {
        //     return;
        // }

        // Will loop through every PTE, by their lock
        // region, and increment the ones that are active

        for (unsigned i = 0; i < NUM_PTE_REGIONS; i++) {

            LockPagetable(i);
            //EnterCriticalSection(&pgtb->lock);
            
            for (unsigned j = i * ptes_per_region; j < ((i+1) * ptes_per_region); j++) {

                PTE* curr_pte = &pgtb->pte_array[j];

                if (curr_pte->memory_format.valid == 1) {
                    if (curr_pte->memory_format.age < 7) {

                        PTE local_contents;
                        local_contents.entire_format = 0;

                        local_contents.memory_format.valid = 1;
                        local_contents.memory_format.frame_number = curr_pte->memory_format.frame_number;
                        local_contents.memory_format.age++;

                        WriteToPTE(curr_pte, local_contents);

                    }
                }
            }

            UnlockPagetable(i);
            //LeaveCriticalSection(&pgtb->lock);

        }
    }
}


// DM: Remove the calculations and make them global!

LPTHREAD_START_ROUTINE handle_faulting() {
    unsigned j;
    PULONG_PTR arbitrary_va;
    BOOL page_faulted;
    unsigned random_number;
    ULONG_PTR virtual_address_size;
    ULONG_PTR virtual_address_size_in_unsigned_chunks;

    virtual_address_size = 64 * NUMBER_OF_PHYSICAL_PAGES * PAGE_SIZE;

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

    num_pagefile_blocks = (virtual_address_size / PAGE_SIZE) - NUMBER_OF_PHYSICAL_PAGES + 2;


    virtual_address_size_in_unsigned_chunks =
                        virtual_address_size / sizeof (ULONG_PTR);

    arbitrary_va = NULL;

    for (j = 0; j < MB (1); j += 1) {

        // Trigger events:
        // 1) Have an aging event every 32nd random access
        // 2) Have a trimming event once the freelist pages runs
        // below about 15%

        if (j % 32 == 0) {
            SetEvent(aging_event);
        }


        // DM: Maybe don't do this every time now?
        // We may be trimming too much too fast, and
        // inadvertently making the modified writer wait
        // a long time since the pagefile will constantly be full

        if ((standby_list.num_of_pages + modified_list.num_of_pages) <= (2 * (pgtb->num_ptes) / 7)) {
            SetEvent(trim_now);
        }


        page_faulted = FALSE;


        if (arbitrary_va == NULL) {

            random_number = rand () * rand () * rand ();

            random_number %= virtual_address_size_in_unsigned_chunks;

            random_number = random_number &~ 7;
            arbitrary_va = (PULONG_PTR)((ULONG_PTR)p + random_number);

        }

        __try {

            *arbitrary_va = (ULONG_PTR) arbitrary_va;

            arbitrary_va = NULL;

        } __except (EXCEPTION_EXECUTE_HANDLER) {

            page_faulted = TRUE;
        }



        if (page_faulted) {

            BOOL pagefault_success = pagefault(arbitrary_va);
            if (pagefault_success == FALSE) {
                //printf("Failed pagefault\n");
            }

            j -=1 ;
            continue;

        }
    }
    
    printf("full_virtual_memory_test : finished accessing %u random virtual addresses\n", j);
    return NULL;
}


// Creating the threads to use 
HANDLE* initialize_threads(VOID)
{
    HANDLE* threads = (HANDLE*) malloc(sizeof(HANDLE) * NUM_OF_THREADS);

    threads[0] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_aging, NULL, 0, NULL);
    threads[1] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_trimming, NULL, 0, NULL);
    threads[2] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_modifying, NULL, 0, NULL);
    threads[3] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_faulting, NULL, 0, NULL);
    threads[4] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_faulting, NULL, 0, NULL);

    return threads;
}


//#if 0

VOID LockPagetable(unsigned i) {
    if (i >= NUM_PTE_REGIONS) {
        printf("Outside PTE Region\n");
        DebugBreak();
    }

    EnterCriticalSection(&pgtb->pte_regions_locks[i].lock);
    DWORD curr_thread = GetCurrentThreadId();

    // if (pgtb->pte_regions_locks[i].owning_thread != 0) {
    //     DebugBreak();
    // }

    pgtb->pte_regions_locks[i].owning_thread = curr_thread;
}



VOID UnlockPagetable(unsigned i) {
    if (i >= NUM_PTE_REGIONS) {
        printf("Outside PTE Region\n");
        DebugBreak();
    }

    DWORD curr_thread = GetCurrentThreadId();

    // if (pgtb->pte_regions_locks[i].owning_thread != curr_thread) {
    //     DebugBreak();
    // }

    pgtb->pte_regions_locks[i].owning_thread = 0;
    LeaveCriticalSection(&pgtb->pte_regions_locks[i].lock);
}


//#endif



VOID WriteToPTE(PTE* pte, PTE pte_contents) {
    DWORD curr_thread = GetCurrentThreadId();

    PULONG_PTR conv_va = pte_to_va(pte, pgtb);
    ULONG64 conv_index = va_to_pte_index(conv_va, pgtb);
    ULONG64 pte_region_index_for_lock = conv_index / PTES_PER_REGION;

    // if (pgtb->pte_regions_locks[pte_region_index_for_lock].owning_thread != curr_thread) {
    //     DebugBreak();
    // }

    *pte = pte_contents;
}