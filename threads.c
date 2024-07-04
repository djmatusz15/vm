#include "threads.h"
#include "globals.h"


// Handles trimming PTEs that are active, and moving them
// to modified list. Switch the PTE as well.

// DM: make sure to enter and leave locks correctly

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

            //EnterCriticalSection(&pgtb->pte_regions_locks[i]);
            LockPagetable(i);

            for (unsigned j = i * ptes_per_region; j < ((i+1) * (ptes_per_region)); j++) {

                if (pgtb->pte_array[j].memory_format.valid == 1) {
                    count = j;
                    region = i;

                    curr_pte = &pgtb->pte_array[count];

                    if (curr_pte->transition_format.frame_number == 0) {
                        DebugBreak();
                    }

                    PTE local_contents;
                    local_contents = *curr_pte;
                    local_contents.transition_format.valid = 0;
                    local_contents.transition_format.in_memory = 1;

                    WriteToPTE(curr_pte, local_contents);

                    // Found PTE to trim
                    //curr_pte->transition_format.valid = 0;
                    //curr_pte->transition_format.in_memory = 1;

                    // Switch is_modified to 1 once you implement modifying thread

                    PULONG_PTR conv_va = pte_to_va(curr_pte, pgtb);


                    if (MapUserPhysicalPages(conv_va, 1, NULL) == FALSE) {
                        DebugBreak();
                        printf("Couldn't unmap VA %p (handle_trimming)\n", conv_va);
                        return NULL;
                    }


                    // Was originally above the Mapping. If this was the
                    // case, we would have left the standby lock, and someone
                    // else could have swept in and took it before we unmapped it
                    EnterCriticalSection(&standby_list.list_lock);

                    addToHead(&standby_list, pfn_to_page(curr_pte->transition_format.frame_number, pgtb));

                    LeaveCriticalSection(&standby_list.list_lock);

                    
                    if ((freelist.num_of_pages + standby_list.num_of_pages) >= (2 * (pgtb->num_ptes)) / 7) {
                        trimmed_enough = TRUE;
                        break;
                    }

                    trimmed_pages_count++;
                    
                    break;
                }
            }
            UnlockPagetable(i);
        }

        if (i == NUM_PTE_REGIONS) {
            // printf("Didn't fill enough: %d\n", trimmed_pages_count);
            continue;
        }
    }
}

UCHAR pagefile_contents[100 * 4096];
UCHAR pagefile_state[100];

#define FREE 0
#define IN_USE 1

LPTHREAD_START_ROUTINE handle_modifying() {

    // This will also run constantly
    // DM: Are there cases where we may want to wait?

     while (1) {
        page_t* curr_page = popHeadPage(&modified_list);
        if (curr_page == NULL) {
            printf("Could not pop tail from modified list\n");
            WaitForSingleObject(modified_list_notempty, 0);
            continue;
        }

        // Leave out first disk space so that 
        // there are no collisions with frame numbers
        // in valid or transition PTEs; no ambiguity
        unsigned i;
        for (i = 1 ; i < PAGEFILE_BLOCKS; i++) {
            if (pagefile_state[i] == FREE) {
                pagefile_state[i] = IN_USE;
                break;
            }
        }

        if (i == PAGEFILE_BLOCKS) {
            printf("All disk slots in use\n");
            addToHead(&modified_list, curr_page);
            WaitForSingleObject(pagefile_blocks_available, 0);
            continue;
        }

        ULONG64 conv_pfn = curr_page->pte->transition_format.frame_number;
        if (MapUserPhysicalPages (modified_page_va, 1, &conv_pfn) == FALSE) {

            printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_page_va, conv_pfn);
            DebugBreak();

        }

        memcpy(&pagefile_contents[i * PAGE_SIZE], modified_page_va, PAGE_SIZE);
        
        // We don't need this, store as disc_number
        // in the page's PTE.
        // curr_page->pagefile = i;


        if (MapUserPhysicalPages (modified_page_va, 1, NULL) == FALSE) {

            printf ("full_virtual_memory_test : could not unmap VA %p\n", modified_page_va);
            DebugBreak();

        }
        

        addToHead(&standby_list, curr_page);

        // curr_page->pte->disc_format.disc_number = i;
        // curr_page->pte->disc_format.valid = 0;
        // curr_page->pte->disc_format.in_memory = 0;
     }
}

LPTHREAD_START_ROUTINE handle_aging() {
    ULONG64 ptes_per_region = pgtb->num_ptes / NUM_PTE_REGIONS;

    while (1) {
        WaitForSingleObject(aging_event, 0);

        // Will loop through every PTE, by their lock
        // region, and increment the ones that are active

        for (unsigned i = 0; i < NUM_PTE_REGIONS; i++) {
            //EnterCriticalSection(&pgtb->pte_regions_locks[i]);
            LockPagetable(i);
            
            for (unsigned j = i * ptes_per_region; j < ((i+1) * ptes_per_region); j++) {
                if (pgtb->pte_array[j].memory_format.valid == 1) {
                    if (pgtb->pte_array[j].memory_format.age < 8) {

                        PTE local_contents;
                        PTE* curr_pte = &pgtb->pte_array[j];
                        
                        local_contents = pgtb->pte_array[j];
                        local_contents.memory_format.age++;
                        WriteToPTE(curr_pte, local_contents);

                        //pgtb->pte_array[j].memory_format.age++;
                    }
                }
            }

            //LeaveCriticalSection(&pgtb->pte_regions_locks[i]);
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