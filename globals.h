#include "list.h"

#define PAGE_SIZE 4096
#define NUM_PTE_REGIONS 128
#define PTES_PER_REGION 32

// #define NUM_PTE_REGIONS 1
// #define PTES_PER_REGION 4096

#define MB(x)                       ((x) * 1024 * 1024)
#define GB(x)                       ((x) * 1024 * 1024 * 1024)

#define VIRTUAL_ADDRESS_SIZE        MB(16)
#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)

#define NUM_OF_THREADS              5

extern page_t* base_pfn;

// Lists
extern page_t freelist;
extern page_t modified_list;
extern page_t standby_list;

// Disk space
extern PUCHAR pagefile_contents;
extern PUCHAR pagefile_state;
extern ULONG64 num_pagefile_blocks;

// Aging event
extern HANDLE aging_event;
extern HANDLE trim_now;

// Temp VAs
extern LPVOID modified_page_va;
extern LPVOID modified_page_va2;

// Writing to disk variables
extern HANDLE modified_list_notempty;
extern HANDLE pagefile_blocks_available;

// Pagetable
extern PAGE_TABLE* pgtb;

// Standby
extern HANDLE pages_available;