#include "pagefault.h"

BOOL pagefault(PULONG_PTR arbitrary_va) {
    // DM: Could the converted index be changed
    // before we enter the lock

    ULONG64 conv_index = va_to_pte_index(arbitrary_va, pgtb);
    ULONG64 pte_region_index_for_lock = conv_index / 32;
    PTE_LOCK* pte_lock;
    ULONG64 pte_pfn;
    ULONG64 popped_pfn;
    page_t* curr_page;
    BOOL get_from_modified = FALSE;

    //pte_lock = &pgtb->pte_regions_locks[pte_region_index_for_lock];
    
    //EnterCriticalSection(pte_lock);
    LockPagetable(pte_region_index_for_lock);

    PTE* curr_pte = &pgtb->pte_array[conv_index];

    if (curr_pte == NULL) {
        printf("Could not get a valid PTE given VA\n");
        //LeaveCriticalSection(pte_lock);
        UnlockPagetable(pte_region_index_for_lock);
        return FALSE;
    }


    if (curr_pte->memory_format.frame_number != 0) {
        // Used by different thread
        if (curr_pte->memory_format.valid == 1) {
            DebugBreak();
            printf("Already active by another thread, continue on\n");
            //LeaveCriticalSection(pte_lock);
            UnlockPagetable(pte_region_index_for_lock);
            return TRUE;
        }

        // Given page on modified or standby
        else if (curr_pte->transition_format.valid == 0 && curr_pte->transition_format.in_memory == 1) {
            // if (curr_pte->transition_format.is_modified == 1) {
            //     get_from_modified = TRUE;
            // }
        }

        // On disk (shouldn't happen yet)
        else if (curr_pte->disc_format.valid == 0 && curr_pte->disc_format.in_memory == 0) {
            DebugBreak();
            printf("Given page is on disc, go get it\n");
            //LeaveCriticalSection(pte_lock);
            UnlockPagetable(pte_region_index_for_lock);
            return TRUE;

            // Get the frame from disk,
            // write to new frame and give back

        }


        // If it's on modified, rescue it from modified (shouldn't happen yet!)
        if (get_from_modified) {
            DebugBreak();
            LeaveCriticalSection(&modified_list.list_lock);
            //LeaveCriticalSection(pte_lock);
            UnlockPagetable(pte_region_index_for_lock);
            return TRUE;

            pte_pfn = curr_pte->transition_format.frame_number;
            EnterCriticalSection(&modified_list.list_lock);
            curr_page = popFromAnywhere(&modified_list, pfn_to_page(pte_pfn, pgtb));

            // DM: Fix the modified listing so its active now


            LeaveCriticalSection(&modified_list.list_lock);
            //LeaveCriticalSection(pte_lock);
            UnlockPagetable(pte_region_index_for_lock);
            return TRUE;
        }



        
        // If its on standby, rescue it from the standby list
        else {
            EnterCriticalSection(&standby_list.list_lock);

            pte_pfn = curr_pte->transition_format.frame_number;
            curr_page = popFromAnywhere(&standby_list, pfn_to_page(pte_pfn, pgtb));

            // DM: Fix the standby listing so its active now

            if (MapUserPhysicalPages (arbitrary_va, 1, &pte_pfn) == FALSE) {

                DebugBreak();
                printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, pte_pfn);
                //LeaveCriticalSection(pte_lock);
                UnlockPagetable(pte_region_index_for_lock);
                return FALSE;
            }


            if (curr_page->pte != curr_pte) {
                DebugBreak();
            }

            if (curr_pte->memory_format.frame_number != pte_pfn) {
                DebugBreak();
            }
            
            //curr_pte->memory_format.age = 0;
            //curr_pte->memory_format.valid = 1;
            //curr_pte->memory_format.frame_number = pte_pfn;
            //curr_page->pte = curr_pte;

            PTE local_contents;
            local_contents = *curr_pte;

            local_contents.memory_format.age = 0;
            local_contents.memory_format.valid = 1;
            local_contents.memory_format.frame_number = pte_pfn;
            WriteToPTE(curr_pte, local_contents);

            curr_page->pte = curr_pte;

            if (curr_pte->memory_format.frame_number == 0) {
                DebugBreak();
            }


            *arbitrary_va = (ULONG_PTR) arbitrary_va;
            

            LeaveCriticalSection(&standby_list.list_lock);
            //LeaveCriticalSection(pte_lock);
            UnlockPagetable(pte_region_index_for_lock);
            return TRUE;
        }
    }

    // PTE was never accessed before:
    // get page from freelist or cannabalize
    // from standby

    else {
        BOOL on_standby;

        EnterCriticalSection(&freelist.list_lock);

        curr_page = popHeadPage(&freelist);


        if (curr_page == NULL) {
            on_standby = TRUE;

            LeaveCriticalSection(&freelist.list_lock);

            EnterCriticalSection(&standby_list.list_lock);

            curr_page = popHeadPage(&standby_list);

            // Couldn't get from standby either
            if (curr_page == NULL) {
                LeaveCriticalSection(&standby_list.list_lock);
                //LeaveCriticalSection(pte_lock);
                UnlockPagetable(pte_region_index_for_lock);
                return FALSE;
            }

            PTE* old_pte = curr_page->pte;

            // DM: When multiple faulting threads running,
            // must lock the PTE region to curr_page as well

            // Getting breaks here as well!!
            if (old_pte->memory_format.valid) {
                DebugBreak();
            }

            if (old_pte->transition_format.frame_number != page_to_pfn(curr_page)) {
                DebugBreak();
            }

            // DM: When we get to this point, we have the curr
            // page, but we do not have the PTE lock for it. So,
            // we must enter the PTE lock for that page's old PTE,
            // 

            BOOL unmap = unmap_va(pte_to_va(old_pte, pgtb));
            if (unmap == FALSE) {
                DebugBreak();
                printf ("full_virtual_memory_test : could not unmap VA %p\n", arbitrary_va);

                //LeaveCriticalSection(pte_lock);
                UnlockPagetable(pte_region_index_for_lock);
                return FALSE;
            }


            // DM: ABBA lock problem. This is just a band-aid

            PTE local_contents;
            local_contents = *old_pte;

            local_contents.transition_format.in_memory = 0;
            local_contents.transition_format.valid = 0;
            // This can't be here once we have modified!
            local_contents.transition_format.frame_number = 0;

            //WriteToPTE(old_pte, local_contents);

            *old_pte = local_contents;

            
        }
        
        popped_pfn = page_to_pfn(curr_page);

        BOOL mapped = map_to_pfn(arbitrary_va, popped_pfn);
        if (mapped == FALSE) {
            DebugBreak();
            printf("Could not map\n");
            //LeaveCriticalSection(pte_lock);
            UnlockPagetable(pte_region_index_for_lock);
            return FALSE;
        }

        // curr_pte->memory_format.age = 0;
        // curr_pte->memory_format.valid = 1;
        // curr_pte->memory_format.frame_number = popped_pfn;
        // curr_page->pte = curr_pte;

        PTE local_contents;
        local_contents = *curr_pte;

        local_contents.memory_format.age = 0;
        local_contents.memory_format.valid = 1;
        local_contents.memory_format.frame_number = popped_pfn;
        WriteToPTE(curr_pte, local_contents);

        curr_page->pte = curr_pte;

        if (curr_pte->memory_format.frame_number == 0) {
            DebugBreak();
        }


        if (on_standby == TRUE) {
            LeaveCriticalSection(&standby_list.list_lock);
        }
        else {
            LeaveCriticalSection(&freelist.list_lock);
        }

        //LeaveCriticalSection(pte_lock);
        UnlockPagetable(pte_region_index_for_lock);
        return TRUE;
    }

    return FALSE;
}

BOOL map_to_pfn(PULONG_PTR arbitrary_va, ULONG64 pfn) {
    if (MapUserPhysicalPages (arbitrary_va, 1, &pfn) == FALSE) {

        DebugBreak();
        printf ("pagefault : could not map VA %p to page %llX\n", arbitrary_va, pfn);
        return FALSE;

    }

    return TRUE;
}

BOOL unmap_va(PULONG_PTR arbitrary_va) {
    if (MapUserPhysicalPages (arbitrary_va, 1, NULL) == FALSE) {

        DebugBreak();
        printf ("pagefault : could not map VA %p\n", arbitrary_va);
        return FALSE;

    }

    return TRUE;
}

VOID make_active(PTE* given_pte, ULONG64 pfn) {
    given_pte->entire_format = 1;
    given_pte->memory_format.valid = 1;
    given_pte->memory_format.frame_number = pfn;

}