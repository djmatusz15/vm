#include "threads.h"
#include "globals.h"


// Handles trimming PTEs that are active, and moving them
// to modified list.

// DM:
// Must add to head and pop from tail!!
// Removing and adding to head! Not efficient
// and not going to work in some places!

LPTHREAD_START_ROUTINE handle_trimming() {

    // srand(time(thread_num * 100));

    ULONG64 ptes_per_region = pgtb->num_ptes / NUM_PTE_REGIONS;
    page_t** batched_pages;
    unsigned int batched_pages_count;


    batched_pages = malloc(sizeof(page_t*) * BATCH_SIZE);
    if (batched_pages == NULL) {
        printf("Could not malloc batched_pages\n");
        return NULL;
    }


    while (1) {

        WaitForSingleObject(trim_now, INFINITE);
        ULONG64 count;
        ULONG64 region;
        PTE* curr_pte;
        BOOL trimmed_enough = FALSE;
        unsigned i;


        batched_pages_count = 0;

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


                    page_t* curr_page = pfn_to_page(curr_pte->memory_format.frame_number, pgtb);

                    // Make sure the page this PTE has doesn't have a pagefile num
                    if (curr_page->pagefile_num != 0) {
                        DebugBreak();
                    }


                    batched_pages[batched_pages_count] = curr_page;


                    batched_pages_count++;

                    if (batched_pages_count == BATCH_SIZE) {
                        break;
                    }

                }
            }

            if (batched_pages_count != 0) {
                write_ptes_to_modified(batched_pages, batched_pages_count);
                unmap_batch(batched_pages, batched_pages_count);
                add_pages_to_modified(batched_pages, batched_pages_count);
                batched_pages_count = 0;
            }

            
            UnlockPagetable(i);
        }

        if (i == NUM_PTE_REGIONS) {
            continue;
        }


    }
}




// Dynamic batch size for the mod writer

#define FREE 0
#define IN_USE 1

