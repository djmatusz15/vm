#define PAGE_EXP_KB                  12 
#define PAGE_SIZE                   (1 << PAGE_EXP_KB)
#define CONV_INDEX_ADDR(x) (x & ~(PAGE_SIZE - 1))
#define CONV_INDEX_NUM(x) (x >> PAGE_EXP_KB)

#include "pagetable.h"

PAGE_TABLE* instantiatePagetable(ULONG64 nums_VAs, page_t* base_pfn) {
    PAGE_TABLE* pgtb = (PAGE_TABLE*)malloc(sizeof(PAGE_TABLE));
    if (pgtb == NULL) {
        printf("Couldn't malloc for pagetable (instantiatePagetable)\n");
        return NULL;
    }

    pgtb->pte_array = (PTE*)malloc(sizeof(PTE) * nums_VAs);
    if (pgtb->pte_array == NULL) {
        printf("Couldn't malloc for PTE array in pagetable\n");
        free(pgtb);
        return NULL;
    }

    unsigned count = 0;
    while (count < nums_VAs) {
        // PTE* new_pte = &pgtb->pte_array[count];

        pgtb->pte_array[count].memory_format.frame_number = 0;
        pgtb->pte_array[count].memory_format.age = 0;
        pgtb->pte_array[count].memory_format.valid = 0;

        count++;
    }

    pgtb->num_ptes = nums_VAs;

    InitializeCriticalSection(&pgtb->pte_lock);

    return pgtb;
}


PTE* va_to_pte(PULONG_PTR arbitrary_va, PAGE_TABLE* pgtb) {

    if (arbitrary_va == NULL) {
        printf("Given VA is NULL (va_to_pte)\n");
        return NULL;
    }

    
    ULONG64 conv_index = CONV_INDEX_NUM((ULONG64)arbitrary_va - (ULONG64)p);
    if (conv_index > pgtb->num_ptes) {
        printf("Was unable to convert VA to valid PTE index\n");
        return NULL;
    }

    return &pgtb->pte_array[conv_index];
}

PULONG_PTR pte_to_va(PTE* pte, PAGE_TABLE* pgtb) {
    if (pte == NULL) {
        printf("Given pagetable and/or PTE is NULL\n");
        return NULL;
    }

    ULONG64 base_address_pte_list = (ULONG64) pgtb->pte_array;
    ULONG64 pte_address = (ULONG64) pte;

    ULONG64 pte_index = (pte_address - base_address_pte_list) / sizeof(PTE);

    PULONG_PTR virtual_address = (PULONG_PTR) ((ULONG_PTR)p + (pte_index * PAGE_SIZE));

    return virtual_address;

}
