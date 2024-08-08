#include "pagefault.h"


// DM: Careful in holding the PTE region lock the whole time

BOOL pagefault(PULONG_PTR arbitrary_va, FLUSHER* flusher) {

    ULONG64 conv_index = va_to_pte_index(arbitrary_va, pgtb);
    ULONG64 pte_region_index_for_lock = conv_index / PTES_PER_REGION;
    PTE_LOCK* pte_lock;
    ULONG64 pte_pfn;
    ULONG64 popped_pfn;
    page_t* curr_page;
    BOOL handled_fault = TRUE;



    LockPagetable(pte_region_index_for_lock);

    PTE* curr_pte = &pgtb->pte_array[conv_index];

    if (curr_pte == NULL) {
        DebugBreak();
        printf("Could not get a valid PTE given VA\n");
    }


    // Active by another thread, or set to valid previously
    else if (curr_pte->memory_format.valid == 1) {

        // DM: Here, we have to check if the valid bit was set
        // because we pagefaulted on THIS PTE and mapped the VA
        // to a physical address, or because we set the valid bit 
        // since it was a neighbor of a VA we had pagefaulted on.

        // PULONG_PTR curr_pte_va = pte_to_va(curr_pte, pgtb);

        // // If this is true, then we know we have accessed one of those
        // // neighbor PTEs, since it's valid but never mapped the VA to
        // // a PA. So, that's all we have to do here.
        // if (curr_pte_va == NULL) {
        //     ULONG64 neighbor_pte_pfn = curr_pte->memory_format.frame_number;
        //     if (MapUserPhysicalPages(arbitrary_va, 1, &neighbor_pte_pfn) == FALSE) {
        //         DebugBreak();
        //         printf("Could not map neighbor VA to its PA\n");
        //         return FALSE;
        //     }
        // }

        ran_into_active_ptes++;
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

        handled_fault = handle_new_pte(curr_pte, pte_region_index_for_lock);

    }


    // On disk. Get new page from standby or freelist,
    // write content from disk to the frame, then map it 
    // to the PTE
    else {

        handled_fault = handle_on_disk(curr_pte, pte_region_index_for_lock, flusher);

    }

    

    UnlockPagetable(pte_region_index_for_lock);

    if (handled_fault == FALSE) {
        SetEvent(trim_now);

        // DM: Change back to infinite
        // WaitForSingleObject(pages_available, 0);
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

    curr_page->pte = curr_pte;

    LeaveCriticalSection(&listhead->list_lock);


    // Need synchronization with some form of lock,
    // like pagefile lock of IncrementExchange

    if (listhead != &modified_list) {

        EnterCriticalSection(&modified_list.list_lock);

        pf.pagefile_state[curr_page->pagefile_num] = 0;
        curr_page->pagefile_num = 0;

        pf.free_pagefile_blocks++;

        LeaveCriticalSection(&modified_list.list_lock);

        // Need to set event here saying pagefile
        // state at this spot is now free!

        SetEvent(pagefile_blocks_available);
    }

    rescues++;
}



#if SUPPORT_MULTIPLE_FREELISTS

// PTE was never accessed before:
// get page from freelist or cannabalize
// from standby. Repurpose the page so
// this PTE is active and can use it

BOOL handle_new_pte(PTE* curr_pte, ULONG64 pte_region_index_for_lock) {

    PTE_LOCK* pte_lock;
    ULONG64 pte_pfn;
    ULONG64 popped_pfn;
    page_t* curr_page;

    int freelist_lock;
    BOOL on_standby = FALSE;
    PULONG_PTR arbitrary_va = pte_to_va(curr_pte, pgtb);



    freelist_lock = acquireRandomFreelistLock();

    if (freelist_lock != -1) {

        if ((DWORD)freelist.freelists[freelist_lock].list_lock.OwningThread != GetCurrentThreadId()) {
            DebugBreak();
        }

        if (freelist.freelists[freelist_lock].num_of_pages <= 0) {
            DebugBreak();
        }

        curr_page = popHeadPage(&freelist.freelists[freelist_lock]);

        if (curr_page == NULL) {
            DebugBreak();
        }

        LeaveCriticalSection(&freelist.freelists[freelist_lock].list_lock);

    }


    if (freelist_lock == -1) {
        on_standby = TRUE;

        EnterCriticalSection(&standby_list.list_lock);

        curr_page = recycleOldestPage(pte_region_index_for_lock);

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
    
    popped_pfn = page_to_pfn(curr_page);

    BOOL mapped = map_to_pfn(arbitrary_va, popped_pfn);
    if (mapped == FALSE) {
        DebugBreak();
        printf("Could not map (handle_new_pte)\n");
    }

    // DM: MUST KEEP TRACK OF THIS!
    // recycleOldestPage no longer does this

    curr_page->pagefile_num = 0;

    PTE local_contents;
    local_contents.entire_format = 0;

    local_contents.memory_format.age = 0;
    local_contents.memory_format.valid = 1;
    local_contents.memory_format.frame_number = popped_pfn;
    WriteToPTE(curr_pte, local_contents);

    curr_page->pte = curr_pte;

    if (on_standby == TRUE) {

       if (freelist_lock != -1) {
            DebugBreak();
       }


        LeaveCriticalSection(&standby_list.list_lock);
    }


    new_ptes++;

    return TRUE;
}

#else

// PTE was never accessed before:
// get page from freelist or cannabalize
// from standby. Repurpose the page so
// this PTE is active and can use it

BOOL handle_new_pte(PTE* curr_pte, ULONG64 pte_region_index_for_lock) {
    PTE_LOCK* pte_lock;
    ULONG64 pte_pfn;
    ULONG64 popped_pfn;
    page_t* curr_page;

    BOOL on_standby = FALSE;
    PULONG_PTR arbitrary_va = pte_to_va(curr_pte, pgtb);


    //acquireLock(&freelist.bitlock);
    EnterCriticalSection(&freelist.list_lock);

    curr_page = popHeadPage(&freelist);


    if (curr_page == NULL) {
        on_standby = TRUE;

        //releaseLock(&freelist.bitlock);
        LeaveCriticalSection(&freelist.list_lock);

        //acquireLock(&standby_list.bitlock);
        EnterCriticalSection(&standby_list.list_lock);

        curr_page = recycleOldestPage(pte_region_index_for_lock);
        if (curr_page == NULL) {
            //printf("Couldn't get oldest page(handle_new_pte)\n");

            //releaseLock(&standby_list.bitlock);
            LeaveCriticalSection(&standby_list.list_lock);


            // No pages left, maybe trigger the trim event now.
            // When making faulting threads that are waiting, 
            // we want to trigger an event, and release PTE lock

            //SetEvent(trim_now);
            return FALSE;
        }
        
    }

    // DM: IF CHANGE DOESN'T WORK, REIMPLEMENT THIS
    // if (curr_page->pagefile_num != 0) {
    //     DebugBreak();
    // }
    
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

    // DM: REMOVE THIS IF CHANGE DOESN'T WORK
    curr_page->pagefile_num = 0;

    if (on_standby == TRUE) {

        /*
            DM: Since we already have the standby list
            lock, we should try and zero out pages and 
            put them back on the freelist. This will
            reduce our contention on this lock, and 
            spread it out towards the freelist
        */

        // if ((DWORD)freelist.list_lock.OwningThread == GetCurrentThreadId()) {
        //     DebugBreak();
        // }

        //releaseLock(&standby_list.bitlock);
        LeaveCriticalSection(&standby_list.list_lock);
    }
    else {
        //releaseLock(&freelist.bitlock);
        LeaveCriticalSection(&freelist.list_lock);
    }


    new_ptes++;

    return TRUE;
}


#endif




BOOL handle_on_disk(PTE* curr_pte, ULONG64 pte_region_index_for_lock, FLUSHER* flusher) {
    page_t* repurposed_page;
    BOOL on_standby;
    ULONG64 curr_pte_pagefile_num = curr_pte->disc_format.pagefile_num;
    PULONG_PTR arbitrary_va = pte_to_va(curr_pte, pgtb);

    #if READ_BATCH_FROM_DISK

    BOOL read_in_batch = read_batch_from_disk(curr_pte, pte_region_index_for_lock, flusher);
    if (read_in_batch == FALSE) {
        return FALSE;
    }
    
    return TRUE;




    #else



    //acquireLock(&freelist.bitlock);
    EnterCriticalSection(&freelist.list_lock);

    repurposed_page = popHeadPage(&freelist);


    if (repurposed_page == NULL) {
        on_standby = TRUE;

        //releaseLock(&freelist.bitlock);
        LeaveCriticalSection(&freelist.list_lock);

        //acquireLock(&standby_list.bitlock);
        EnterCriticalSection(&standby_list.list_lock);

        repurposed_page = recycleOldestPage(pte_region_index_for_lock);
        if (repurposed_page == NULL) {

            //releaseLock(&standby_list.bitlock);
            LeaveCriticalSection(&standby_list.list_lock);

            // No pages left, maybe trigger the trim event now.
            // When making faulting threads that are waiting, 
            // we want to trigger an event, and release PTE lock
            
            //SetEvent(trim_now);
            return FALSE;
        }

    }


    ULONG64 conv_page_num = page_to_pfn(repurposed_page);
    if (MapUserPhysicalPages (flusher->temp_vas[flusher->num_of_vas_used], 1, &conv_page_num) == FALSE) {
        
        if (on_standby == TRUE) {
            //releaseLock(&standby_list.bitlock);
            LeaveCriticalSection(&standby_list.list_lock);
        }
        else {
            //releaseLock(&freelist.bitlock);
            LeaveCriticalSection(&freelist.list_lock);
        }

        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", flusher->temp_vas[flusher->num_of_vas_used], conv_page_num);
        DebugBreak();

    }

    memcpy(flusher->temp_vas[flusher->num_of_vas_used], &pf.pagefile_contents[curr_pte_pagefile_num * PAGE_SIZE], PAGE_SIZE);

    flusher->num_of_vas_used++;

    if (flusher->num_of_vas_used == NUM_TEMP_VAS) {

        if (MapUserPhysicalPagesScatter(flusher->temp_vas, NUM_TEMP_VAS, NULL) == FALSE) {

            if (on_standby == TRUE) {
                //releaseLock(&standby_list.bitlock);
                LeaveCriticalSection(&standby_list.list_lock);
            }
            else {
                //releaseLock(&freelist.bitlock);
                LeaveCriticalSection(&freelist.list_lock);
            }

            printf ("full_virtual_memory_test : could not unmap VA %p\n", flusher->temp_vas);
            DebugBreak();

        }

        flusher->num_of_vas_used = 0;

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
            //releaseLock(&standby_list.bitlock);
            LeaveCriticalSection(&standby_list.list_lock);
        }
        else {
            //releaseLock(&freelist.bitlock);
            LeaveCriticalSection(&freelist.list_lock);
        }

        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, new_mapping);
        DebugBreak();

    }


    if (on_standby == TRUE) {

        // DM: While I have the standby list lock,
        // I should free up some pages and move them
        // to the freelist/zero list. This way, 
        // contention will be relieved on the standby
        // list lock. Should just pop a few of the oldest
        // pages from the tail of standby, zero them out
        // with memset(0) and move them to zero list


        move_pages_from_standby_to_freelist(pte_region_index_for_lock);

        //releaseLock(&standby_list.bitlock);
        LeaveCriticalSection(&standby_list.list_lock);

    }

    else {
        //releaseLock(&freelist.bitlock);
        LeaveCriticalSection(&freelist.list_lock);
    }



    // Make the disk slot free again,
    // and set an event so that the modifying
    // thread knows there's a free slot on
    // the disk open again

    acquireLock(&modified_list.bitlock);
    // EnterCriticalSection(&modified_list.list_lock);

    pf.pagefile_state[curr_pte_pagefile_num] = 0;
    pf.free_pagefile_blocks++;

    releaseLock(&modified_list.bitlock);
    //LeaveCriticalSection(&modified_list.list_lock);


    SetEvent(pagefile_blocks_available);


    read_from_disk++;
    
    return TRUE;

    #endif

}



