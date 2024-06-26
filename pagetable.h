#ifndef PAGETABLE_H
#define PAGETABLE_H

// #define PAGE_SIZE 4096      // 4k Pages

#include <stdio.h>
#include <windows.h>

typedef struct {
    // 0 if free, 1 if active
    ULONG64 valid: 1;
    ULONG64 frame_number: 40;
    ULONG64 age: 3;
} VALID_PTE;

typedef struct {
    // Always zero
    ULONG64 valid: 1;
    ULONG64 disc_number: 40;
    // Always one
    ULONG64 on_disc: 1;
} INVALID_PTE;

typedef struct {
    // Always zero
    ULONG64 valid: 1;
    ULONG64 frame_number: 40;
    // Always zero
    ULONG64 on_disc: 1;
} TRANSITION_PTE;

typedef struct {
    union {
        VALID_PTE memory_format;
        INVALID_PTE disc_format;
        TRANSITION_PTE transition_format;
    };
} PTE;

typedef struct {
    PTE* pte_array;
    ULONG64 num_ptes;
    ULONG64 virtual_frame_num;
    CRITICAL_SECTION pte_lock;
} PAGE_TABLE;


PAGE_TABLE* instantiatePagetable(ULONG64 num_VAs, PULONG_PTR virtual_memory_nums);
PTE* va_to_pte(PAGE_TABLE* pgtb, PULONG_PTR arbitrary_va);
PULONG_PTR pte_to_va(PAGE_TABLE* pgtb, PTE* pte);


#endif // PAGETABLE_H
