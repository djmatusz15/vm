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
                        write_ptes_to_modified(batched_pages, batched_pages_count);
                        unmap_batch(batched_pages, batched_pages_count);
                        add_pages_to_modified(batched_pages, batched_pages_count);
                        batched_pages_count = 0;
                    }

                }
            }

            if (batched_pages_count != 0) {
                // printf("Pages trimmed: %d\n", batched_pages_count);
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






// #define FREE 0
// #define IN_USE 1

// LPTHREAD_START_ROUTINE handle_modifying() {


//     // srand(time(thread_num * 100));


//     // DM: Keep array to write as a batch to
//     // MapUserPhysicalPages. In the for loop,
//     // pop each page from modified and plug it.
//     // Just keep it as pages, not VAs


//     page_t** batched_pages;
//     ULONG64* batched_pfns;
//     int* slots_to_map_to;
//     unsigned curr_batch_end_index;

//     // Set the batch size dynamically to whatever we got,
//     // and malloc for the batch array as well. 

//     batched_pages = malloc(sizeof(page_t*) * BATCH_SIZE);
//     if (batched_pages == NULL) {
//         printf("Couldn't malloc for batched pages\n");
//         return NULL;
//     }


//     batched_pfns = malloc(sizeof(ULONG64) * BATCH_SIZE);
//     if (batched_pfns == NULL) {
//         printf("Couldn't malloc memory for batched pfns\n");
//         return FALSE;
//     }

//     slots_to_map_to = malloc(sizeof(int) * BATCH_SIZE);
//     if (slots_to_map_to == NULL) {
//         printf("Couldn't malloc for slots to map to on pagefile\n");
//         return NULL;
//     }


//      while (1) {

//         //WaitForSingleObject(modified_list_notempty, INFINITE);

//         EnterCriticalSection(&modified_list.list_lock);

//         // Wait for both modified pages and pagefile
//         // blocks to be available

//         if (modified_list.num_of_pages == 0) {
//             LeaveCriticalSection(&modified_list.list_lock);
//             WaitForSingleObject(modified_list_notempty, INFINITE);
//             continue;
//         }

//         if (pf.free_pagefile_blocks == 0) {
//             LeaveCriticalSection(&modified_list.list_lock);
//             WaitForSingleObject(pagefile_blocks_available, INFINITE);
//             continue;
//         }

//         if (modified_list.num_of_pages == 0 || pf.free_pagefile_blocks == 0) {
//             DebugBreak();

//         }


//         // Leave out first disk space so that 
//         // there are no collisions with frame numbers
//         // in valid or transition PTEs; no ambiguity

//         unsigned curr_batch_size = min(BATCH_SIZE, modified_list.num_of_pages);


//         // CANNOT DO THIS! We must be able to accomodate
//         // any batch size, no matter how small. We can
//         // get as many free spaces as we can from the pagefile
//         // without abandoning our search. With the other if
//         // statement, we could end up in an endless loop
//         // of trying to find pagefile spaces to match
//         // curr_batch_size, but never find it.

//         // if (curr_batch_size > pf.free_pagefile_blocks) {
//         //     LeaveCriticalSection(&modified_list.list_lock);
//         //     WaitForSingleObject(pagefile_blocks_available, INFINITE);
//         //     continue;
//         // }


//         int curr_slots_found = 0;

//         EnterCriticalSection(&pf.pf_lock);

//         //unsigned curr_batch_size = min(BATCH_SIZE, pf.free_pagefile_blocks);

//         for (unsigned i = 1 ; i < num_pagefile_blocks; i++) {


//             if (pf.pagefile_state[i] == 0) {
//                 slots_to_map_to[curr_slots_found] = i;
//                 curr_slots_found++;
//             }

//             if (curr_slots_found == curr_batch_size) {
//                 break;
//             }

//         }

//         LeaveCriticalSection(&pf.pf_lock);

//         // This secures our batch size, making sure
//         // if we ran through all the slots in the 
//         // pagefile and couldn't find enough for the
//         // curr_batch_size we found, then we set it lower
//         // curr_batch_size = curr_slots_found;



//         for (unsigned j = 0; j < curr_batch_size; j++) {

//             page_t* curr_page = popHeadPage(&modified_list);

//             if (curr_page == NULL) {
//                 printf("Page corrupted or mod list NULL\n");
//                 DebugBreak();
//                 LeaveCriticalSection(&modified_list.list_lock);
//                 return NULL;

//                 // LeaveCriticalSection(&modified_list.list_lock);
//                 // curr_batch_size = j;
//                 // break;
//             }

//             curr_page->in_flight = 1;
//             batched_pages[j] = curr_page;
//             batched_pfns[j] = page_to_pfn(curr_page);

//         }

//         // DM: MAKE SURE WE DON'T OVERMAP.
//         // If we overmap, left over PFNs in
//         // the batch could be accidentally
//         // mapped to a different temp VA and,
//         // consequently, have its contents read
//         // into a completely different spot on disk

//         BOOL mapped_batch_to_pagefile = map_batch_to_pagefile(batched_pages, batched_pfns, slots_to_map_to, curr_batch_size);
//         if (mapped_batch_to_pagefile == FALSE) {
//             printf("Mapping to temp VA didn't work\n");
//             DebugBreak();
//             return NULL;
//         }

//         // LeaveCriticalSection(&modified_list.list_lock);

//         SetEvent(pages_available);

//      }
// }





/* For this mod writer, going to try and keep an array
of free spots. We initialize an array in the pagefile
structure the literally just keeps track of all the spots
that aren't in use. The mod writer pops free spots from the
tail, and the pagefault handler will batch disk reads and 
add their newly freed spots to the head. This way, we can
remove the linear walk to find free pagefile slots in the
mod writer, which looks like is taking a good amount of 
time just searching in xperf
*/


#define FREE 0
#define IN_USE 1

LPTHREAD_START_ROUTINE handle_modifying() {


    // srand(time(thread_num * 100));


    // DM: Keep array to write as a batch to
    // MapUserPhysicalPages. In the for loop,
    // pop each page from modified and plug it.
    // Just keep it as pages, not VAs


    page_t** batched_pages;
    ULONG64* batched_pfns;
    int* slots_to_map_to;

    // Set the batch size dynamically to whatever we got,
    // and malloc for the batch array as well. 

    batched_pages = malloc(sizeof(page_t*) * BATCH_SIZE);
    if (batched_pages == NULL) {
        printf("Couldn't malloc for batched pages\n");
        return NULL;
    }


    batched_pfns = malloc(sizeof(ULONG64) * BATCH_SIZE);
    if (batched_pfns == NULL) {
        printf("Couldn't malloc memory for batched pfns\n");
        return FALSE;
    }

    slots_to_map_to = malloc(sizeof(int) * BATCH_SIZE);
    if (slots_to_map_to == NULL) {
        printf("Couldn't malloc for slots to map to on pagefile\n");
        return NULL;
    }


     while (1) {

        //WaitForSingleObject(modified_list_notempty, INFINITE);

        EnterCriticalSection(&modified_list.list_lock);

        // Wait for both modified pages and pagefile
        // blocks to be available

        if (modified_list.num_of_pages == 0) {
            LeaveCriticalSection(&modified_list.list_lock);
            WaitForSingleObject(modified_list_notempty, INFINITE);
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

        EnterCriticalSection(&pf.pf_lock);


        while (pf.free_pagefile_blocks != 0) {

            int freed_tail_spot = takeFreePagefileSlot();
            if (freed_tail_spot < 0) {
                DebugBreak();
            }

            slots_to_map_to[curr_slots_found] = freed_tail_spot;

            curr_slots_found++;


            // If we hit our batch size or slots available is empty, break out
            if (curr_slots_found == curr_batch_size || pf.free_pagefile_blocks == 0) {
                break;
            }
        }

        LeaveCriticalSection(&pf.pf_lock);

        // This secures our batch size, making sure
        // if we ran through all the slots in the 
        // pagefile and couldn't find enough for the
        // curr_batch_size we found, then we set it lower
        // curr_batch_size = curr_slots_found;



        for (unsigned j = 0; j < curr_batch_size; j++) {

            page_t* curr_page = popHeadPage(&modified_list);

            if (curr_page == NULL) {
                printf("Page corrupted or mod list NULL\n");
                DebugBreak();
                LeaveCriticalSection(&modified_list.list_lock);
                return NULL;
            }

            curr_page->in_flight = 1;
            batched_pages[j] = curr_page;
            batched_pfns[j] = page_to_pfn(curr_page);

        }

        // DM: MAKE SURE WE DON'T OVERMAP.
        // If we overmap, left over PFNs in
        // the batch could be accidentally
        // mapped to a different temp VA and,
        // consequently, have its contents read
        // into a completely different spot on disk

        BOOL mapped_batch_to_pagefile = map_batch_to_pagefile(batched_pages, batched_pfns, slots_to_map_to, curr_batch_size);
        if (mapped_batch_to_pagefile == FALSE) {
            printf("Mapping to temp VA didn't work\n");
            DebugBreak();
            return NULL;
        }


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

    LPVOID temp_va_for_zeroing = VirtualAlloc2 (NULL,
                        NULL,
                        PAGE_SIZE,
                        MEM_RESERVE | MEM_PHYSICAL,
                        PAGE_READWRITE,
                        &parameter,
                        1);

    if (temp_va_for_zeroing == NULL) {
        printf("Couldn't malloc for temp VA to zero pages in handle_new_pte\n");
        return NULL;
    }


    // LPVOID* vas_for_zeroing_in_brandnewpte = malloc(sizeof(LPVOID) * (CONSECUTIVE_ACCESSES / 8));
    // if (vas_for_zeroing_in_brandnewpte == NULL) {
    //     printf("Couldn't malloc for VAs to zero in brand new PTE\n");
    //     return NULL;
    // }

    // for (unsigned i = 0; i < (CONSECUTIVE_ACCESSES / 8); i++) {

    //     LPVOID temp_va_for_zeroing_in_brandnewpte = VirtualAlloc2 (NULL,
    //                     NULL,
    //                     PAGE_SIZE,
    //                     MEM_RESERVE | MEM_PHYSICAL,
    //                     PAGE_READWRITE,
    //                     &parameter,
    //                     1);

    //     if (temp_va_for_zeroing_in_brandnewpte == NULL) {
    //         printf("Couldn't malloc for temp VA to zero pages in handle_new_pte\n");
    //         return NULL;
    //     }

    //     vas_for_zeroing_in_brandnewpte[i] = temp_va_for_zeroing_in_brandnewpte;

    // }



    LPVOID* batch_for_trimming_behind = malloc(sizeof(LPVOID) * PTES_TO_TRIM_BEHIND);
    if (batch_for_trimming_behind == NULL) {
        printf("Could not malloc for trimming behind batch\n");
        return NULL;
    }


    // LPVOID* batch_for_new_ptes = malloc(sizeof(LPVOID) * (CONSECUTIVE_ACCESSES / 8));
    // if (batch_for_new_ptes == NULL) {
    //     printf("Could not malloc for batched new ptes\n");
    //     return NULL;
    // }

    // ULONG64* batch_pfns_for_new_ptes = malloc(sizeof(ULONG64) * (CONSECUTIVE_ACCESSES / 8));
    // if (batch_pfns_for_new_ptes == NULL) {
    //     printf("Could not malloc for batched pages for new ptes\n");
    //     return NULL;
    // }


    for (j = 0; j < (MB ((TOTAL_FAULTS_IN_MB)) / (NUM_OF_FAULTING_THREADS)); j += 1) {

        // Trigger events:
        // 1) Have an aging event every 32nd random access
        // 2) Have a trimming event once the freelist pages runs
        // below about 15%

        if (j % 32 == 0) {
            SetEvent(aging_event);
        }

        if (standby_list.num_of_pages + modified_list.num_of_pages + freelist.num_of_pages + zero_list.num_of_pages <= (( 1 * (NUMBER_OF_PHYSICAL_PAGES)) / 3)) {
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

            // int vas_gotten_to_trim = 0;
            // for (PULONG_PTR i = (arbitrary_va - ((PAGE_SIZE) / sizeof(ULONG_PTR))); i > arbitrary_va - (PTES_TO_TRIM_BEHIND * ((PAGE_SIZE) / sizeof(ULONG_PTR))); i -= ((PAGE_SIZE) / sizeof(ULONG_PTR))) {

            //     ULONG64 pte_index_to_trim = va_to_pte_index(i, pgtb);
                
            //     if (pte_index_to_trim == -1) {
            //         continue;
            //     }

            //     PTE* attempt_pte_to_trim = &pgtb->pte_array[pte_index_to_trim];

            //     LockPagetable(pte_index_to_trim / PTES_PER_REGION);

            //     if (attempt_pte_to_trim->memory_format.valid == 1) {
            //         batch_for_trimming_behind[vas_gotten_to_trim] = i;
            //         vas_gotten_to_trim++;
            //     }

            //     else {
            //         UnlockPagetable(pte_index_to_trim / PTES_PER_REGION);
            //     }

            // }

            // if (vas_gotten_to_trim >= 64) {
            //     //printf("Trimming: %d\n", vas_gotten_to_trim);
            //     trim_behind_fault(batch_for_trimming_behind, vas_gotten_to_trim);
            // }
            // else {
            //     for (int i = 0; i < vas_gotten_to_trim; i++) {
            //         ULONG64 pte_index_to_unlock = va_to_pte_index(batch_for_trimming_behind[i], pgtb);
            //         ULONG64 pte_region_to_unlock = pte_index_to_unlock / PTES_PER_REGION;
            //         UnlockPagetable(pte_region_to_unlock);
            //     }
            // }

            // BOOL pagefault_success = pagefault(arbitrary_va, flusher, batch_vas_for_readins, batch_pfns_for_readins, vas_for_zeroing_in_brandnewpte, batch_for_new_ptes, batch_pfns_for_new_ptes);
            BOOL pagefault_success = pagefault(arbitrary_va, flusher, batch_vas_for_readins, batch_pfns_for_readins, temp_va_for_zeroing);


            // We will not try a unique random address again, so we do not incrment j
            j--; 
        } 

    }

    printf("full_virtual_memory_test : finished accessing %u random virtual addresses\n", j);
    return NULL;
}


#endif


// This thread goes through the freelist and zeroes out pages.
// Right now, we just grab a single page from each freelist
// if there is one and zero it out. This way, 
// processes that need zeroed pages can take them
// straight from the zero list without having to memset(0) them
// itself. Thus, now these processes will first check the zero
// lists, then the freelists, and finally the standby list if
// all else fails

LPTHREAD_START_ROUTINE handle_zeroing() {



    // DM: Change the sizes of these back to NUM_FREELISTS
    // if it does not improve amount of pages we can zero
    LPVOID* vas_for_zeroing = malloc(sizeof(LPVOID) * BATCH_SIZE);
    if (vas_for_zeroing == NULL) {
        printf("Could not malloc memory to hold temp VAs for zeroing\n");
        DebugBreak();
        return NULL;
    }


    ULONG64* pfns_for_zeroing = malloc(sizeof(ULONG64) * BATCH_SIZE);
    if (pfns_for_zeroing == NULL) {
        printf("Could not malloc memory to hold PFNs for zeroing\n");
        DebugBreak();
        return NULL;
    }


    MEM_EXTENDED_PARAMETER parameter = { 0 };

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;


    for (unsigned i = 0; i < BATCH_SIZE; i++) {
        LPVOID temp_va_for_zero_thread = VirtualAlloc2 (NULL,
                        NULL,
                        PAGE_SIZE,
                        MEM_RESERVE | MEM_PHYSICAL,
                        PAGE_READWRITE,
                        &parameter,
                        1);
        
        if (temp_va_for_zero_thread == NULL) {
            printf("couldn't malloc temp va for zero thread\n");
            return NULL;
        }

        vas_for_zeroing[i] = temp_va_for_zero_thread;
    }


    while (1) {

        // Wait for pages to be on the freelist,
        // so that the zero thread doesn't just spin
        WaitForSingleObject(pages_on_freelists, INFINITE);


        unsigned int num_pages_to_zero = 0;


        for (unsigned int i = 0; i < NUM_FREELISTS; i++) {

            if (num_pages_to_zero == BATCH_SIZE) {
                break;
            }

            // Try to get a page from this freelist.
            EnterCriticalSection(&freelist.freelists[i].list_lock);

            while (freelist.freelists[i].num_of_pages > 0) {
                page_t* page_to_zero = popHeadPage(&freelist.freelists[i]);
                
                if (page_to_zero == NULL) {
                    LeaveCriticalSection(&freelist.freelists[i].list_lock);
                    continue;
                }

                if (page_to_zero->pagefile_num != 0 || page_to_zero->num_of_pages != 0 || page_to_zero == NULL) {
                    printf("Pagefile num of this page is non-zero - handle_zeroing\n");
                    DebugBreak();
                }

                // Get PFN to zero and add to PFN batch
                ULONG64 pfn_to_zero = page_to_pfn(page_to_zero);
                pfns_for_zeroing[num_pages_to_zero] = pfn_to_zero;

                num_pages_to_zero++;
                if (num_pages_to_zero == BATCH_SIZE) {
                    break;
                }

            }

            LeaveCriticalSection(&freelist.freelists[i].list_lock);
            
        }

        // printf("pages to zero: %d\n", num_pages_to_zero);


        if (MapUserPhysicalPagesScatter(vas_for_zeroing, num_pages_to_zero, pfns_for_zeroing) == FALSE) {
            printf("Could not map PFNs to VAs for zeroing\n");
            DebugBreak();
            return NULL;
        }

        
        for (unsigned int i = 0; i < num_pages_to_zero; i++) {
            memset(vas_for_zeroing[i], 0, PAGE_SIZE);
        }

        if (MapUserPhysicalPagesScatter(vas_for_zeroing, num_pages_to_zero, NULL) == FALSE) {
            printf("Could not map PFNs to VAs for zeroing\n");
            DebugBreak();
            return NULL;
        }


        // Add the page back to its respective zero list
        for (unsigned int i = 0; i < num_pages_to_zero; i++) {

            ULONG64 zerolist_to_add_to = pfns_for_zeroing[i] % NUM_FREELISTS;

            EnterCriticalSection(&zero_list.freelists[zerolist_to_add_to].list_lock);
            addToHead(&zero_list.freelists[zerolist_to_add_to], pfn_to_page(pfns_for_zeroing[i], pgtb));
            LeaveCriticalSection(&zero_list.freelists[zerolist_to_add_to].list_lock);

        }


    }
}




// Instead of handling the movement of pages from
// standby to the freelists in the page fault handler,
// I am going to create this separate thread that
// does it in the background. Since I only move pages
// from standby to the freelists when I acquire the 
// standby list lock (which I only do once the freelists
// and zerolists are exhausted), I inadvertently create
// MASSIVE contention on the standby list lock. Once the
// pages are moved, even though we now have freelists pages, 
// the threads that were waiting for the standby list will
// rip right through these new pages, only to very quickly
// arrive at the same situation. By doing this, I will hopefully
// have a gradual transfer of pages from standby to the freelists,
// such that I no longer acquire the standby list lock and move pages
// when everybody absolutely has to have the standby list lock
// at the same time.

LPTHREAD_START_ROUTINE handle_moving_from_standby_to_freelists() {


    while (1) {


        // Wait for the thread to be triggered, so that we know
        // the freelists should start to get some pages
        WaitForSingleObject(too_few_pages_on_freelists, INFINITE);

        EnterCriticalSection(&standby_list.list_lock);


        unsigned int num_pages_on_standby = standby_list.num_of_pages;
        unsigned int num_to_move_to_freelist = num_pages_on_standby / 4;      // 128


        // printf("pages on standby: %d\n", standby_list.num_of_pages);
        // printf("pages on freelists: %d\n", freelist.num_of_pages);
        // printf("\n");

        if (num_pages_on_standby <= (.085 * NUMBER_OF_PHYSICAL_PAGES)) {
            SetEvent(trim_now);
            LeaveCriticalSection(&standby_list.list_lock);
            continue;
        }


    
        for (unsigned i = 0; i < num_to_move_to_freelist; i++) {

            // DM: Here, recycleOldestPage tries to enter the critical section
            // each time. Can optimize this by entering the section once


            page_t* curr_page = recycleOldestPage(-1);

            if (curr_page == NULL) {
                break;
            }



            // Need to tell this PTE that it's page is no
            // longer available for rescue, meaning it is 
            // invalid.

            PULONG_PTR va_of_curr_pages_pte = pte_to_va(curr_page->pte, pgtb);

            ULONG64 curr_pte_index = va_to_pte_index(va_of_curr_pages_pte, pgtb);
            ULONG64 curr_pte_index_region = curr_pte_index / PTES_PER_REGION;

            // LockPagetable(curr_pte_index_region);

            // DM: Back out if this will cause a deadlock, and try
            // again with a different page from standby list.

            if (TryEnterCriticalSection(&pgtb->pte_regions_locks[curr_pte_index_region].lock) == 0) {
                addToHead(&standby_list, curr_page);
                continue;
            }

            DWORD curr_thread = GetCurrentThreadId();
            pgtb->pte_regions_locks[curr_pte_index_region].owning_thread = curr_thread;


            PTE local_contents;
            local_contents.entire_format = 0;

            local_contents.disc_format.in_memory = 0;
            local_contents.disc_format.pagefile_num = curr_page->pagefile_num;
            WriteToPTE(curr_page->pte, local_contents);

            curr_page->pagefile_num = 0;

            UnlockPagetable(curr_pte_index_region);



            // Figure out which freelist to add the page back to,
            // and do it.

            int freelist_to_add_to = page_to_pfn(curr_page) % NUM_FREELISTS;

            EnterCriticalSection(&freelist.freelists[freelist_to_add_to].list_lock);

            addToHead(&freelist.freelists[freelist_to_add_to], curr_page);

            LeaveCriticalSection(&freelist.freelists[freelist_to_add_to].list_lock);


            // This is the only process that tries to move pages back
            // onto the freelists, so this is where we should trigger the
            // zeroing thread to let it know it can try to zero out 
            // some pages

            SetEvent(pages_on_freelists);

            
        }

        LeaveCriticalSection(&standby_list.list_lock);
    }
}




// Creating the threads to use 
HANDLE* initialize_threads()
{
    HANDLE* threads = (HANDLE*) malloc(sizeof(HANDLE) * NUM_OF_THREADS);

    threads[0] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_aging, NULL, 0, NULL);
    threads[1] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_trimming, NULL, 0, NULL);
    threads[2] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_modifying, NULL, 0, NULL);
    threads[3] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_zeroing, NULL, 0, NULL);
    threads[4] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_moving_from_standby_to_freelists, NULL, 0, NULL);

    for (unsigned i = 5; i < NUM_OF_THREADS; i ++) {
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






BOOL map_batch_to_pagefile(page_t** batched_pages, ULONG64* batched_pfns, int* slots_to_map_to, unsigned curr_batch_size) {

    

    // This is where we do the reference counting. Hopefully,
    // this allows us to not only drop the modified list lock
    // while we are writing pages to pagefile, but also can help
    // us retain disk slots  for other pages to use
    LeaveCriticalSection(&modified_list.list_lock);



    if (MapUserPhysicalPagesScatter (modified_writer_vas, curr_batch_size, batched_pfns) == FALSE) {

        LeaveCriticalSection(&modified_list.list_lock);
        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_writer_vas, batched_pfns);
        DebugBreak();

        return FALSE;

    }


    // If it hasn't been rescued, memcpy it like normal.
    // Skips over those that have been rescued

    EnterCriticalSection(&pf.pf_lock);

    for (unsigned i = 0; i < curr_batch_size; i++) {

        if (batched_pages[i]->was_rescued == 0) {
            memcpy(&pf.pagefile_contents[slots_to_map_to[i] * PAGE_SIZE], modified_writer_vas[i], PAGE_SIZE);
        }

    }

    LeaveCriticalSection(&pf.pf_lock);



    if (MapUserPhysicalPagesScatter (modified_writer_vas, curr_batch_size, NULL) == FALSE) {

        LeaveCriticalSection(&modified_list.list_lock);
        printf ("full_virtual_memory_test : could not unmap VA %p\n", modified_writer_vas);
        DebugBreak();

        return FALSE;

    }


    // Add the pages to standby
    EnterCriticalSection(&standby_list.list_lock);
    EnterCriticalSection(&pf.pf_lock);

    for (unsigned i = 0; i < curr_batch_size; i++) {

        // Add page to standby. Don't forget to set pagefile slot
        if (batched_pages[i]->was_rescued == 0) {
            pf.pagefile_state[slots_to_map_to[i]] = IN_USE;
            batched_pages[i]->pagefile_num = slots_to_map_to[i];
        }
        

        // Else, add the pagefile slot back to free pagefile
        // slot list, since it was rescued.
        else {
            addFreePagefileSlot(slots_to_map_to[i]);
        }


        if (batched_pages[i]->was_rescued == 0) {
            addToHead(&standby_list, batched_pages[i]);
        }

        batched_pages[i]->in_flight = 0;
        batched_pages[i]->was_rescued = 0;

    }

    SetEvent(pages_available);


    LeaveCriticalSection(&pf.pf_lock);
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
    if (modified_list.num_of_pages >= 512) {        // 256
        SetEvent(modified_list_notempty);
    }

    // SetEvent(modified_list_notempty);
}




