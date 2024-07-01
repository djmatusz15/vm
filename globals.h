#include "list.h"

#define PAGE_SIZE 4096
#define PAGEFILE_BLOCKS 100

extern page_t* base_pfn;

// Lists
extern page_t freelist;
extern page_t modified_list;
extern page_t standby_list;

// Disk space
extern UCHAR pagefile_contents[PAGEFILE_BLOCKS * PAGE_SIZE];
extern UCHAR pagefile_state[PAGEFILE_BLOCKS];

// Aging event
extern HANDLE aging_event;

// Temp VAs
extern LPVOID modified_page_va;
extern LPVOID modified_page_va2;

// Writing to disk variables
extern HANDLE modified_list_notempty;
extern HANDLE pagefile_blocks_available;