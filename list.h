#ifndef FREELIST_H
#define FREELIST_H

#include <windows.h>
#include <stdio.h>

typedef struct page {
    struct page* blink;
    struct page* flink;
    ULONG64 pfn;
    CRITICAL_SECTION list_lock;
} page_t;

page_t* instantiateFreeList(PULONG_PTR physical_frame_numbers, ULONG_PTR num_physical_frames);
page_t* instantiateStandyList();
page_t* popTailPage(page_t* listhead);
void addToTail(page_t* listhead, ULONG64 given_pfn);

#endif //FREELIST_H
