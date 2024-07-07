#include "threads.h"
#include "globals.h"


// Handles trimming PTEs that are active, and moving them
// to modified list. Switch the PTE as well.

LPTHREAD_START_ROUTINE handle_trimming() {

    ULONG64 ptes_per_region = pgtb->num_ptes / NUM_PTE_REGIONS;

    while (1) {
        WaitForSingleObject(trim_now, 0);
        ULONG64 count;
        ULONG64 region;
        PTE* curr_pte;
        BOOL trimmed_enough = FALSE;
        unsigned trimmed_pages_count = 0;
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


                    // DM: there shouldn't be any problems here

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

                    
                    if ((freelist.num_of_pages + modified_list.num_of_pages) >= (2 * (pgtb->num_ptes)) / 7) {
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


     while (1) {

        EnterCriticalSection(&modified_list.list_lock);

        page_t* curr_page = popHeadPage(&modified_list);
        if (curr_page == NULL) {
            //printf("Could not pop head from modified list\n");

            LeaveCriticalSection(&modified_list.list_lock);
            WaitForSingleObject(modified_list_notempty, 0);
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
            WaitForSingleObject(pagefile_blocks_available, 0);
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

     }
}

LPTHREAD_START_ROUTINE handle_aging() {
    ULONG64 ptes_per_region = pgtb->num_ptes / NUM_PTE_REGIONS;

    while (1) {
        WaitForSingleObject(aging_event, 0);

        // Will loop through every PTE, by their lock
        // region, and increment the ones that are active

        for (unsigned i = 0; i < NUM_PTE_REGIONS; i++) {

            LockPagetable(i);
            
            for (unsigned j = i * ptes_per_region; j < ((i+1) * ptes_per_region); j++) {

                PTE* curr_pte = &pgtb->pte_array[j];

                if (curr_pte->memory_format.valid == 1) {
                    if (curr_pte->memory_format.age < 7) {

                        // PTE local_contents;
                        // PTE* curr_pte = &pgtb->pte_array[j];
                        
                        // local_contents = pgtb->pte_array[j];
                        // local_contents.memory_format.age++;
                        // WriteToPTE(curr_pte, local_contents);

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


// Creating the threads to use 
VOID initialize_threads(VOID)
{
    HANDLE* threads = (HANDLE*) malloc(sizeof(HANDLE) * 1);
    threads[0] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_aging, NULL, 0, NULL);
    threads[1] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_trimming, NULL, 0, NULL);
    threads[2] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) handle_modifying, NULL, 0, NULL);
}




VOID LockPagetable(unsigned i) {
    if (i >= NUM_PTE_REGIONS) {
        printf("Outside PTE Region\n");
        DebugBreak();
    }

    EnterCriticalSection(&pgtb->pte_regions_locks[i].lock);
    DWORD curr_thread = GetCurrentThreadId();

    if (pgtb->pte_regions_locks[i].owning_thread != 0) {
        DebugBreak();
    }

    pgtb->pte_regions_locks[i].owning_thread = curr_thread;
}



VOID UnlockPagetable(unsigned i) {
    if (i >= NUM_PTE_REGIONS) {
        printf("Outside PTE Region\n");
        DebugBreak();
    }

    DWORD curr_thread = GetCurrentThreadId();

    if (pgtb->pte_regions_locks[i].owning_thread != curr_thread) {
        DebugBreak();
    }

    pgtb->pte_regions_locks[i].owning_thread = 0;
    LeaveCriticalSection(&pgtb->pte_regions_locks[i].lock);
}



VOID WriteToPTE(PTE* pte, PTE pte_contents) {
    DWORD curr_thread = GetCurrentThreadId();

    PULONG_PTR conv_va = pte_to_va(pte, pgtb);
    ULONG64 conv_index = va_to_pte_index(conv_va, pgtb);
    ULONG64 pte_region_index_for_lock = conv_index / PTES_PER_REGION;

    if (pgtb->pte_regions_locks[pte_region_index_for_lock].owning_thread != curr_thread) {
        DebugBreak();
    }

    *pte = pte_contents;
}