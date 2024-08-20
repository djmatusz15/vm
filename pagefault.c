#include "pagefault.h"


// DM: Careful in holding the PTE region lock the whole time

BOOL pagefault(PULONG_PTR arbitrary_va, FLUSHER* flusher, PULONG_PTR* batch_vas_for_readins, ULONG64* batch_pfns_for_readins, LPVOID temp_va_for_new_pte) {

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


        // checkPrivilegesPTE(curr_pte);

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

        handled_fault = handle_new_pte(curr_pte, pte_region_index_for_lock, temp_va_for_new_pte);

    }


    // On disk. Get new page from standby or freelist,
    // write content from disk to the frame, then map it 
    // to the PTE
    else {

        handled_fault = handle_on_disk(curr_pte, pte_region_index_for_lock, flusher, batch_vas_for_readins, batch_pfns_for_readins);

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

        printf ("pagefault : could not map VA %p to page %llX\n", arbitrary_va, pfn);
        DebugBreak();
        return FALSE;

    }

    return TRUE;
}

BOOL unmap_va(PULONG_PTR arbitrary_va) {
    if (MapUserPhysicalPages (arbitrary_va, 1, NULL) == FALSE) {

        printf ("pagefault : could not map VA %p\n", arbitrary_va);
        DebugBreak();
        return FALSE;

    }

    return TRUE;
}


// DM: Building this function to check active PTEs
// and see if their privileges have been changed.

// VOID checkPrivileges(PTE* curr_pte) {

//     page_t* page_of_curr_pte = pfn_to_page(curr_pte->memory_format.frame_number, pgtb);

//     if (page_of_curr_pte->readwrite != curr_pte->memory_format.privileges) {

//     }
// }





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
        printf("Valid format pte being rescued\n");
        DebugBreak();
    }

    if (curr_pte->transition_format.in_memory == 0) {
        printf("Rescued page no longer in memory\n");
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

        // Check pre-emptively to see what we maybe
        // should get
        if (curr_page->pagefile_num == 0) {
            if (curr_page->in_flight == 0) {
                listhead = &modified_list;
            }
            else {
                listhead = curr_page;
            }
        }
        else {
            listhead = &standby_list;
        }



        if (listhead != curr_page) {
            EnterCriticalSection(&listhead->list_lock);
        }



        if (curr_page->pagefile_num == 0) {
            if (curr_page->in_flight == 0) {
                if (listhead == &modified_list) {
                    break;
                }

            }
            // else {
            //     if (listhead == curr_page) {
            //         break;
            //     }
            // }
            
        }

        else {
            if (listhead == &standby_list) {
                break;
            }
        }

        if (listhead != curr_page) {
            LeaveCriticalSection(&listhead->list_lock);
        }

        continue;
    }

    if (listhead != curr_page) {
        popFromAnywhere(listhead, curr_page);
    }

    if (MapUserPhysicalPages (arbitrary_va, 1, &pte_pfn) == FALSE) {

        printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", arbitrary_va, pte_pfn);
        DebugBreak();
    }


    if (listhead == curr_page) {
        curr_page->was_rescued = 1;
    }
    
    curr_page->pte = curr_pte;

    if (listhead != curr_page) {
        LeaveCriticalSection(&listhead->list_lock);
    }


    // Need synchronization with some form of lock,
    // like pagefile lock or IncrementExchange

    if (listhead != &modified_list) {

        EnterCriticalSection(&pf.pf_lock);

        pf.pagefile_state[curr_page->pagefile_num] = 0;
        curr_page->pagefile_num = 0;

        pf.free_pagefile_blocks++;

        LeaveCriticalSection(&pf.pf_lock);

        // Need to set event here saying pagefile
        // state at this spot is now free!

        SetEvent(pagefile_blocks_available);
    }



    PTE local_contents;
    local_contents.entire_format = 0;

    local_contents.memory_format.age = 0;
    local_contents.memory_format.valid = 1;
    local_contents.memory_format.frame_number = page_to_pfn(curr_page);
    WriteToPTE(curr_pte, local_contents);

    rescues++;
}







#if SUPPORT_MULTIPLE_FREELISTS

// PTE was never accessed before:
// get page from zerolist, freelist (and zero out) 
// or cannabalize from standby. Repurpose the 
// page so this PTE is active and can use it

