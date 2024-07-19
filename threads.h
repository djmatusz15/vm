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

VOID LockPagetable(unsigned i);
VOID UnlockPagetable(unsigned i);
VOID WriteToPTE(PTE* pte, PTE pte_contents);

HANDLE* initialize_threads(VOID);

BOOL map_to_pagefile(page_t* curr_page, unsigned pagefile_slot);
BOOL map_batch_to_pagefile(page_t** batched_pages, unsigned pagefile_slot);