// DM: For this, we assume that we are making
// consecutive accesses most of the time, i.e.
// the user is accessing a bunch of memory right
// next to each other sequentially. Therefore,
// it would make sense that if user used a piece/
// batch of memory, it is rather rare that the user
// will go back immediately and access the same
// piece/batch of memory. Therefore, we can 
// confidently trim the PTEs behind us, since we
// assume the user accessed the memory, used it,
// and will most likely not return.

// Potential setbacks:
// Assume thread 1 starts accessing PTEs at VA
// 20, and thread 2 starts faulting on VA 30.
// Assume be trim 8 PTEs behind us, if the are
// active, and the threads try to fault in the 
// 8 addresses in front of it. Thread 1 would
// read in addresses 22-28, only for thread 2 to
// immediately trim them, potentially before the
// user even has time to use them.


// Assume thread 1 starts accessing PTEs at VA 20, 
// and thread 2 starts faulting on VA 25. Assume, again
// the threads trim 8 PTEs behind and fault in the 8 
// PTEs in front of them. Thread 2 would not only do
// the same thing it did in scenario 1, but it would 
// now also try and trim the PTEs behind thread 1,
// meaning both faulters would try and unmap them.

// Acknowledging these potential setbacks, we assume
// that these scenarios are few, especially with a larger
// and larger virtual address space. Overall, in a real-
// world scenario, this should prove to be effective, and
// is a techinque that is utilized.
void trim_behind_fault(LPVOID* vas_to_trim_behind, int vas_to_trim_count) {

    for (int i = 0; i < vas_to_trim_count; i++) {
        PTE* test_pte = &pgtb->pte_array[va_to_pte_index(vas_to_trim_behind[i], pgtb)];
        if (test_pte->memory_format.valid != 1) {
            DebugBreak();
        }

        if (test_pte->entire_format == 0) {
            DebugBreak();
        }

    }

    if (MapUserPhysicalPagesScatter(vas_to_trim_behind, vas_to_trim_count, NULL) == FALSE) {
        printf("Could not trim PTEs behind faulter\n");
        DebugBreak();
    }

    for (int i = 0; i < vas_to_trim_count; i++) {
        PTE* curr_pte_to_make_modified = &pgtb->pte_array[(va_to_pte_index(vas_to_trim_behind[i], pgtb))];
        ULONG64 ptes_frame_number = curr_pte_to_make_modified->memory_format.frame_number;

        PTE local_contents;

        local_contents.entire_format = 0;
        local_contents.transition_format.valid = 0;
        local_contents.transition_format.in_memory = 1;
        local_contents.transition_format.frame_number = ptes_frame_number;

        WriteToPTE(curr_pte_to_make_modified, local_contents);


        page_t* page_to_move_to_modified = pfn_to_page(ptes_frame_number, pgtb);

        EnterCriticalSection(&modified_list.list_lock);
        addToTail(&modified_list, page_to_move_to_modified);
        LeaveCriticalSection(&modified_list.list_lock);

    }


    for (int i = 0; i < vas_to_trim_count; i++) {
        int region_to_unlock = (va_to_pte_index(vas_to_trim_behind[i], pgtb) / PTES_PER_REGION);
        UnlockPagetable(region_to_unlock);
    }

}