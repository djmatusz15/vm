#include "pagefault.h"

BOOL pagefault(PULONG_PTR arbitrary_va) {

    ULONG64 conv_index = va_to_pte_index(arbitrary_va, pgtb);
    ULONG64 pte_region_index_for_lock = conv_index / PTES_PER_REGION;
    PTE_LOCK* pte_lock;
    ULONG64 pte_pfn;
    ULONG64 popped_pfn;
    page_t* curr_page;
    BOOL handled_fault = TRUE;


    //LockPagetable(pte_region_index_for_lock);
    EnterCriticalSection(&pgtb->lock);

    PTE* curr_pte = &pgtb->pte_array[conv_index];

    if (curr_pte == NULL) {
        DebugBreak();
        printf("Could not get a valid PTE given VA\n");
    }


    // Active by another thread, skip entirely.
    else if (curr_pte->memory_format.valid == 1) {
        //DebugBreak();
        //printf("Already active by another thread, continue on\n");
        // return TRUE;
    }


    // Given page on modified or standby
    // Go rescue it from either of those lists.
    // This just checks if its on modified,
    // we start handling the fault at line 134

    else if (curr_pte->transition_format.in_memory == 1) {

        rescuePTE(curr_pte);

    }


    // HANDLE BRAND NEW PTE
    else if (curr_pte->entire_format == 0) {

        handled_fault = handle_new_pte(curr_pte);

    }


    // On disk. Get new page from standby or freelist,
    // write content from disk to the frame, then map it 
    // to the PTE
    else {

        handled_fault = handle_on_disk(curr_pte);

    }

    //UnlockPagetable(pte_region_index_for_lock);
    LeaveCriticalSection(&pgtb->lock);

    if (handled_fault == FALSE) {
        SetEvent(trim_now);

        WaitForSingleObject(pages_available, INFINITE);
    }

    return TRUE;
    

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


// Convert a PTE from Transition to Valid
VOID rescuePTE(PTE* curr_pte) {
    PTE_LOCK* pte_lock;
    ULONG64 pte_pfn;
    ULONG64 popped_pfn;
    page_t* curr_page;

    PULONG_PTR arbitrary_va = pte_to_va(curr_pte, pgtb);

    // Now, use this to figure out the listhead instead
    pte_pfn = curr_pte->transition_format.frame_number;
    curr_page = pfn_to_page(pte_pfn, pgtb);


    page_t* listhead;

    if (curr_pte->memory_format.valid == 1) {
        DebugBreak();
    }

    if (curr_pte->transition_format.in_memory == 0) {
        DebugBreak();
    }

    
    // We do this because, between popping from 
    // anywhere and checking which listhead we need,
    // we could incidentally grab the wrong list lock,
    // since we are racing with the modified writer.
    // This while loop ensures that, by the time we pop,
    // we know we have settled the race condition and 
    // have the right listhead
    
    while (1) {
        if (curr_page->pagefile_num == 0) {
            listhead = &modified_list;
        }
        else {
            listhead = &standby_list;
        }

            
        EnterCriticalSection(&listhead->list_lock);

        if (curr_page->pagefile_num == 0) {
            if (listhead == &modified_list) {
                break;
            }
        }

        else {
            if (listhead == &standby_list) {
                break;
            }
        }

        LeaveCriticalSection(&listhead->list_lock);
        continue;
    }

    popFromAnywhere(listhead, curr_page);

    if (MapUserPhysicalPages (arbitrary_va, 1, &pte_pfn) == FALSE) {

        DebugBreak();
        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, pte_pfn);
    }

    PTE local_contents;
    local_contents.entire_format = 0;

    local_contents.memory_format.age = 0;
    local_contents.memory_format.valid = 1;
    local_contents.memory_format.frame_number = pte_pfn;
    WriteToPTE(curr_pte, local_contents);

    if (listhead != &modified_list) {
        pagefile_state[curr_page->pagefile_num] = 0;
        curr_page->pagefile_num = 0;
    }

    // DM: Does this need to be in the modified_list
    // critical section?
    curr_page->pte = curr_pte;

    LeaveCriticalSection(&listhead->list_lock);
}


// PTE was never accessed before:
// get page from freelist or cannabalize
// from standby. Repurpose the page so
// this PTE is active and can use it

BOOL handle_new_pte(PTE* curr_pte) {
    PTE_LOCK* pte_lock;
    ULONG64 pte_pfn;
    ULONG64 popped_pfn;
    page_t* curr_page;

    BOOL on_standby = FALSE;
    PULONG_PTR arbitrary_va = pte_to_va(curr_pte, pgtb);

    EnterCriticalSection(&freelist.list_lock);

    curr_page = popHeadPage(&freelist);


    if (curr_page == NULL) {
        on_standby = TRUE;

        LeaveCriticalSection(&freelist.list_lock);

        EnterCriticalSection(&standby_list.list_lock);

        curr_page = recycleOldestPage();
        if (curr_page == NULL) {
            //printf("Couldn't get oldest page(handle_new_pte)\n");
            LeaveCriticalSection(&standby_list.list_lock);


            // No pages left, maybe trigger the trim event now.
            // When making faulting threads that are waiting, 
            // we want to trigger an event, and release PTE lock

            //SetEvent(trim_now);
            return FALSE;
        }
        
    }

    if (curr_page->pagefile_num != 0) {
        DebugBreak();
    }
    
    popped_pfn = page_to_pfn(curr_page);

    BOOL mapped = map_to_pfn(arbitrary_va, popped_pfn);
    if (mapped == FALSE) {
        DebugBreak();
        printf("Could not map (handle_new_pte)\n");
    }

    PTE local_contents;
    local_contents.entire_format = 0;

    local_contents.memory_format.age = 0;
    local_contents.memory_format.valid = 1;
    local_contents.memory_format.frame_number = popped_pfn;
    WriteToPTE(curr_pte, local_contents);

    curr_page->pte = curr_pte;

    if (on_standby == TRUE) {
        LeaveCriticalSection(&standby_list.list_lock);
    }
    else {
        LeaveCriticalSection(&freelist.list_lock);
    }

    return TRUE;
}

