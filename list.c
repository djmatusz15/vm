#define PAGE_SIZE 4096

#include "list.h"

HANDLE trim_now;

#if SUPPORT_MULTIPLE_FREELISTS


// This function instantiates the freelist structures. It
// also creates the page_t structures and allocates all the
// physical frame numbers to them. It then adds these pages
// to the zerolists at the start. 

HANDLE too_few_pages_on_freelists;

void instantiateFreeList(PULONG_PTR physical_frame_numbers, ULONG_PTR num_physical_frames, page_t* base_pfn) {

    freelist.freelists = (page_t*)malloc(sizeof(page_t) * NUM_FREELISTS);

    if (freelist.freelists == NULL) {
        printf("Couldn't malloc memory for freelists\n");
        DebugBreak();
        return;
    }

    // Create event to track the amount of pages
    // on the freelists. If it hits too low of a
    // limit, then we call the moving thread to
    // acquire the standby list lock and move 
    // pages to the freelists

    too_few_pages_on_freelists = CreateEvent(NULL, FALSE, FALSE, NULL);



    // Initialize each freelist

    for (unsigned i = 0; i < NUM_FREELISTS; i++) {

        InitializeCriticalSection(&freelist.freelists[i].list_lock);

        EnterCriticalSection(&freelist.freelists[i].list_lock);

        freelist.freelists[i].blink = &freelist.freelists[i];
        freelist.freelists[i].flink = &freelist.freelists[i];

        freelist.freelists[i].num_of_pages = 0;
        freelist.freelists[i].is_freelist = 1;

        LeaveCriticalSection(&freelist.freelists[i].list_lock);
    }



    // Add physical frame and its page to respective freelist

    for (unsigned i = 0; i < num_physical_frames; i++) {

        page_t* new_page = page_create(base_pfn, physical_frame_numbers[i]);
        if (new_page == NULL) {
            printf("Couldn't create new page\n");
            DebugBreak();
        }

        unsigned int zerolist_to_add_frame_to = page_to_pfn(new_page) % NUM_FREELISTS;


        // Add 9/10 of the pages to the zerolists. Add a 
        // sixth of pages to the freelists so that we can
        // actually trigger the too_few_pages event to add
        // pages back to the freelists
        if (zerolist_to_add_frame_to % 10 != 0) {
            EnterCriticalSection(&zero_list.freelists[zerolist_to_add_frame_to].list_lock);

            addToHead(&zero_list.freelists[zerolist_to_add_frame_to], new_page);

            LeaveCriticalSection(&zero_list.freelists[zerolist_to_add_frame_to].list_lock);

        }

        else {

            EnterCriticalSection(&freelist.freelists[zerolist_to_add_frame_to].list_lock);

            addToHead(&freelist.freelists[zerolist_to_add_frame_to], new_page);

            LeaveCriticalSection(&freelist.freelists[zerolist_to_add_frame_to].list_lock);

        }


        // Add all the pages to the zerolists
        // EnterCriticalSection(&zero_list.freelists[zerolist_to_add_frame_to].list_lock);

        // addToHead(&zero_list.freelists[zerolist_to_add_frame_to], new_page);

        // LeaveCriticalSection(&zero_list.freelists[zerolist_to_add_frame_to].list_lock);

        
    } 



}

#else


void instantiateFreeList(PULONG_PTR physical_frame_numbers, ULONG_PTR num_physical_frames, page_t* base_pfn) {
    freelist.blink = &freelist;
    freelist.flink = &freelist;

    InitializeCriticalSection(&freelist.list_lock);

    //acquireLock(&freelist.bitlock);
    EnterCriticalSection(&freelist.list_lock);

    for (unsigned i = 0; i < num_physical_frames; i++) {
        page_t* new_page = page_create(base_pfn, physical_frame_numbers[i]);
        if (new_page == NULL) {
            printf("Couldn't create new page\n");
        }


        addToHead(&freelist, new_page);
    } 

    //releaseLock(&freelist.bitlock);
    LeaveCriticalSection(&freelist.list_lock);
}


#endif


