#define NUM_FREE_LISTS 16
#define PAGE_SIZE 4096

#include "list.h"
#include "pagetable.h"

HANDLE trim_now;

void instantiateFreeList(PULONG_PTR physical_frame_numbers, ULONG_PTR num_physical_frames, page_t* base_pfn) {
    freelist.blink = &freelist;
    freelist.flink = &freelist;
    //freelist.num_of_pages = num_physical_frames;

    for (unsigned i = 0; i < num_physical_frames; i++) {
        page_t* new_page = page_create(base_pfn, physical_frame_numbers[i]);
        if (new_page == NULL) {
            printf("Couldn't create new page\n");
        }

        addToHead(&freelist, new_page);
    } 

    InitializeCriticalSection(&freelist.list_lock);

}

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


// Create standby list
void instantiateStandyList () {
    standby_list.blink = &standby_list;
    standby_list.flink = &standby_list;

    InitializeCriticalSection(&standby_list.list_lock);

}


HANDLE modified_list_notempty;
HANDLE pagefile_blocks_available;
LPVOID modified_page_va;
LPVOID modified_page_va2;


// Create modified list
// DM: Ask if virtualAlloc of the 
// temp VAs here is screwing something up?
void instantiateModifiedList() {
    modified_list.blink = &modified_list;
    modified_list.flink = &modified_list;

    modified_list_notempty = CreateEvent(NULL, FALSE, FALSE, NULL);
    pagefile_blocks_available = CreateEvent(NULL, FALSE, FALSE, NULL);

    // For writing from memory to disk
    modified_page_va = VirtualAlloc(NULL,
                      PAGE_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

    if (modified_page_va == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory for temp VA\n");
        return;
    }

    // DM: this is for writing from disk back to new page
    modified_page_va2 = VirtualAlloc(NULL,
                      PAGE_SIZE,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

    if (modified_page_va2 == NULL) {
        printf ("full_virtual_memory_test : could not reserve memory for temp2 VA\n");
        return;
    }

    InitializeCriticalSection(&modified_list.list_lock);

    return;
}


// For removing page from freelist
// CHANGED: listhead to page_t*
page_t* popTailPage(page_t* listhead) {

    if (listhead->blink == listhead) {
        //printf("Empty - no pages (freelist)\n");
        return NULL;
    }

    // TODO: CONTAINING_RECORD
    page_t* tail = listhead->blink;

    tail->blink->flink = listhead;
    listhead->blink = tail->blink;
    listhead->num_of_pages -= 1;

    return tail;

}

// From TS
page_t* popHeadPage(page_t* listhead) {

    if (listhead->flink == listhead) {
        //printf("Nothing to pop, the list is empty\n");
        return NULL;
    }

    // adjust links
    page_t* popped_page = (page_t*) listhead->flink;
    listhead->flink = listhead->flink->flink;
    listhead->flink->blink = listhead;
    listhead->num_of_pages -= 1;

    return popped_page;
}


page_t* popFromAnywhere(page_t* listhead, page_t* given_page) {
    //page_t* returned_page;

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
        page_t* prev_page = given_page->blink;
        prev_page->flink = given_page->flink;
        given_page->flink->blink = prev_page;
        listhead->num_of_pages -= 1;
    }

    return given_page;
}

// From TS
void addToHead(page_t* listhead, page_t* new_page) {

    //EnterCriticalSection(&listhead->list_lock);

    if (listhead == NULL) {
        printf("Given listhead is NULL (addToTail)\n");
        //LeaveCriticalSection(&listhead->list_lock);
        return;
    }

    new_page->flink = listhead->flink;
    listhead->flink->blink = new_page;
    listhead->flink = new_page;
    new_page->blink = listhead;
    listhead->num_of_pages += 1;

    return;

    //LeaveCriticalSection(&listhead->list_lock);

}


page_t* pfn_to_page(ULONG64 given_pfn, PAGE_TABLE* pgtb) {
    if (pgtb == NULL || given_pfn == 0) {
        DebugBreak();
        printf("Given pagetable is NULL or pfn is 0 (pfn_to_page)\n");
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