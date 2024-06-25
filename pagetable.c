#define PAGE_EXP_KB                  12 
#define PAGE_SIZE                   (1 << PAGE_EXP_KB)
#define CONV_INDEX_ADDR(x) (x & ~(PAGE_SIZE - 1))
#define CONV_INDEX_NUM(x) (x >> PAGE_EXP_KB)

#include "pagetable.h"

PAGE_TABLE* instantiatePagetable(ULONG64 nums_VAs, PULONG_PTR virtual_memory_nums) {
    PAGE_TABLE* pgtb = (PAGE_TABLE*)malloc(sizeof(PAGE_TABLE));
    if (pgtb == NULL) {
        printf("Couldn't malloc for pagetable (instantiatePagetable)\n");
        return NULL;
    }

    pgtb->pte_array = (PTE**)malloc(sizeof(PTE*) * nums_VAs);
    if (pgtb->pte_array == NULL) {
        printf("Couldn't malloc for PTE array in pagetable\n");
        free(pgtb);
        return NULL;
    }

    unsigned count = 0;
    while (count < nums_VAs) {
        PTE* new_pte = (PTE*)malloc(sizeof(PTE));
        if (new_pte == NULL) {
            printf("Could not malloc for new PTE\n");

            for (unsigned j = 0; j < count; j++) {
                free(pgtb->pte_array[j]);
            }

            free(pgtb->pte_array);
            free(pgtb);
            return NULL;
        }

        new_pte->memory_format.frame_number = 0;
        new_pte->memory_format.valid = 0;
        new_pte->memory_format.age = 0;

        pgtb->pte_array[count] = new_pte;
        count++;
    }

    pgtb->num_ptes = nums_VAs;
    pgtb->virtual_frame_num = (ULONG64)virtual_memory_nums;

    // InitializeCriticalSection(&pgtb->pte_lock);

    return pgtb;
}


// Changed line 72 to &pgtb instead of pgtb
PTE* va_to_pte(PAGE_TABLE* pgtb, PULONG_PTR arbitrary_va) {
    if (pgtb == NULL) {
        printf("Given invalid pagetable (va_to_pte)\n");
        return NULL;
    }

    if (arbitrary_va == NULL) {
        printf("Given VA is NULL (va_to_pte)\n");
        return NULL;
    }

    ULONG64 conv_index = CONV_INDEX_NUM((ULONG64)arbitrary_va - pgtb->virtual_frame_num);
    if (conv_index > pgtb->num_ptes) {
        printf("Was unable to convert VA to valid PTE index\n");
        return NULL;
    }

    return pgtb->pte_array[conv_index];

}

PULONG_PTR pte_to_va(PAGE_TABLE* pgtb, PTE* pte) {
    if (pgtb == NULL || pte == NULL) {
        printf("Given pagetable and/or PTE is NULL\n");
        return NULL;
    }

    ULONG64 base_address_ptes = (ULONG64)pgtb->pte_array;
    ULONG64 address_of_given_pte = (ULONG64)pte;

    ULONG64 given_pte_index = (address_of_given_pte - base_address_ptes) / sizeof(PTE);
    PULONG_PTR pte_va = (PULONG_PTR)(pgtb->virtual_frame_num + (given_pte_index * PAGE_SIZE));

    return pte_va;

}