page_t* page_create(page_t* base, ULONG_PTR page_num) {
    page_t* new_page = base + page_num;
    page_t* base_page = VirtualAlloc(new_page, sizeof(page_t), MEM_COMMIT, PAGE_READWRITE);
    
    // COULD NOT ASSERT
    // C_ASSERT((PAGE_SIZE % sizeof(page_t)) == 0);
    
    if (base_page == NULL) {
        printf("Could not create page\n");
        return NULL;
    }

    return new_page;
}


HANDLE pages_available;

// Create standby list
void instantiateStandyList () {
    standby_list.blink = &standby_list;
    standby_list.flink = &standby_list;
    standby_list.num_of_pages = 0;

    standby_list.is_freelist = 0;

    pages_available = CreateEvent(NULL, FALSE, FALSE, NULL);

    InitializeCriticalSection(&standby_list.list_lock);

}


HANDLE modified_list_notempty;
HANDLE pagefile_blocks_available;
LPVOID* modified_writer_vas;

pagefile_t pf;


// Create modified list

void instantiateModifiedList() {
    modified_list.blink = &modified_list;
    modified_list.flink = &modified_list;
    modified_list.num_of_pages = 0;

    modified_list.is_freelist = 0;

    modified_list_notempty = CreateEvent(NULL, FALSE, FALSE, NULL);
    pagefile_blocks_available = CreateEvent(NULL, FALSE, FALSE, NULL);

    modified_writer_vas = malloc(sizeof(LPVOID) * BATCH_SIZE);
    if (modified_writer_vas == NULL) {
        printf("Couldn't malloc for mod writer VAs\n");
        return;
    }


    #if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;
    


    for (unsigned i = 0; i < BATCH_SIZE; i++) {
        LPVOID modified_page_va = VirtualAlloc2 (NULL,
                       NULL,
                       PAGE_SIZE,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &parameter,
                       1);
        
        if (modified_page_va == NULL) {
            printf("Could not malloc for a mod writer VA\n");
            return;
        }


        modified_writer_vas[i] = modified_page_va;
    }

    #else

    // For writing from memory to disk
    modified_page_va = VirtualAlloc(NULL,
                      PAGE_SIZE * BATCH_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);



    if (modified_page_va == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory for temp VA\n");
        return;
    }

    #endif

    // calloc initializes all values to 0, thus we 
    // don't have to spend the time looping and doing
    // it manually

    pf.pagefile_state = calloc(num_pagefile_blocks, sizeof(UCHAR));
    if (pf.pagefile_state == NULL) {
        printf("Could not calloc for pagefile states\n");
        DebugBreak();
    }

    // We can give this garbage values, since it will automatically
    // be overwritten in the mod writer once we find it's free and 
    // copy the page's contents into it

    pf.pagefile_contents = malloc(PAGE_SIZE * num_pagefile_blocks);
    if (pf.pagefile_contents == NULL) {
        printf("Could not malloc for pagefile contents\n");
        DebugBreak();
    }


    pf.free_slots_available = malloc(sizeof(int) * (num_pagefile_blocks-1));
    if (pf.free_slots_available == NULL) {
        printf("Could not malloc for pagefile free slots array\n");
        DebugBreak();
    }


    InitializeCriticalSection(&pf.pf_lock);
    EnterCriticalSection(&pf.pf_lock);

    pf.free_pagefile_blocks = 0;

    for (int i = 1; i < num_pagefile_blocks; i++) {
        addFreePagefileSlot(i);
    }

    LeaveCriticalSection(&pf.pf_lock);


    // InitializeCriticalSection(&pf.pf_lock);
    // EnterCriticalSection(&pf.pf_lock);

    // for (unsigned i = 0; i < num_pagefile_blocks; i++) {
    //     pf.pagefile_state[i] = 0;
    // }

    // for (unsigned i = 0; i < num_pagefile_blocks * PAGE_SIZE; i++) {
    //     pf.pagefile_contents[i] = '\0';
    // }


    // DM: take out the - 1 if O(1) solution breaks
    // pf.free_pagefile_blocks = num_pagefile_blocks - 1;

    // LeaveCriticalSection(&pf.pf_lock);



    InitializeCriticalSection(&modified_list.list_lock);
    // DM: To switch to shared lock,
    // use InitializeSRWLock();

    return;
}