// Caller already holds standby list lock
page_t* recycleOldestPage(ULONG64 pte_region_index_for_lock) {

    page_t* curr_page;

    // Peak into the standby list, and see if we can actually
    // access the PTE region of the standby list's flink's PTE.
    // If we can, continue as normal. Else, return NULL.
    
    // In handle_new_pte and handle_on_disk, when we see 
    // recycleOldestPage() return NULL, we release both
    // the standby list lock and the current regions lock,
    // freeing it for any other faulting pages that were deadlocked


    PTE* attempt_pte = standby_list.flink->pte;

    if (attempt_pte == NULL) {
        SetEvent(trim_now);
        return NULL;
    }

    PULONG_PTR attempt_va = pte_to_va(attempt_pte, pgtb);
    ULONG64 attempt_index = va_to_pte_index(attempt_va, pgtb);
    ULONG64 attempt_region = attempt_index / PTES_PER_REGION;


    // Either owned by someone else OR we own it already!
    // DM: We have a problem if we already own the lock,
    // i.e. attempt_region == pte_region_index_for_lock

    if (TryEnterCriticalSection(&pgtb->pte_regions_locks[attempt_region].lock) == 0) {
        //printf("Region lock owned by someone else, surrender\n");
        return NULL;
    }

    DWORD curr_thread = GetCurrentThreadId();

    if (pgtb->pte_regions_locks[attempt_region].owning_thread != 0) {
        if (pgtb->pte_regions_locks[attempt_region].owning_thread != curr_thread) {
            DebugBreak();
        }
    }
    
    pgtb->pte_regions_locks[attempt_region].owning_thread = curr_thread;


    curr_page = popHeadPage(&standby_list);

    // Couldn't get from standby either
    if (curr_page == NULL) {
        UnlockPagetable(attempt_region);
        DebugBreak();
        return NULL;
    }

    PTE* old_pte = curr_page->pte;

    BOOL unmap = unmap_va(pte_to_va(old_pte, pgtb));
    if (unmap == FALSE) {
        DebugBreak();
        //printf ("full_virtual_memory_test : could not unmap VA (handle_new_pte)\n");
    }


    PTE local_contents;
    local_contents.entire_format = 0;

    local_contents.disc_format.in_memory = 0;
    // Remind old PTE where to get its contents from
    local_contents.disc_format.pagefile_num = curr_page->pagefile_num;

    WriteToPTE(old_pte, local_contents);

    // *old_pte = local_contents;

    #if READ_BATCH_FROM_DISK == 0
    curr_page->pagefile_num = 0;
    #endif

    UnlockPagetable(attempt_region);

    // DM: could there be a race condition here,
    // where once I unlock this region, someone else
    // could access this PTE and edit it, since
    // we have popped it off the standby list?
    // Thus, even though we still have the standby list lock,
    // this PTE will be naked?

    return curr_page;
}