LPTHREAD_START_ROUTINE handle_modifying() {


    // srand(time(thread_num * 100));


    // DM: Keep array to write as a batch to
    // MapUserPhysicalPages. In the for loop,
    // pop each page from modified and plug it.
    // Just keep it as pages, not VAs


    page_t** batched_pages;
    int* slots_to_map_to;
    unsigned curr_batch_end_index;

    // Set the batch size dynamically to whatever we got,
    // and malloc for the batch array as well. 

    batched_pages = malloc(sizeof(page_t*) * BATCH_SIZE);
    if (batched_pages == NULL) {
        printf("Couldn't malloc for batched pages\n");
        return NULL;
    }

    slots_to_map_to = malloc(sizeof(int) * BATCH_SIZE);
    if (slots_to_map_to == NULL) {
        printf("Couldn't malloc for slots to map to on pagefile\n");
        return NULL;
    }


     while (1) {

        WaitForSingleObject(modified_list_notempty, INFINITE);

        EnterCriticalSection(&modified_list.list_lock);

        // Wait for both modified pages and pagefile
        // blocks to be available

        if (modified_list.num_of_pages == 0) {
            LeaveCriticalSection(&modified_list.list_lock);
            continue;
        }

        if (pf.free_pagefile_blocks == 0) {
            LeaveCriticalSection(&modified_list.list_lock);
            WaitForSingleObject(pagefile_blocks_available, INFINITE);
            continue;
        }

        if (modified_list.num_of_pages == 0 || pf.free_pagefile_blocks == 0) {
            DebugBreak();

        }


        // Leave out first disk space so that 
        // there are no collisions with frame numbers
        // in valid or transition PTEs; no ambiguity

        unsigned curr_batch_size = min(BATCH_SIZE, modified_list.num_of_pages);


        // CANNOT DO THIS! We must be able to accomodate
        // any batch size, no matter how small. We can
        // get as many free spaces as we can from the pagefile
        // without abandoning our search. With the other if
        // statement, we could end up in an endless loop
        // of trying to find pagefile spaces to match
        // curr_batch_size, but never find it.

        // if (curr_batch_size > pf.free_pagefile_blocks) {
        //     LeaveCriticalSection(&modified_list.list_lock);
        //     WaitForSingleObject(pagefile_blocks_available, INFINITE);
        //     continue;
        // }


        int curr_slots_found = 0;

        for (unsigned i = 1 ; i < num_pagefile_blocks; i++) {


            if (pf.pagefile_state[i] == 0) {
                slots_to_map_to[curr_slots_found] = i;
                curr_slots_found++;
            }

            if (curr_slots_found == curr_batch_size) {
                break;
            }

        }



        for (unsigned j = 0; j < curr_batch_size; j++) {

            page_t* curr_page = popHeadPage(&modified_list);

            if (curr_page == NULL) {
                printf("Page corrupted or mod list NULL\n");
                DebugBreak();
                LeaveCriticalSection(&modified_list.list_lock);
                return NULL;
            }

            batched_pages[j] = curr_page;

        }

        // DM: MAKE SURE WE DON'T MISS ANY MAPPING BELOW!

        BOOL mapped_batch_to_pagefile = map_batch_to_pagefile(batched_pages, slots_to_map_to, curr_batch_size);
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
    // ULONG64 ptes_per_region = PTES_PER_REGION;

    while (1) {
        WaitForSingleObject(aging_event, INFINITE);


        // Will loop through every PTE, by their lock
        // region, and increment the ones that are active

        for (unsigned i = 0; i < NUM_PTE_REGIONS; i++) {

            LockPagetable(i);
            
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

        }
    }
}

#if RANDOM_ACCESSES

LPTHREAD_START_ROUTINE handle_faulting() {
    unsigned j;
    PULONG_PTR arbitrary_va;
    BOOL page_faulted;
    unsigned random_number;
    FLUSHER* flusher;


    // srand(time(thread_num * 100));


    arbitrary_va = NULL;

    #if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    flusher = malloc(sizeof(FLUSHER*));
    if (flusher == NULL) {
        printf("Couldn't malloc for FLUSHER\n");
        return NULL;
    }

    flusher->temp_vas = (LPVOID*)malloc(sizeof(LPVOID) * NUM_TEMP_VAS);
    if (flusher->temp_vas == NULL) {
        printf("Couldn't malloc for temp VAS\n");
        return NULL;
    }

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;

    for (unsigned i = 0; i < NUM_TEMP_VAS; i++) {

        LPVOID modified_page_va2 = VirtualAlloc2 (NULL,
                        NULL,
                        PAGE_SIZE,
                        MEM_RESERVE | MEM_PHYSICAL,
                        PAGE_READWRITE,
                        &parameter,
                        1);

        if (modified_page_va2 == NULL) {
            printf("Couldn't malloc for temp VA to read from disk\n");
            return NULL;
        }

        flusher->temp_vas[i] = modified_page_va2;

    }


    flusher->num_of_vas_used = 0;


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

    for (j = 0; j < (MB (1) / (NUM_OF_FAULTING_THREADS)); j += 1) {

        // Trigger events:
        // 1) Have an aging event every 32nd random access
        // 2) Have a trimming event once the freelist pages runs
        // below about 15%

        if (j % 32 == 0) {
            SetEvent(aging_event);
        }

        if (standby_list.num_of_pages + modified_list.num_of_pages <= (( 2 * (pgtb->num_ptes)) / 7)) {
            SetEvent(trim_now);
        }


        page_faulted = FALSE;


        if (arbitrary_va == NULL) {

            // random_number = rand() * rand() * rand();

            // Not cryptographically strong, but gives great
            // distribution across the VA space

            random_number = ReadTimeStampCounter();

            random_number %= virtual_address_size_in_unsigned_chunks;

            random_number = random_number &~ 7;

            arbitrary_va = (PULONG_PTR)p + random_number;

        }

        __try {

            *arbitrary_va = (ULONG_PTR) arbitrary_va;

            arbitrary_va = NULL;

        } __except (EXCEPTION_EXECUTE_HANDLER) {

            page_faulted = TRUE;
        }



        if (page_faulted) {

            BOOL pagefault_success = pagefault(arbitrary_va, flusher);
            if (pagefault_success == FALSE) {
                // printf("Failed pagefault\n");
            }

            j -=1 ;
            continue;

        }
    }
    
    printf("full_virtual_memory_test : finished accessing %u random virtual addresses\n", j);
    return NULL;
}


#else

LPTHREAD_START_ROUTINE handle_faulting() {
    unsigned j;
    PULONG_PTR arbitrary_va;
    int consecutive_accesses;
    BOOL page_faulted;
    unsigned random_number;
    FLUSHER* flusher;


    // srand(time(thread_num * 100));


    arbitrary_va = NULL;
    consecutive_accesses = 0;

    #if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    flusher = malloc(sizeof(FLUSHER*));
    if (flusher == NULL) {
        printf("Couldn't malloc for FLUSHER\n");
        return NULL;
    }

    flusher->temp_vas = (LPVOID*)malloc(sizeof(LPVOID) * NUM_TEMP_VAS);
    if (flusher->temp_vas == NULL) {
        printf("Couldn't malloc for temp VAS\n");
        return NULL;
    }

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;

    for (unsigned i = 0; i < NUM_TEMP_VAS; i++) {

        LPVOID modified_page_va2 = VirtualAlloc2 (NULL,
                        NULL,
                        PAGE_SIZE,
                        MEM_RESERVE | MEM_PHYSICAL,
                        PAGE_READWRITE,
                        &parameter,
                        1);

        if (modified_page_va2 == NULL) {
            printf("Couldn't malloc for temp VA to read from disk\n");
            return NULL;
        }

        flusher->temp_vas[i] = modified_page_va2;

    }


    flusher->num_of_vas_used = 0;


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


    // This is to keep track of batched reads, so
    // we can map them all in one call. There must be 
    // a smarter way to do this in the TB flusher instead,
    // so I don't have to malloc more memory like this in
    // each faulter.

    PULONG_PTR* batch_vas_for_readins = malloc(sizeof(PULONG_PTR) * (CONSECUTIVE_ACCESSES / 8));
    if (batch_vas_for_readins == NULL) {
        printf("Couldn't malloc extra mem for batched read ins\n");
        return NULL;
    }


    ULONG64* batch_pfns_for_readins = malloc(sizeof(ULONG64) * (CONSECUTIVE_ACCESSES / 8));
    if (batch_pfns_for_readins == NULL) {
        printf("Couldn't malloc extra mem for batched read ins\n");
        return NULL;
    }


    for (j = 0; j < (MB ((TOTAL_FAULTS_IN_MB)) / (NUM_OF_FAULTING_THREADS)); j += 1) {

        // Trigger events:
        // 1) Have an aging event every 32nd random access
        // 2) Have a trimming event once the freelist pages runs
        // below about 15%

        if (j % 32 == 0) {
            SetEvent(aging_event);
        }

        if (standby_list.num_of_pages + modified_list.num_of_pages <= (( 2 * (pgtb->num_ptes)) / 7)) {
            SetEvent(trim_now);
        }


        if (consecutive_accesses == 0) {
            arbitrary_va = NULL;
        }
        
        /**
         * We want to make consecutive accesses very common. If we are doing consecutive accesses, we will increment the VA
         * into the next page
         */
        if (consecutive_accesses != 0 && page_faulted == FALSE) {
            if ((ULONG64) arbitrary_va + PAGE_SIZE < (ULONG64) p + VIRTUAL_ADDRESS_SIZE) {
                arbitrary_va += (PAGE_SIZE / sizeof(ULONG_PTR));
            } else {
                arbitrary_va = NULL;
            }
            consecutive_accesses --;
        }
        
        if (arbitrary_va == NULL) {

            // Not cryptographically strong, but good enough to get a spread-out distribution
            random_number = ReadTimeStampCounter();

            random_number %= virtual_address_size_in_unsigned_chunks;

            random_number = random_number &~ 7;

            arbitrary_va = (PULONG_PTR)p + random_number;

            consecutive_accesses = ReadTimeStampCounter() % CONSECUTIVE_ACCESSES;
        }

        //
        // Write the virtual address into each page.  
        //

        page_faulted = FALSE;

        __try {
            if (*arbitrary_va == 0) {
                
                *arbitrary_va = (ULONG_PTR) arbitrary_va;
                
            } 

            // DM: What does this check for?

            // else if((ULONG_PTR) *arbitrary_va != (ULONG_PTR) arbitrary_va) {

            //     DebugBreak();

            // }

        } __except (EXCEPTION_EXECUTE_HANDLER) {

            page_faulted = TRUE;
        }

        

        if (page_faulted) {
            BOOL pagefault_success = pagefault(arbitrary_va, flusher, batch_vas_for_readins, batch_pfns_for_readins);


            // We will not try a unique random address again, so we do not incrment j
            j--; 
        } 

    }

    printf("full_virtual_memory_test : finished accessing %u random virtual addresses\n", j);
    return NULL;
}


#endif


// Creating the threads to use 
HANDLE* initialize_threads()
{
    HANDLE* threads = (HANDLE*) malloc(sizeof(HANDLE) * NUM_OF_THREADS);

    threads[0] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_aging, NULL, 0, NULL);
    threads[0] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_trimming, NULL, 0, NULL);
    threads[1] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_modifying, NULL, 0, NULL);

    for (unsigned i = 3; i < NUM_OF_THREADS; i ++) {
        threads[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_faulting, NULL, 0, NULL);
    }

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