// Create zeroed list
// I understand the misnomer of using "freelists"
// for the zero list structure. For the sake of time
// I am just using the same freelists attribute I implemented
// for the freelists. With more time, could just create list
// structures for each list. That way, each page would also carry
// much less around

HANDLE pages_on_freelists;

void instantiateZeroList() {
    zero_list.freelists = (page_t*)malloc(sizeof(page_t) * NUM_FREELISTS);

    if (zero_list.freelists == NULL) {
        printf("Couldn't malloc memory for freelists\n");
        DebugBreak();
        return;
    }

    zero_list.num_of_pages = 0;


    // This is used in the zeroing thread, 
    // therefore instantiating here.
    pages_on_freelists = CreateEvent(NULL, FALSE, FALSE, NULL);

    // Initialize each freelist

    for (unsigned i = 0; i < NUM_FREELISTS; i++) {

        InitializeCriticalSection(&zero_list.freelists[i].list_lock);

        EnterCriticalSection(&zero_list.freelists[i].list_lock);

        zero_list.freelists[i].blink = &zero_list.freelists[i];
        zero_list.freelists[i].flink = &zero_list.freelists[i];

        zero_list.freelists[i].num_of_pages = 0;
        zero_list.freelists[i].is_zerolist = 1;

        LeaveCriticalSection(&zero_list.freelists[i].list_lock);
    }

}



// For removing page from freelist
// CHANGED: listhead to page_t*
page_t* popTailPage(page_t* listhead) {

    debug_checks_list_counter(listhead);

    if (listhead->blink == listhead) {
        //printf("Empty - no pages (freelist)\n");
        return NULL;
    }

    // TODO: CONTAINING_RECORD
    page_t* tail = listhead->blink;

    tail->blink->flink = listhead;
    listhead->blink = tail->blink;
    listhead->num_of_pages -= 1;

    if (listhead->is_freelist == 1) {
        InterlockedDecrement(&freelist.num_of_pages);
    }

    if (listhead->is_zerolist == 1) {
        InterlockedDecrement(&zero_list.num_of_pages);
    }

    return tail;

}

// From TS
page_t* popHeadPage(page_t* listhead) {

    debug_checks_list_counter(listhead);

    if (listhead->flink == listhead) {
        //printf("Nothing to pop, the list is empty\n");
        return NULL;
    }

    // adjust links
    page_t* popped_page = (page_t*) listhead->flink;
    listhead->flink = popped_page->flink;
    listhead->flink->blink = listhead;
    listhead->num_of_pages -= 1;

    if (listhead->is_freelist == 1) {
        InterlockedDecrement(&freelist.num_of_pages);
    }

    if (listhead->is_zerolist == 1) {
        InterlockedDecrement(&zero_list.num_of_pages);
    }


    return popped_page;
}


void popFromAnywhere(page_t* listhead, page_t* given_page) {

    debug_checks_list_counter(listhead);

    // Remember to remove this once we find the 
    // standby_list.num_pages bug
    
    #if 0
    page_t* curr_page = listhead->flink;
    while (curr_page != listhead) {
        if (curr_page == given_page) {
            break;
        }
        curr_page = curr_page->flink;
    }

    if (curr_page == listhead) {
        DebugBreak();
    }

    #endif

    //Checks to see if its the tail or first real page
    if (listhead->blink == given_page) {
        given_page = popTailPage(listhead);
    }
    else if (listhead->flink == given_page) {
        // DM: Check that this works! Haven't tested
        given_page = popHeadPage(listhead);
    }
    // If not first page or tail page, its somewhere in the middle

    else {
        // Once we find the standby_list.num_pages bug,
        // remove this since its O(n)

        page_t* prev_page = given_page->blink;
        prev_page->flink = given_page->flink;
        given_page->flink->blink = prev_page;
        listhead->num_of_pages -= 1;

        if (listhead->is_freelist == 1) {
            InterlockedDecrement(&freelist.num_of_pages);
        }

        if (listhead->is_zerolist == 1) {
            InterlockedDecrement(&zero_list.num_of_pages);
        }
    }

    //return given_page;
}

