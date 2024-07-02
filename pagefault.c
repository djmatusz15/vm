#include "pagefault.h"

BOOL pagefault(PULONG_PTR arbitrary_va) {
    ULONG64 pte_pfn;
    ULONG64 conv_index = va_to_pte(arbitrary_va, pgtb);
    ULONG64 pte_region_index_for_lock = conv_index / 128;
    EnterCriticalSection(&pgtb->pte_regions_locks[pte_region_index_for_lock]);

    PTE* curr_pte = &pgtb->pte_array[conv_index];

    if (curr_pte == NULL) {
        printf("Could not get a valid PTE given VA\n");
        return FALSE;
    }

    page_t* curr_page;
    BOOL get_from_modified = FALSE;


    if (curr_pte->memory_format.frame_number != 0) {
        // Used by different thread
        if (curr_pte->memory_format.valid == 1) {
            printf("Already active by another thread, continue on\n");
            LeaveCriticalSection(&pgtb->pte_regions_locks[pte_region_index_for_lock]);
            return TRUE;
        }

        // Given page on modified or standby
        else if (curr_pte->transition_format.valid == 0 && curr_pte->transition_format.in_memory == 1) {
            LeaveCriticalSection(&pgtb->pte_regions_locks[pte_region_index_for_lock]);
            return TRUE;
            if (curr_pte->transition_format.is_modified == 1) {
                get_from_modified = TRUE;
            }
        }

        // On disk (shouldn't happen yet)
        else if (curr_pte->disc_format.valid == 0 && curr_pte->disc_format.in_memory == 0) {
            printf("Given page is on disc, go get it\n");
            LeaveCriticalSection(&pgtb->pte_regions_locks[pte_region_index_for_lock]);
            return TRUE;

            // Get the frame from disk,
            // write to new frame and give back

            // Get pfn stored in this PTE
            pte_pfn = curr_pte->disc_format.disc_number;

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


        // DM: Will have to hold on to these locks for a bit longer:
        // see later leaving of freelist for reference
        if (get_from_modified) {
            pte_pfn = curr_pte->transition_format.frame_number;
            EnterCriticalSection(&modified_list.list_lock);
            curr_page = popFromAnywhere(&modified_list, pfn_to_page(pte_pfn, pgtb));

            // DM: Fix the modified listing so its active now

            LeaveCriticalSection(&modified_list.list_lock);
        }
        else {
            pte_pfn = curr_pte->transition_format.frame_number;
            EnterCriticalSection(&standby_list.list_lock);
            curr_page = popFromAnywhere(&standby_list, pfn_to_page(pte_pfn, pgtb));

            // DM: Fix the standby listing so its active now
            

            LeaveCriticalSection(&standby_list.list_lock);
        }


    }


    else{
        EnterCriticalSection(&freelist.list_lock);

        curr_page = popHeadPage(&freelist);


        if (curr_page == NULL) {
            LeaveCriticalSection(&freelist.list_lock);

            EnterCriticalSection(&standby_list.list_lock);

            curr_page = popHeadPage(&standby_list);
            if (curr_page == NULL) {
                //printf("Also could not get from standby\n");
                LeaveCriticalSection(&standby_list.list_lock);
                return TRUE;
            }
            LeaveCriticalSection(&standby_list.list_lock);
        }
        else {
            LeaveCriticalSection(&freelist.list_lock);
        }
        
        ULONG64 popped_pfn = page_to_pfn(curr_page);
        curr_page->pte = curr_pte;

        curr_pte->memory_format.age = 0;
        curr_pte->memory_format.valid = 1;
        curr_pte->memory_format.frame_number = popped_pfn;

        if (MapUserPhysicalPages (arbitrary_va, 1, &popped_pfn) == FALSE) {

            DebugBreak();
            printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, popped_pfn);

            return FALSE;
        }

        *arbitrary_va = (ULONG_PTR) arbitrary_va;

        LeaveCriticalSection(&pgtb->pte_regions_locks[pte_region_index_for_lock]);
        return TRUE;
    }
    return FALSE;
}