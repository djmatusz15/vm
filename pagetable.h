#ifndef PAGETABLE_H
#define PAGETABLE_H

// #define PAGE_SIZE 4096      // 4k Pages

#include <stdio.h>
#include <windows.h>

struct page;
typedef struct page page_t;

extern PVOID p;

typedef struct {
    // 0 if free, 1 if active
    ULONG64 valid: 1;
    ULONG64 frame_number: 40;
    ULONG64 age: 3;
} VALID_PTE;

typedef struct {
    // Always zero
    ULONG64 valid: 1;

    // Can never be zero! 
    ULONG64 disc_number: 40;
    // Always zero
    ULONG64 in_memory: 1;
} INVALID_PTE;

typedef struct {
    // Always zero
    ULONG64 valid: 1;
    ULONG64 frame_number: 40;
    // Always one
    ULONG64 in_memory: 1;
    //ULONG64 is_modified: 1;
} TRANSITION_PTE;

typedef struct {
    union {
        VALID_PTE memory_format;
        INVALID_PTE disc_format;
        TRANSITION_PTE transition_format;
        ULONG64 entire_format;
    };
} PTE;

typedef struct {
    CRITICAL_SECTION lock;
    DWORD owning_thread;
} PTE_LOCK;

typedef struct {
    PTE* pte_array;
    ULONG64 num_ptes;
    PTE_LOCK* pte_regions_locks;
} PAGE_TABLE;


PAGE_TABLE* instantiatePagetable(ULONG64 num_VAs, page_t* virtual_memory_nums);
ULONG64 va_to_pte_index(PULONG_PTR arbitrary_va, PAGE_TABLE* pgtb);
PULONG_PTR pte_to_va(PTE* pte, PAGE_TABLE* pgtb);


#endif // PAGETABLE_H