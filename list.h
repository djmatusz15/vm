#ifndef FREELIST_H
#define FREELIST_H

#include <windows.h>
#include <stdio.h>
#include "pagetable.h"
#include "globals.h"

typedef struct page {
    struct page* flink;
    struct page* blink;
    int num_of_pages;
    PTE* pte;

    // Find where the page was stored on disk
    // (pagefile_blocks, range 0-99)
    ULONG64 pagefile_num;

    CRITICAL_SECTION list_lock;

    // Bitlock, for when we get
    // comfortable enough to not
    // need debugging help of
    // CRITICAL_SECTION

    volatile LONG bitlock;

    // Only used for the freelists

    page_t* freelists;
    unsigned int is_freelist;


    // Only used for zerolists. Again,
    // understand that it would be much 
    // better to create list structures to
    // carry less in each page, but lazily
    // doing this for the sake of time


    unsigned int is_zerolist;


    // Used for reference counting,
    // rescuing pages in flight

    unsigned int in_flight;
    unsigned int was_rescued;

    // Used for page read/write privileges.
    // Right now, only supporting read-write
    // or read-only privileges. Thus, only need
    // one bit to keep track of the two states.
    // In the future, implementing none/executable
    // privileges would require two bits to store,
    // in both the PTE and the page (since I store
    // the states in both)

    unsigned int readwrite;

} page_t;




void instantiateFreeList(PULONG_PTR physical_frame_numbers, ULONG_PTR num_physical_frames, page_t* base_pfn);
void instantiateStandyList();
void instantiateModifiedList();
void instantiateZeroList();
page_t* page_create(page_t* base, ULONG_PTR page_num);
page_t* popTailPage(page_t* listhead);
page_t* popHeadPage(page_t* listhead);
void popFromAnywhere(page_t* listhead, page_t* given_page);
void addToHead(page_t* listhead, page_t* given_page);
void addToTail(page_t* listhead, page_t* new_page);
void addFreePagefileSlot(int free_spot);
int takeFreePagefileSlot();
page_t* pfn_to_page(ULONG64 given_pfn, PAGE_TABLE* pgtb);
ULONG64 page_to_pfn(page_t* given_page);
VOID debug_checks_list_counter();

#endif //FREELIST_H
