#define NUM_FREE_LISTS 16

#include "list.h"

page_t* instantiateFreeList(PULONG_PTR physical_frame_numbers, ULONG_PTR num_physical_frames) {
    page_t* first_page = (page_t*)malloc(sizeof(page_t));
    if (first_page == NULL) {
        printf("Couldn't malloc for first page\n");
        return NULL;
    }

    first_page->blink = first_page;
    first_page->flink = first_page;
    first_page->pfn = 0;

    unsigned count = 0;
    page_t* prev_page = first_page;

    while (count < num_physical_frames) {
        page_t* new_page = (page_t*)malloc(sizeof(page_t));

        if (new_page == NULL) {
            printf("New frame could not be malloc'd (instantiateFreeList)\n");
        }

        // Set the new frame
        new_page->pfn = physical_frame_numbers[count];
        new_page->flink = first_page;
        new_page->blink = prev_page;

        // Fix prev_frame flink and first page's blink
        prev_page->flink = new_page;
        first_page->blink = new_page;

        // Set new prev_frame and incr count
        prev_page = new_page;
        count++;
    }

    page_t* curr_page = first_page->flink;
    unsigned count_test = 0;
    while (curr_page != first_page) {
        printf("curr_page address: %p -> count: %d\n", curr_page, count_test);
        curr_page = curr_page->flink;
        count_test++;
    }

    return first_page;
}


// Create standby list
page_t* instantiateStandyList () {
    page_t* first_page = (page_t*)malloc(sizeof(page_t));
    if (first_page == NULL) {
        printf("Couldn't malloc for head (standby list)\n");
        return NULL;
    }

    first_page->blink = first_page;
    first_page->flink = first_page;
    first_page->pfn = 0;

    return first_page;
}

// For removing page from freelist
// CHANGED: listhead to page_t*
page_t* popTailPage(page_t* listhead) {

    //EnterCriticalSection(&listhead->list_lock);

    if (listhead->blink == listhead) {
        //printf("Empty - no pages (freelist)\n");
        //LeaveCriticalSection(&listhead->list_lock);
        return NULL;
    }

    // TODO: CONTAINING_RECORD
    page_t* tail = listhead->blink;

    tail->blink->flink = listhead;
    listhead->blink = tail->blink;

    //LeaveCriticalSection(&listhead->list_lock);

    // DM: Couldn't another thread access address of tail 
    // between 85 and return at 90? What to do?

    return tail;

}

void addToTail(page_t* listhead, ULONG64 given_pfn) {

    //EnterCriticalSection(&listhead->list_lock);

    if (listhead == NULL) {
        printf("Given listhead is NULL (addToTail)\n");
        //LeaveCriticalSection(&listhead->list_lock);
        return;
    }

    page_t* new_page = (page_t*)malloc(sizeof(page_t));
    if (new_page == NULL) {
        printf("Couldn't malloc for new page (addToTail)\n");
        //LeaveCriticalSection(&listhead->list_lock);
        return;
    }

    new_page->blink = listhead->blink;
    new_page->flink = listhead;
    new_page->pfn = given_pfn;

    listhead->blink->flink = new_page;
    listhead->blink = new_page;

    //LeaveCriticalSection(&listhead->list_lock);

}
