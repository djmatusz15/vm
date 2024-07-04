#include <windows.h>
#include <stdio.h>
#include "list.h"
#include "pagetable.h"
#include "globals.h"

#define NUM_HANDLES 3

LPTHREAD_START_ROUTINE handle_trimming();
LPTHREAD_START_ROUTINE handle_modifying();
LPTHREAD_START_ROUTINE handle_aging();

VOID LockPagetable(unsigned i);
VOID UnlockPagetable(unsigned i);
VOID WriteToPTE(PTE* pte, PTE pte_contents);