#if 0

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


#endif




BOOL map_batch_to_pagefile(page_t** batched_pages, int* slots_to_map_to, unsigned curr_batch_size) {

    // Get array of pfns, map array to temp VA, memcpy then unmap.

    ULONG64* batched_pfns;

    // DM: Do this in parent! Don't malloc

    batched_pfns = malloc(sizeof(ULONG64) * curr_batch_size);
    if (batched_pfns == NULL) {
        printf("Couldn't malloc memory for batched pfns\n");
        return FALSE;
    }

    for (unsigned i = 0; i < curr_batch_size; i++) {
        ULONG64 curr_pfn = page_to_pfn(batched_pages[i]);
        batched_pfns[i] = curr_pfn;
    }

    if (MapUserPhysicalPagesScatter (modified_writer_vas, curr_batch_size, batched_pfns) == FALSE) {

        LeaveCriticalSection(&modified_list.list_lock);
        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_writer_vas, batched_pfns);
        DebugBreak();

        return FALSE;

    }

    for (unsigned i = 0; i < curr_batch_size; i++) {

        memcpy(&pf.pagefile_contents[slots_to_map_to[i] * PAGE_SIZE], modified_writer_vas[i], PAGE_SIZE);

    }



    if (MapUserPhysicalPagesScatter (modified_writer_vas, curr_batch_size, NULL) == FALSE) {

        LeaveCriticalSection(&modified_list.list_lock);
        printf ("full_virtual_memory_test : could not unmap VA %p\n", modified_writer_vas);
        DebugBreak();

        return FALSE;

    }


    // Add the pages to standby
    EnterCriticalSection(&standby_list.list_lock);

    for (unsigned i = 0; i < curr_batch_size; i++) {

        // Add page to standby. Don't forget to set pagefile slot
        batched_pages[i]->pagefile_num = slots_to_map_to[i];

        pf.free_pagefile_blocks--;
        pf.pagefile_state[slots_to_map_to[i]] = IN_USE;

        addToHead(&standby_list, batched_pages[i]);

    }

    free(batched_pfns);

    LeaveCriticalSection(&standby_list.list_lock);

    return TRUE;
}