void move_pages_from_standby_to_freelist(ULONG64 pte_region_index_for_lock) {


    #if SUPPORT_MULTIPLE_FREELISTS


    unsigned int num_pages_on_standby = standby_list.num_of_pages;

    // If num_pages_on_standby is greater than a certain threshold,
    // Move a third of the pages on standby to freelist

    if (num_pages_on_standby >= (NUMBER_OF_PHYSICAL_PAGES * .25)) {


        unsigned int num_to_move_to_freelist = num_pages_on_standby / 128;

    
        for (unsigned i = 0; i < num_to_move_to_freelist; i++) {

            // DM: Here, recycleOldestPage tries to enter the critical section
            // each time. Can optimize this by entering the section once


            page_t* curr_page = recycleOldestPage(pte_region_index_for_lock);

            if (curr_page == NULL) {
                break;
            }


            // DM: This should fix repetition of VAs. Next,
            // batch these all into one flush like with the temp VAs.

            PULONG_PTR va_of_curr_pages_pte = pte_to_va(curr_page->pte, pgtb);


            if (MapUserPhysicalPages(va_of_curr_pages_pte, 1, NULL) == FALSE) {
                LeaveCriticalSection(&standby_list.list_lock);
                DebugBreak();
            }


            // Need to tell this PTE that it's page is no
            // longer available for rescue, meaning it is 
            // invalid.

            //ULONG64 curr_pte_index = va_to_pte_index(va_of_curr_pages_pte, pgtb);
            //ULONG64 curr_pte_index_region = curr_pte_index / PTES_PER_REGION;

            //LockPagetable(curr_pte_index_region);

            PTE local_contents;
            local_contents.entire_format = 0;

            local_contents.disc_format.in_memory = 0;
            local_contents.disc_format.pagefile_num = curr_page->pagefile_num;
            WriteToPTE(curr_page->pte, local_contents);

            //UnlockPagetable(curr_pte_index_region);


            // DM: Zero out the page to remove any sensitive data
            // memset(page_to_pfn(curr_page), 0, PAGE_SIZE);

            curr_page->pagefile_num = 0;



            // Figure out which freelist to add the page back to,
            // and do it.

            int freelist_to_add_to = page_to_pfn(curr_page) % NUM_FREELISTS;

            EnterCriticalSection(&freelist.freelists[freelist_to_add_to].list_lock);

            addToHead(&freelist.freelists[freelist_to_add_to], curr_page);

            LeaveCriticalSection(&freelist.freelists[freelist_to_add_to].list_lock);

            
        }


    }


    #else

    unsigned int num_pages_on_standby = standby_list.num_of_pages;

    // If num_pages_on_standby is greater than a certain threshold,
    // Move a third of the pages on standby to freelist

    if (num_pages_on_standby >= (NUMBER_OF_PHYSICAL_PAGES * .25)) {

        EnterCriticalSection(&freelist.list_lock);

        unsigned int num_to_move_to_freelist = num_pages_on_standby / 512;

    
        for (unsigned i = 0; i < num_to_move_to_freelist; i++) {

            // DM: Here, recycleOldestPage tries to enter the critical section
            // each time. Can optimize this by entering the section once


            page_t* curr_page = recycleOldestPage(pte_region_index_for_lock);

            if (curr_page == NULL) {
                break;
            }


            if (curr_page->pagefile_num == 0) {
                DebugBreak();
            }

            // DM: Need to figure this out 
            // memset(page_to_pfn(curr_page), 0, PAGE_SIZE);


            // Need to tell this PTE that it's page is no
            // longer available for rescue, meaning it is 
            // invalid.

            PTE local_contents;
            local_contents.entire_format = 0;

            local_contents.disc_format.in_memory = 0;
            local_contents.disc_format.pagefile_num = curr_page->pagefile_num;
            WriteToPTE(curr_page->pte, local_contents);


            // Make sure to secure this; new user 
            // can't be able to find the contents on disk

            curr_page->pagefile_num = 0;

            addToHead(&freelist, curr_page);
            
        }
        
        LeaveCriticalSection(&freelist.list_lock);


    }

    #endif

}



