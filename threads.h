#include <windows.h>
#include <stdio.h>
#include "list.h"
#include "pagetable.h"
#include "pagefault.h"
#include "globals.h"

LPTHREAD_START_ROUTINE handle_trimming();
LPTHREAD_START_ROUTINE handle_modifying();
LPTHREAD_START_ROUTINE handle_aging();
LPTHREAD_START_ROUTINE handle_faulting();
LPTHREAD_START_ROUTINE handle_zeroing();

VOID LockPagetable(unsigned i);
VOID UnlockPagetable(unsigned i);
VOID WriteToPTE(PTE* pte, PTE pte_contents);

HANDLE* initialize_threads(VOID);

BOOL map_to_pagefile(page_t* curr_page, unsigned pagefile_slot);
BOOL map_batch_to_pagefile(page_t** batched_pages, ULONG64* batched_pfns, int* slots_to_map_to, unsigned curr_batch_size);
void unmap_batch (page_t** batched_pages, unsigned int curr_batch_size);
void write_ptes_to_modified(page_t** batched_pages, unsigned int curr_batch_size);
void add_pages_to_modified(page_t** batched_pages, unsigned int curr_batch_size);
//void trim_behind_fault(LPVOID* vas_to_trim_behind, int vas_to_trim_count);