BOOL handle_new_pte(PTE* curr_pte, ULONG64 pte_region_index_for_lock, LPVOID temp_va_for_new_pte) {

    PTE_LOCK* pte_lock;
    ULONG64 pte_pfn;
    ULONG64 popped_pfn;
    page_t* curr_page;

    int freelist_lock;
    int zerolist_lock;
    BOOL on_standby = FALSE;
    PULONG_PTR arbitrary_va = pte_to_va(curr_pte, pgtb);


    zerolist_lock = acquireRandomZerolistLock();   

    if (zerolist_lock != -1) {

        if ((DWORD)zero_list.freelists[zerolist_lock].list_lock.OwningThread != GetCurrentThreadId()) {
            printf("Mismatching zerolist owning threads\n");
            DebugBreak();
        }

        if (zero_list.freelists[zerolist_lock].num_of_pages <= 0) {
            printf("Still got empty zerolist\n");
            DebugBreak();
        }

        // Popping Tail so to not potentially pop listhead accidentally
        curr_page = popTailPage(&zero_list.freelists[zerolist_lock]);

        if (curr_page == NULL || curr_page == &zero_list.freelists[zerolist_lock] || curr_page->num_of_pages != 0) {
            printf("Null page from zerolist\n");
            DebugBreak();
        }

        LeaveCriticalSection(&zero_list.freelists[zerolist_lock].list_lock);

    }

    else if (zerolist_lock == -1) {
        freelist_lock = acquireRandomFreelistLock();

        if (freelist_lock != -1) {

            if ((DWORD)freelist.freelists[freelist_lock].list_lock.OwningThread != GetCurrentThreadId()) {
                printf("Mismatching freelist owning threads\n");
                DebugBreak();
            }

            if (freelist.freelists[freelist_lock].num_of_pages <= 0) {
                printf("Still got empty freelist\n");
                DebugBreak();
            }

            curr_page = popTailPage(&freelist.freelists[freelist_lock]);

            if (curr_page == NULL || curr_page == &freelist.freelists[freelist_lock] || curr_page->num_of_pages != 0) {
                printf("Null page from freelist\n");
                DebugBreak();
            }

            LeaveCriticalSection(&freelist.freelists[freelist_lock].list_lock);

        }
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

        LeaveCriticalSection(&standby_list.list_lock);
        
    }


    if (curr_page->pte != NULL) {

        if (zerolist_lock == -1) {

            BOOL mapped_temp_va = map_to_pfn(temp_va_for_new_pte, page_to_pfn(curr_page));
            if (mapped_temp_va == FALSE) {
                printf("Couldn't map page to temp VA for zeroing\n");
                DebugBreak();
            }

            memset(temp_va_for_new_pte, 0, PAGE_SIZE);

            BOOL unmaped_temp_va = unmap_va(temp_va_for_new_pte);
            if (unmap_va == FALSE) {
                printf("Failed at unmapping temp VA for zeroing\n");
                DebugBreak();
            }

        }
    }
    
    popped_pfn = page_to_pfn(curr_page);

    BOOL mapped = map_to_pfn(arbitrary_va, popped_pfn);
    if (mapped == FALSE) {
        printf("Could not map (handle_new_pte)\n");
        DebugBreak();
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
            printf("Broken freelist lock acquisition\n");
            DebugBreak();
       }

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

        LeaveCriticalSection(&standby_list.list_lock);
    }
    else {
        LeaveCriticalSection(&freelist.list_lock);
    }


    new_ptes++;

    return TRUE;
}


#endif




