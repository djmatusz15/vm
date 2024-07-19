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

                    addToHead(&modified_list, curr_page);

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


    // DM: Keep array to write as a batch to
    // MapUserPhysicalPages. In the for loop,
    // pop each page from modified and plug it.
    // Just keep it as pages, not VAs


    page_t** batched_pages;

    batched_pages = malloc(sizeof(page_t*) * BATCH_SIZE);


     while (1) {

        // Don't try to batch until you have at
        // least 8 pages on the modified list and
        // 8 free spots on the disk

        //WaitForSingleObject(pagefile_blocks_available, INFINITE);

        if (modified_list.num_of_pages <= BATCH_SIZE || pf.free_pagefile_blocks <= BATCH_SIZE) {
            continue;
        }

        EnterCriticalSection(&modified_list.list_lock);

        // Check again to make sure that we have 
        // 8 pages or more on the modified list
        // and 8 free spots on the disk 
        
        if (modified_list.num_of_pages <= BATCH_SIZE || pf.free_pagefile_blocks <= BATCH_SIZE) {
            LeaveCriticalSection(&modified_list.list_lock);
            continue;
        }


        // If this is still true, debug break

        if (modified_list.num_of_pages <= BATCH_SIZE || pf.free_pagefile_blocks <= BATCH_SIZE) {
            DebugBreak();
        }


        // Leave out first disk space so that 
        // there are no collisions with frame numbers
        // in valid or transition PTEs; no ambiguity

        unsigned i;
        // unsigned blocks_taken = 0;
        for (i = 1 ; i < num_pagefile_blocks; i++) {


            // DM: Find a chunk of 8 pagefile slots
            // that are free

            unsigned check_if_free = i;
            unsigned last_free_block = i + 8;
            while (check_if_free < last_free_block) {
                if (pf.pagefile_state[check_if_free] == IN_USE) {
                    break;
                }
                check_if_free++;
            }

            // If we didn't find 8 free blocks, continue
            // do next for loop iteration

            if (check_if_free != last_free_block) {
                continue;
            }

            // if we made it, break out of the for loop
            break;

        }

        for (unsigned j = 0; j < BATCH_SIZE; j++) {

            page_t* curr_page = popHeadPage(&modified_list);

            if (curr_page == NULL) {
                printf("Page corrupted or mod list NULL\n");
                DebugBreak();
                LeaveCriticalSection(&modified_list.list_lock);
                return NULL;
            }

            batched_pages[j] = curr_page;

        }

        // Use i to know where to start mapping to on the pagefile

        BOOL mapped_batch_to_pagefile = map_batch_to_pagefile(batched_pages, i);
        if (mapped_batch_to_pagefile == FALSE) {
            printf("Mapping to temp VA didn't work\n");
            DebugBreak();
            return NULL;
        }

        LeaveCriticalSection(&modified_list.list_lock);

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

    #if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;

    LPVOID modified_page_va2 = VirtualAlloc2 (NULL,
                       NULL,
                       PAGE_SIZE,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &parameter,
                       1);

    #else

    // DM: this is for writing from disk back to new page
    LPVOID modified_page_va2 = VirtualAlloc(NULL,
                      PAGE_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

    if (modified_page_va2 == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory for temp2 VA\n");
        return;
    }

    #endif

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

            BOOL pagefault_success = pagefault(arbitrary_va, modified_page_va2);
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


// At this point, handle_modifying already has
// modified_list lock (for mapping a single page
// to pagefile at a time, outdated)

BOOL map_to_pagefile(page_t* curr_page, unsigned pagefile_slot) {

    ULONG64 conv_pfn = curr_page->pte->transition_format.frame_number;

    if (MapUserPhysicalPages (modified_page_va, 1, &conv_pfn) == FALSE) {

        LeaveCriticalSection(&modified_list.list_lock);
        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_page_va, conv_pfn);
        DebugBreak();

        return FALSE;

    }

    memcpy(&pf.pagefile_contents[pagefile_slot * PAGE_SIZE], modified_page_va, PAGE_SIZE);


    if (MapUserPhysicalPages (modified_page_va, 1, NULL) == FALSE) {

        LeaveCriticalSection(&modified_list.list_lock);
        printf ("full_virtual_memory_test : could not unmap VA %p\n", modified_page_va);
        DebugBreak();

        return FALSE;

    }


    // Add the curr page to standby
    EnterCriticalSection(&standby_list.list_lock);

    curr_page->pagefile_num = pagefile_slot;
    addToHead(&standby_list, curr_page);

    LeaveCriticalSection(&standby_list.list_lock);

    return TRUE;
}


BOOL map_batch_to_pagefile(page_t** batched_pages, unsigned pagefile_slot) {

    // Get array of pfns, map array to temp VA, memcpy then unmap.
    // Don't forget to change size from 1 pages to 8!

    ULONG64* batched_pfns;

    batched_pfns = malloc(sizeof(ULONG64) * BATCH_SIZE);

    for (unsigned i = 0; i < BATCH_SIZE; i++) {
        ULONG64 curr_pfn = page_to_pfn(batched_pages[i]);
        batched_pfns[i] = curr_pfn;
    }

    if (MapUserPhysicalPages (modified_page_va, BATCH_SIZE, batched_pfns) == FALSE) {

        LeaveCriticalSection(&modified_list.list_lock);
        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_page_va, batched_pfns);
        DebugBreak();

        return FALSE;

    }

    memcpy(&pf.pagefile_contents[pagefile_slot * PAGE_SIZE], modified_page_va, BATCH_SIZE * PAGE_SIZE);


    if (MapUserPhysicalPages (modified_page_va, BATCH_SIZE, NULL) == FALSE) {

        LeaveCriticalSection(&modified_list.list_lock);
        printf ("full_virtual_memory_test : could not unmap VA %p\n", modified_page_va);
        DebugBreak();

        return FALSE;

    }


    // Add the pages to standby
    EnterCriticalSection(&standby_list.list_lock);

    for (unsigned i = 0; i < BATCH_SIZE; i++) {

        batched_pages[i]->pagefile_num = pagefile_slot + i;
        addToHead(&standby_list, batched_pages[i]);

    }

    LeaveCriticalSection(&standby_list.list_lock);

    return TRUE;
}