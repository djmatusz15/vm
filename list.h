#ifndef FREELIST_H
#define FREELIST_H

#include <windows.h>
#include <stdio.h>
#include "pagetable.h"
#include "globals.h"

typedef struct page {
    struct page* flink;
    struct page* blink;
    PTE* pte;

    CRITICAL_SECTION list_lock;
} page_t;


void instantiateFreeList(PULONG_PTR physical_frame_numbers, ULONG_PTR num_physical_frames, page_t* base_pfn);
void instantiateStandyList();
void instantiateModifiedList();
page_t* page_create(page_t* base, ULONG_PTR page_num);
page_t* popTailPage(page_t* listhead);
page_t* popHeadPage(page_t* listhead);
page_t* popFromAnywhere(page_t* listhead, page_t* given_page);
void addToHead(page_t* listhead, page_t* given_page);
page_t* pfn_to_page(ULONG64 given_pfn, PAGE_TABLE* pgtb);
ULONG64 page_to_pfn(page_t* given_page);

#endif //FREELIST_H
