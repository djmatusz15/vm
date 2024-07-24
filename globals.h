#include "list.h"
#include "pagefile.h"
#include "bitlock.h"

#define PAGE_SIZE 4096
#define NUM_PTE_REGIONS  2048    // 2048
#define PTES_PER_REGION 128     // 128
#define BATCH_SIZE 32       // 32

#define MB(x)                       ((x) * 1024 * 1024)
#define GB(x)                       ((x) * 1024 * 1024 * 1024)

#define VIRTUAL_ADDRESS_SIZE        MB(128)          // 128
#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 4)    // 8

// This is 8192 pages right now


#define NUM_OF_THREADS             19
#define NUM_OF_FAULTING_THREADS     NUM_OF_THREADS - 3

#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 1

// VM privileges (for mult VAs to one PA)
extern HANDLE physical_page_handle;
extern ULONG_PTR virtual_address_size;

extern page_t* base_pfn;

// Lists
extern page_t freelist;
extern page_t modified_list;
extern page_t standby_list;
extern page_t zero_list;

// Disk space
// extern PUCHAR pagefile_contents;
// extern PUCHAR pagefile_state;

extern pagefile_t pf;
extern ULONG64 num_pagefile_blocks;

// Aging event
extern HANDLE aging_event;
extern HANDLE trim_now;

// Temp VAs
extern LPVOID modified_page_va;

// Writing to disk variables
extern HANDLE modified_list_notempty;
extern HANDLE pagefile_blocks_available;

// Pagetable
extern PAGE_TABLE* pgtb;

// Standby
extern HANDLE pages_available;