void unmap_batch (page_t** batched_pages, unsigned int curr_batch_size) {


    // DM: Do not malloc here! Do so in parent
    PULONG_PTR* batched_vas = malloc(sizeof(PULONG_PTR) * curr_batch_size);

    for (unsigned i = 0; i < curr_batch_size; i++) {
        PULONG_PTR curr_va = pte_to_va(batched_pages[i]->pte, pgtb);
        batched_vas[i] = curr_va;
    }

    if (MapUserPhysicalPagesScatter(batched_vas, curr_batch_size, NULL) == FALSE) {
        printf("Could not unmap batch for trimmer\n");
        DebugBreak();
    }

}


void write_ptes_to_modified(page_t** batched_pages, unsigned int curr_batch_size) {

    for (unsigned i = 0; i < curr_batch_size; i++) {

        PTE* curr_pte = batched_pages[i]->pte;

        PTE local_contents;
        local_contents.entire_format = 0;

        // Set the PTE so the its modified
        local_contents.transition_format.valid = 0;
        local_contents.transition_format.in_memory = 1;
        local_contents.transition_format.frame_number = curr_pte->memory_format.frame_number;

        WriteToPTE(curr_pte, local_contents);

    }
}



void add_pages_to_modified(page_t** batched_pages, unsigned int curr_batch_size) {

    EnterCriticalSection(&modified_list.list_lock);

    for (unsigned i = 0; i < curr_batch_size; i++) {
        addToHead(&modified_list, batched_pages[i]);
    }

    LeaveCriticalSection(&modified_list.list_lock);


    // DM: Make this the batch size? Arbitrary now
    if (modified_list.num_of_pages >= 256) {        // 512
        SetEvent(modified_list_notempty);
    }

    //SetEvent(modified_list_notempty);
}