BOOL handle_on_disk(PTE* curr_pte, ULONG64 pte_region_index_for_lock, FLUSHER* flusher, PULONG_PTR* batch_vas_for_readins, ULONG64* batch_pfns_for_readins) {
    page_t* repurposed_page;
    BOOL on_standby;
    ULONG64 curr_pte_pagefile_num = curr_pte->disc_format.pagefile_num;
    PULONG_PTR arbitrary_va = pte_to_va(curr_pte, pgtb);

    #if READ_BATCH_FROM_DISK

    BOOL read_in_batch = read_batch_from_disk(curr_pte, pte_region_index_for_lock, flusher, batch_vas_for_readins, batch_pfns_for_readins);
    if (read_in_batch == FALSE) {
        return FALSE;
    }
    
    return TRUE;




    #else



    EnterCriticalSection(&freelist.list_lock);

    repurposed_page = popHeadPage(&freelist);


    if (repurposed_page == NULL) {
        on_standby = TRUE;

        LeaveCriticalSection(&freelist.list_lock);

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


    ULONG64 conv_page_num = page_to_pfn(repurposed_page);
    if (MapUserPhysicalPages (flusher->temp_vas[flusher->num_of_vas_used], 1, &conv_page_num) == FALSE) {
        
        if (on_standby == TRUE) {
            LeaveCriticalSection(&standby_list.list_lock);
        }
        else {
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
                LeaveCriticalSection(&standby_list.list_lock);
            }
            else {
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
        LeaveCriticalSection(&freelist.list_lock);
    }



    // Make the disk slot free again,
    // and set an event so that the modifying
    // thread knows there's a free slot on
    // the disk open again

    EnterCriticalSection(&modified_list.list_lock);

    pf.pagefile_state[curr_pte_pagefile_num] = 0;
    pf.free_pagefile_blocks++;

    LeaveCriticalSection(&modified_list.list_lock);


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
            printf("Mismatching freelist owning threads\n");
            DebugBreak();
        }
    }
    
    pgtb->pte_regions_locks[attempt_region].owning_thread = curr_thread;


    curr_page = popHeadPage(&standby_list);

    // Couldn't get from standby
    if (curr_page == NULL) {
        printf("No pages available\n");
        UnlockPagetable(attempt_region);
        DebugBreak();
        return NULL;
    }

    PTE* old_pte = curr_page->pte;

    // Remind old PTE where to get its contents from

    PTE local_contents;
    local_contents.entire_format = 0;

    local_contents.disc_format.in_memory = 0;
    local_contents.disc_format.pagefile_num = curr_page->pagefile_num;

    WriteToPTE(old_pte, local_contents);


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


            // DM: Zero out the page to remove any sensitive data
            // memset(page_to_pfn(curr_page), 0, PAGE_SIZE);



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



BOOL read_batch_from_disk(PTE* curr_pte, ULONG64 pte_region_index_for_lock, FLUSHER* flusher, PULONG_PTR* batch_vas_for_readins, ULONG64* batch_pfns_for_readins) {

    #if SUPPORT_MULTIPLE_FREELISTS

    page_t* repurposed_page;
    PULONG_PTR base_va = pte_to_va(curr_pte, pgtb);
    ULONG64 base_pte_index = va_to_pte_index(base_va, pgtb);

    int count_batch_vas = 0;

    for (unsigned i = 0; i < (CONSECUTIVE_ACCESSES/ 8); i++) {


        // int zerolist_lock = -1;


        PTE* attempt_pte = &pgtb->pte_array[base_pte_index + i];
        ULONG64 attempt_pte_region = (base_pte_index + i) / PTES_PER_REGION;

        if (attempt_pte->memory_format.valid == 1 || attempt_pte->transition_format.in_memory == 1 || attempt_pte->entire_format == 0) {
            continue;
        }


        if (TryEnterCriticalSection(&pgtb->pte_regions_locks[attempt_pte_region].lock) == 0) {
            continue;
        }



        DWORD curr_thread = GetCurrentThreadId();
        pgtb->pte_regions_locks[attempt_pte_region].owning_thread = curr_thread;


        BOOL on_standby = FALSE;

        int freelist_lock = acquireRandomFreelistLock();

        if (freelist_lock != -1) {

            repurposed_page = popTailPage(&freelist.freelists[freelist_lock]);

            if (repurposed_page->num_of_pages != 0) {
                printf("Accidentally popped listhead somehow - freelist\n");
                DebugBreak();
            }

            LeaveCriticalSection(&freelist.freelists[freelist_lock].list_lock);

        }


        // else if (freelist_lock == -1) {
        //     zerolist_lock = acquireRandomZerolistLock();

        //     if (zerolist_lock != -1) {

        //         repurposed_page = popTailPage(&zero_list.freelists[zerolist_lock]);

        //         if (repurposed_page->num_of_pages != 0) {
        //             printf("Accidentally popped listhead somehow - zerolist\n");
        //             DebugBreak();
        //         }

        //         LeaveCriticalSection(&zero_list.freelists[zerolist_lock]);
        //     }

        // }

        else if (freelist_lock == -1) {
            on_standby = TRUE;


            EnterCriticalSection(&standby_list.list_lock);

            repurposed_page = recycleOldestPage(pte_region_index_for_lock);
            if (repurposed_page == NULL) {

                LeaveCriticalSection(&standby_list.list_lock);

                // No pages left, maybe trigger the trim event now.
                // When making faulting threads that are waiting, 
                // we want to trigger an event, and release PTE lock
                
                //SetEvent(trim_now);
                
                UnlockPagetable(attempt_pte_region);

                continue;
            }

            // Move pages from standby to freelists to
            // reduce standby list contention

            #if MOVE_PAGES_FROM_STANDBY_TO_FREELIST
            move_pages_from_standby_to_freelist(pte_region_index_for_lock);
            #endif


            LeaveCriticalSection(&standby_list.list_lock);

        }


        PULONG_PTR attempt_va = pte_to_va(attempt_pte, pgtb);

        batch_vas_for_readins[count_batch_vas] = attempt_va;
        batch_pfns_for_readins[count_batch_vas] = page_to_pfn(repurposed_page);

        count_batch_vas++;

    }


        


    if ((count_batch_vas + flusher->num_of_vas_used) >= (NUM_TEMP_VAS)) {

        if (MapUserPhysicalPagesScatter(flusher->temp_vas, flusher->num_of_vas_used, NULL) == 0) {

            for (unsigned j = 0; j < count_batch_vas; j++) {
                ULONG64 pte_region_to_leave = va_to_pte_index(batch_vas_for_readins[j], pgtb) / PTES_PER_REGION;
                LeaveCriticalSection(&pgtb->pte_regions_locks[pte_region_to_leave].lock);

                // Gracefully, would also have to add back all the pages to respective
                // lists, but this should never happen.
            }

            DebugBreak();
            return FALSE;

        }

        flusher->num_of_vas_used = 0;
    }



    if (MapUserPhysicalPagesScatter(&flusher->temp_vas[flusher->num_of_vas_used], count_batch_vas, batch_pfns_for_readins) == 0) {

        for (unsigned j = 0; j < count_batch_vas; j++) {
                ULONG64 pte_region_to_leave = va_to_pte_index(batch_vas_for_readins[j], pgtb) / PTES_PER_REGION;
                LeaveCriticalSection(&pgtb->pte_regions_locks[pte_region_to_leave].lock);

                // Gracefully, would also have to add back all the pages to respective
                // lists, but this should never happen.
            }

        DebugBreak();
        return FALSE;

    }
    

    for (unsigned i = 0; i < count_batch_vas; i++) {

        ULONG64 attempt_pte_index = va_to_pte_index(batch_vas_for_readins[i], pgtb);
        PTE* attempt_pte = &pgtb->pte_array[attempt_pte_index];
        ULONG64 attempt_pte_pagefile_slot = attempt_pte->disc_format.pagefile_num;

        memcpy(flusher->temp_vas[flusher->num_of_vas_used], &pf.pagefile_contents[attempt_pte_pagefile_slot * PAGE_SIZE], PAGE_SIZE);
        flusher->num_of_vas_used++;


        PTE local_contents;
        local_contents.entire_format = 0;

        local_contents.memory_format.age = 0;
        local_contents.memory_format.valid = 1;
        local_contents.memory_format.frame_number = batch_pfns_for_readins[i];

        WriteToPTE(attempt_pte, local_contents);


        page_t* curr_page = pfn_to_page(batch_pfns_for_readins[i], pgtb);
        curr_page->pte = attempt_pte;
        curr_page->pagefile_num = 0;


        EnterCriticalSection(&pf.pf_lock);

        pf.pagefile_state[attempt_pte_pagefile_slot] = 0;
        pf.free_pagefile_blocks++;

        LeaveCriticalSection(&pf.pf_lock);


        //SetEvent(pagefile_blocks_available);
        read_from_disk++;



    }

    if (MapUserPhysicalPagesScatter(batch_vas_for_readins, count_batch_vas, batch_pfns_for_readins) == 0) {

        for (unsigned j = 0; j < count_batch_vas; j++) {
                ULONG64 pte_region_to_leave = va_to_pte_index(batch_vas_for_readins[j], pgtb) / PTES_PER_REGION;
                LeaveCriticalSection(&pgtb->pte_regions_locks[pte_region_to_leave].lock);

                // Gracefully, would also have to add back all the pages to respective
                // lists, but this should never happen.
            }

        DebugBreak();
        return FALSE;

    }


    // I unlock the PTE regions here. Before, when I was
    // doing it all in the loop above, I could unlock a
    // PTE region that belongs to a different PTE that I still
    // need to set in batch_vas_for_readins. Thus, we leave the
    // PTES in the same region free for other threads to alter
    // before writing it properly. 

    for (unsigned i = 0; i < count_batch_vas; i++) {
        ULONG64 attempt_pte_index = va_to_pte_index(batch_vas_for_readins[i], pgtb);
        ULONG64 attempt_pte_index_region = attempt_pte_index / PTES_PER_REGION;
        UnlockPagetable(attempt_pte_index_region);
    }


    // Having this here, instead of setting it every time
    // we open a pagefile slot, has the mod writer spin for 
    // less than half of what it was previously doing.

    SetEvent(pagefile_blocks_available);
    
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

        
        // Map to temporary VAs first

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

        EnterCriticalSection(&modified_list.list_lock);

        pf.pagefile_state[attempt_pte_pagefile_slot] = 0;
        pf.free_pagefile_blocks++;

        LeaveCriticalSection(&modified_list.list_lock);


        SetEvent(pagefile_blocks_available);

        read_from_disk++;

    }

    return TRUE;

    #endif

}