BOOL handle_on_disk(PTE* curr_pte) {
    page_t* repurposed_page;
    BOOL on_standby;
    ULONG64 curr_pte_pagefile_num = curr_pte->disc_format.pagefile_num;

    PULONG_PTR arbitrary_va = pte_to_va(curr_pte, pgtb);

    EnterCriticalSection(&freelist.list_lock);

    repurposed_page = popHeadPage(&freelist);

    if (repurposed_page == NULL) {
        on_standby = TRUE;

        LeaveCriticalSection(&freelist.list_lock);

        EnterCriticalSection(&standby_list.list_lock);

        repurposed_page = recycleOldestPage();
        if (repurposed_page == NULL) {
            //printf("Couldn't get oldest page\n");
            LeaveCriticalSection(&standby_list.list_lock);

            // No pages left, maybe trigger the trim event now.
            // When making faulting threads that are waiting, 
            // we want to trigger an event, and release PTE lock
            // SetEvent(trim_now)
            return FALSE;
        }

    }


    ULONG64 conv_page_num = page_to_pfn(repurposed_page);
    if (MapUserPhysicalPages (modified_page_va2, 1, &conv_page_num) == FALSE) {
        
        if (on_standby == TRUE) {
            LeaveCriticalSection(&standby_list.list_lock);
        }
        else {
            LeaveCriticalSection(&freelist.list_lock);
        }

        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_page_va2, conv_page_num);
        DebugBreak();

    }

    memcpy(modified_page_va2, &pagefile_contents[curr_pte_pagefile_num * PAGE_SIZE], PAGE_SIZE);


    if (MapUserPhysicalPages (modified_page_va2, 1, NULL) == FALSE) {

        if (on_standby == TRUE) {
            LeaveCriticalSection(&standby_list.list_lock);
        }
        else {
            LeaveCriticalSection(&freelist.list_lock);
        }

        printf ("full_virtual_memory_test : could not unmap VA %p\n", modified_page_va2);
        DebugBreak();

    }

    PTE local_contents;
    local_contents.entire_format = 0;

    local_contents.memory_format.age = 0;
    local_contents.memory_format.valid = 1;
    local_contents.memory_format.frame_number = page_to_pfn(repurposed_page);
    WriteToPTE(curr_pte, local_contents);

    //*curr_pte = local_contents;

    if (repurposed_page->pagefile_num != 0) {
        DebugBreak();
    }

    repurposed_page->pte = curr_pte;

    ULONG64 new_mapping = local_contents.memory_format.frame_number;


    if (MapUserPhysicalPages (arbitrary_va, 1, &new_mapping) == FALSE) {
        
        if (on_standby == TRUE) {
            LeaveCriticalSection(&standby_list.list_lock);
        }
        else {
            LeaveCriticalSection(&freelist.list_lock);
        }

        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, new_mapping);
        DebugBreak();

    }


    if (on_standby == TRUE) {
        LeaveCriticalSection(&standby_list.list_lock);
    }
    else {
        LeaveCriticalSection(&freelist.list_lock);
    }

    // Make the disk slot free again,
    // and set an event so that the modifying
    // thread knows there's a free slot on
    // the disk open again

    EnterCriticalSection(&modified_list.list_lock);
    pagefile_state[curr_pte_pagefile_num] = 0;
    LeaveCriticalSection(&modified_list.list_lock);

    SetEvent(pagefile_blocks_available);
    
    return TRUE;

}


// Caller already holds standby list lock
page_t* recycleOldestPage() {
    page_t* curr_page = popHeadPage(&standby_list);

    // Couldn't get from standby either
    if (curr_page == NULL) {
        return NULL;
    }

    PTE* old_pte = curr_page->pte;

    // DM: When multiple faulting threads running,
    // must lock the PTE region to curr_page as well

    // DM: When we get to this point, we have the curr
    // page, but we do not have the PTE lock for it. So,
    // we must enter the PTE lock for that page's old PTE,
    // 

    BOOL unmap = unmap_va(pte_to_va(old_pte, pgtb));
    if (unmap == FALSE) {
        DebugBreak();
        //printf ("full_virtual_memory_test : could not unmap VA (handle_new_pte)\n");
    }


    // DM: ABBA lock problem. This is just a band-aid

    PTE local_contents;
    local_contents.entire_format = 0;

    local_contents.disc_format.in_memory = 0;

    // Remind old PTE where to get its contents from
    local_contents.disc_format.pagefile_num = curr_page->pagefile_num;

    //WriteToPTE(old_pte, local_contents);

    *old_pte = local_contents;

    curr_page->pagefile_num = 0;

    return curr_page;
}