BOOL read_batch_from_disk(PTE* curr_pte, ULONG64 pte_region_index_for_lock, FLUSHER* flusher) {

    #if SUPPORT_MULTIPLE_FREELISTS

    page_t* repurposed_page;
    PULONG_PTR base_va = pte_to_va(curr_pte, pgtb);
    ULONG64 base_pte_index = va_to_pte_index(base_va, pgtb);

    for (unsigned i = 0; i < (CONSECUTIVE_ACCESSES/ 8); i++) {

        BOOL on_standby = FALSE;

        int freelist_lock = acquireRandomFreelistLock();

        if (freelist_lock != -1) {

            repurposed_page = popHeadPage(&freelist.freelists[freelist_lock]);

        }



        if (freelist_lock == -1) {
            on_standby = TRUE;

            LeaveCriticalSection(&freelist.freelists[freelist_lock].list_lock);

            EnterCriticalSection(&standby_list.list_lock);

            repurposed_page = recycleOldestPage(pte_region_index_for_lock);
            if (repurposed_page == NULL) {

                LeaveCriticalSection(&standby_list.list_lock);

                // No pages left, maybe trigger the trim event now.
                // When making faulting threads that are waiting, 
                // we want to trigger an event, and release PTE lock
                
                //SetEvent(trim_now);
                return FALSE;
            }

        }

        PTE* attempt_pte = &pgtb->pte_array[base_pte_index + i];


        // Basically, only read in page if its on disk.
        // Otherwise, just skip over it.

        if (attempt_pte->memory_format.valid == 1 || attempt_pte->transition_format.in_memory == 1 || attempt_pte->entire_format == 0) {
            
            if (on_standby == TRUE) {
                addToHead(&standby_list, repurposed_page);
                LeaveCriticalSection(&standby_list.list_lock);
            }
            else {
                addToHead(&freelist.freelists[freelist_lock], repurposed_page);
                LeaveCriticalSection(&freelist.freelists[freelist_lock].list_lock);
            }

            continue;
        }


        // Now, we can assume we have a PTE that is in disk format. Get
        // where the contents of this PTE from the disk to read in

        ULONG64 attempt_pte_pagefile_slot = attempt_pte->disc_format.pagefile_num;

        PULONG_PTR attempt_va = pte_to_va(attempt_pte, pgtb);

        ULONG64 conv_page_num = page_to_pfn(repurposed_page);

        if (MapUserPhysicalPages (flusher->temp_vas[flusher->num_of_vas_used], 1, &conv_page_num) == FALSE) {
            
            if (on_standby == TRUE) {
                LeaveCriticalSection(&standby_list.list_lock);
            }

            else {
                LeaveCriticalSection(&freelist.freelists[freelist_lock].list_lock);
            }

            printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", flusher->temp_vas[flusher->num_of_vas_used], conv_page_num);
            DebugBreak();
            return FALSE;

        }

        memcpy(flusher->temp_vas[flusher->num_of_vas_used], &pf.pagefile_contents[attempt_pte_pagefile_slot * PAGE_SIZE], PAGE_SIZE);

        flusher->num_of_vas_used++;

        if (flusher->num_of_vas_used == NUM_TEMP_VAS) {

            if (MapUserPhysicalPagesScatter(flusher->temp_vas, NUM_TEMP_VAS, NULL) == FALSE) {

                if (on_standby == TRUE) {
                    LeaveCriticalSection(&standby_list.list_lock);
                }
                else {
                    LeaveCriticalSection(&freelist.freelists[freelist_lock].list_lock);
                }

                printf ("full_virtual_memory_test : could not unmap VA %p\n", flusher->temp_vas);
                DebugBreak();
                return FALSE;

            }

            flusher->num_of_vas_used = 0;

        }


        // ULONG64 new_mapping = local_contents.memory_format.frame_number;


        if (MapUserPhysicalPages (attempt_va, 1, &conv_page_num) == FALSE) {
            
            if (on_standby == TRUE) {
                LeaveCriticalSection(&standby_list.list_lock);
            }
            else {
                LeaveCriticalSection(&freelist.freelists[freelist_lock].list_lock);
            }

            printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", attempt_va, conv_page_num);
            DebugBreak();
            return FALSE;

        }

        // DM: MUST KEEP TRACK OF THIS NOW!
        // recycleOldestPage no longer does this

        repurposed_page->pagefile_num = 0;


        PTE local_contents;
        local_contents.entire_format = 0;

        local_contents.memory_format.age = 0;
        local_contents.memory_format.valid = 1;
        local_contents.memory_format.frame_number = page_to_pfn(repurposed_page);

        // THIS HAS TO BE attempt_pte
        WriteToPTE(attempt_pte, local_contents);

        repurposed_page->pte = curr_pte;



        if (on_standby == TRUE) {

            // DM: While I have the standby list lock,
            // I should free up some pages and move them
            // to the freelist/zero list. This way, 
            // contention will be relieved on the standby
            // list lock. Should just pop a few of the oldest
            // pages from the tail of standby, zero them out
            // with memset(0) and move them to zero list


            #if MOVE_PAGES_FROM_STANDBY_TO_FREELIST

            move_pages_from_standby_to_freelist(pte_region_index_for_lock);

            #endif

            LeaveCriticalSection(&standby_list.list_lock);

        }

        else {
            LeaveCriticalSection(&freelist.freelists[freelist_lock].list_lock);
        }


        // Make the disk slot free again,
        // and set an event so that the modifying
        // thread knows there's a free slot on
        // the disk open again

        EnterCriticalSection(&modified_list.list_lock);

        pf.pagefile_state[attempt_pte_pagefile_slot] = 0;
        pf.free_pagefile_blocks++;

        LeaveCriticalSection(&modified_list.list_lock);


        SetEvent(pagefile_blocks_available);


        read_from_disk++;

    }
    
    return TRUE;


    #else

    page_t* curr_page;
    PULONG_PTR base_va = pte_to_va(curr_pte, pgtb);
    ULONG64 base_pte_index = va_to_pte_index(base_va, pgtb);


    for (unsigned i = 0; i < (CONSECUTIVE_ACCESSES/ 8); i++) {

        BOOL on_standby = FALSE;

        EnterCriticalSection(&freelist.list_lock);

        curr_page = popHeadPage(&freelist);

        if (curr_page == NULL) {

            on_standby = TRUE;

            LeaveCriticalSection(&freelist.list_lock);

            EnterCriticalSection(&standby_list.list_lock);

            curr_page = recycleOldestPage(pte_region_index_for_lock);

            if (curr_page == NULL) {

                LeaveCriticalSection(&standby_list.list_lock);
                return FALSE;
            }

        }


        PTE* attempt_pte = &pgtb->pte_array[base_pte_index + i];


        // Basically, only read in page if its on disk.
        // Otherwise, just skip over it.

        if (attempt_pte->memory_format.valid == 1 || attempt_pte->transition_format.in_memory == 1 || attempt_pte->entire_format == 0) {
            
            if (on_standby == TRUE) {
                addToHead(&standby_list, curr_page);
                LeaveCriticalSection(&standby_list.list_lock);
            }
            else {
                addToHead(&freelist, curr_page);
                LeaveCriticalSection(&freelist.list_lock);
            }

            continue;
        }


        // We know this PTE is on disk. Get the pagefile slot to read into.

        ULONG64 attempt_pte_pagefile_slot = attempt_pte->disc_format.pagefile_num;


        PULONG_PTR attempt_va = pte_to_va(attempt_pte, pgtb);

        
        // Map to temporary VAs, then the hardware

        ULONG64 conv_page_num = page_to_pfn(curr_page);
        if (MapUserPhysicalPages (flusher->temp_vas[flusher->num_of_vas_used], 1, &conv_page_num) == FALSE) {

            printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", flusher->temp_vas[flusher->num_of_vas_used], conv_page_num);
            DebugBreak();
            return FALSE;

        }

        memcpy(flusher->temp_vas[flusher->num_of_vas_used], &pf.pagefile_contents[attempt_pte_pagefile_slot * PAGE_SIZE], PAGE_SIZE);

        flusher->num_of_vas_used++;

        if (flusher->num_of_vas_used == NUM_TEMP_VAS) {

            if (MapUserPhysicalPagesScatter(flusher->temp_vas, NUM_TEMP_VAS, NULL) == FALSE) {

                printf ("full_virtual_memory_test : could not unmap VA %p\n", flusher->temp_vas);
                DebugBreak();
                return FALSE;

            }

            flusher->num_of_vas_used = 0;

        }



        //ULONG64 new_mapping = local_contents.memory_format.frame_number;


        if (MapUserPhysicalPages (attempt_va, 1, &conv_page_num) == FALSE) {

            printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", attempt_va, conv_page_num);
            DebugBreak();
            return FALSE;

        }

        PTE local_contents;
        local_contents.entire_format = 0;

        local_contents.memory_format.age = 0;
        local_contents.memory_format.valid = 1;
        local_contents.memory_format.frame_number = page_to_pfn(curr_page);
        WriteToPTE(attempt_pte, local_contents);


        // DM: REIMPLEMENT IF CHANGE DOESN'T WORK
        // if (curr_page->pagefile_num != 0) {
        //     DebugBreak();
        // }

        curr_page->pte = attempt_pte;


        // DM: KEEP TRACK OF ZEROING PAGEFILE NUM ON PAGE
        // No longer doing it in recycleOldestPage

        curr_page->pagefile_num = 0;


        if (on_standby == TRUE) {

            #if MOVE_PAGES_FROM_STANDBY_TO_FREELIST

            move_pages_from_standby_to_freelist(pte_region_index_for_lock);

            #endif

            LeaveCriticalSection(&standby_list.list_lock);

        }

        else {

            LeaveCriticalSection(&freelist.list_lock);

        }

        // Make the disk slot free again,
        // and set an event so that the modifying
        // thread knows there's a free slot on
        // the disk open again

        //acquireLock(&modified_list.bitlock);
        EnterCriticalSection(&modified_list.list_lock);

        pf.pagefile_state[attempt_pte_pagefile_slot] = 0;
        pf.free_pagefile_blocks++;

        //releaseLock(&modified_list.bitlock);
        LeaveCriticalSection(&modified_list.list_lock);


        SetEvent(pagefile_blocks_available);

        read_from_disk++;

    }

    return TRUE;

    #endif

}