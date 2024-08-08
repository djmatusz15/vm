#ifndef GLOBALS_H
#define GLOBALS_H

#include "list.h"
#include "pagefile.h"
#include "bitlock.h"

#define MB(x)                       ((x) * 1024 * 1024)
#define GB(x)                       ((x) * 1024 * 1024 * 1024)
#define PAGE_SIZE 4096

#define FRACTION_OF_VA_SPACE_ALLOCATED_TO_PHYSICAL      4
#define VIRTUAL_ADDRESS_SIZE        GB(1)  
#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / FRACTION_OF_VA_SPACE_ALLOCATED_TO_PHYSICAL)
#define NUMBER_OF_VIRTUAL_PAGES     VIRTUAL_ADDRESS_SIZE / PAGE_SIZE



#define NUM_PTE_REGIONS     ((NUMBER_OF_VIRTUAL_PAGES) / 64)    // 32
#define PTES_PER_REGION    ((NUMBER_OF_VIRTUAL_PAGES) / NUM_PTE_REGIONS)  
#define BATCH_SIZE 256          // 1024       

// Defines number of threads we are using in our program
// NUM_OF_THREADS = total threads, including trimmer + modified
// writer + ager, NUM_OF_FAULTING THREADS = number of threads faulting
#define NUM_OF_THREADS             7
#define NUM_OF_FAULTING_THREADS     NUM_OF_THREADS - 3


// The number of temporary VAs that we allow to flush 

#define NUM_TEMP_VAS    512


#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 1
#define RANDOM_ACCESSES 0
#define MOVE_PAGES_FROM_STANDBY_TO_FREELIST 1

// Not in use yet
#define SUPPORT_MULTIPLE_FREELISTS 0
#define NUM_FREELISTS           64


#define READ_BATCH_FROM_DISK 1
#define CONSECUTIVE_ACCESSES 128

// Flusher struct for temp VA for reading from disk

typedef struct {
    LPVOID* temp_vas;
    unsigned int num_of_vas_used;
} FLUSHER;



// VM privileges (for mult VAs to one PA)
extern HANDLE physical_page_handle;
extern ULONG_PTR virtual_address_size;
ULONG_PTR virtual_address_size_in_unsigned_chunks;

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


// Global counts for faults
extern int rescues;
extern int read_from_disk;
extern int new_ptes;
extern int ran_into_active_ptes;

#endif // GLOBALS_H