// From TS
void addToHead(page_t* listhead, page_t* new_page) {

    if (listhead == NULL) {
        printf("Given listhead is NULL (addToHead)\n");
        return;
    }

    if (new_page == NULL) {
        printf("Given page is NULL(addToHead)\n");
        return;
    }

    new_page->flink = listhead->flink;
    listhead->flink->blink = new_page;
    listhead->flink = new_page;
    new_page->blink = listhead;
    listhead->num_of_pages += 1;

    if (listhead->is_freelist == 1) {
        InterlockedIncrement(&freelist.num_of_pages);
    }

    if (listhead->is_zerolist == 1) {
        InterlockedIncrement(&zero_list.num_of_pages);
    }

    if (listhead == &standby_list && new_page->pagefile_num == 0) {
        DebugBreak();
    }

    if (listhead == &modified_list && new_page->pagefile_num != 0) {
        DebugBreak();
    }

    // if (listhead == &standby_list && listhead->num_of_pages >= 1) {
    //     SetEvent(pages_available);
    // }

    debug_checks_list_counter(listhead);

    return;

}

void addToTail(page_t* listhead, page_t* new_page) {


    if (listhead == NULL) {
        printf("Given listhead is NULL (addToHead)\n");
        return;
    }

    new_page->flink = listhead;
    new_page->blink = listhead->blink;
    listhead->blink->flink = new_page;
    listhead->blink = new_page;
    listhead->num_of_pages += 1;

    if (listhead->is_freelist == 1) {
        InterlockedIncrement(&freelist.num_of_pages);
    }

    if (listhead->is_zerolist == 1) {
        InterlockedIncrement(&zero_list.num_of_pages);
    }

    if (listhead == &standby_list && new_page->pagefile_num == 0) {
        DebugBreak();
    }

    if (listhead == &modified_list && new_page->pagefile_num != 0) {
        DebugBreak();
    }

    if (listhead == &standby_list && listhead->num_of_pages >= 1) {
        SetEvent(pages_available);
    }

    debug_checks_list_counter(listhead);

    return;
}






// Will already hold the pagefile critical section
// Adds the free spot to the head
void addFreePagefileSlot(int free_spot) {
    if (free_spot < 0 || free_spot > num_pagefile_blocks) {
        printf("Given null free spot - addFreePagefileSlot\n");
        DebugBreak();
    }

    pf.free_slots_available[pf.free_pagefile_blocks] = free_spot;
    pf.free_pagefile_blocks++;
}




// Will already hold the pagefile critical section
// Takes a free spot from the tail
int takeFreePagefileSlot() {
    if (pf.free_pagefile_blocks == 0) {
        printf("No more pagefile slots available - takeFreePagefileSlot\n");
        DebugBreak();
        return -5;
    }

    pf.free_pagefile_blocks--;
    int freeslot_to_take = pf.free_slots_available[pf.free_pagefile_blocks];

    return freeslot_to_take;
}




page_t* pfn_to_page(ULONG64 given_pfn, PAGE_TABLE* pgtb) {
    if (pgtb == NULL || given_pfn == 0) {
        printf("Given pagetable is NULL or pfn is 0 (pfn_to_page)\n");
        DebugBreak();
        return NULL;
    }

    return (page_t*)(base_pfn + given_pfn);
}

ULONG64 page_to_pfn(page_t* given_page) {
    if (given_page == NULL) {
        DebugBreak();
        return 0;
    }

    return (ULONG64)(given_page - base_pfn);
}


VOID debug_checks_list_counter(page_t* listhead) {
    
    // NOT IN USE: after the switch to the bitlock,
    // this debug checker for list count became 
    // redundant, since it doesn't keep track of
    // the owning thread. 

    return;

    DWORD curr_thread = GetCurrentThreadId();
    if (curr_thread != (DWORD) (ULONG_PTR) listhead->list_lock.OwningThread) {
        DebugBreak();
    }

    unsigned count = 0;

    page_t* curr_page = listhead->flink;
    while (curr_page != listhead) {
        count += 1;


        if (listhead == &modified_list) {
            if (curr_page->pagefile_num != 0) {
                DebugBreak();
            }
        }


        if (listhead == &standby_list) {
            if (curr_page->pagefile_num == 0) {
                DebugBreak();
            }
        }


        curr_page = curr_page->flink;
    }

    if (count != listhead->num_of_pages) {
        DebugBreak();
    }
}
