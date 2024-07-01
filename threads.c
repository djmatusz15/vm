#include "threads.h"


// Handles trimming PTEs that are active, and moving them
// to modified list. Switch the PTE as well.
// DM: Handle this so that it moves to modified list, not standby!

void handle_trimming(PAGE_TABLE* pgtb, page_t* modified_head) {

    // We'll have it run constantly
    while (1) {
        ULONG64 count = 0;
        PTE* curr_pte = &pgtb->pte_array[count];

        while (count < pgtb->num_ptes) {
            if (curr_pte->memory_format.valid != 1) {
                count++;
            }
        }

        if (count == pgtb->num_ptes) {
            printf("Couldn't find active PTE to trim\n");
            return;
        }

        curr_pte = &pgtb->pte_array[count];

        // Found PTE to trim
        curr_pte->transition_format.valid = 0;
        curr_pte->transition_format.in_memory = 1;
        curr_pte->transition_format.is_modified = 1;

        addToHead(modified_head, pfn_to_page(curr_pte->transition_format.frame_number, pgtb));

        PULONG_PTR conv_va = pte_to_va(curr_pte, pgtb);

        if (MapUserPhysicalPages(conv_va, 1, NULL) == FALSE) {
            printf("Couldn't unmap VA %p (handle_trimming)\n", conv_va);
            return;
        }
    }
}

UCHAR pagefile_contents[100 * 4096];
UCHAR pagefile_state[100];

#define FREE 0
#define IN_USE 1

void handle_modifying(page_t* modified_head) {

    // This will also run constantly
    // DM: Are there cases where we may want to wait?

     while (1) {
        page_t* curr_page = popTailPage(modified_head);
        if (curr_page == NULL) {
            printf("Could not pop tail from modified list\n");
            WaitForSingleObject(modified_list_notempty, 0);
            continue;
        }

        unsigned i;
        for (i = 0 ; i < PAGEFILE_BLOCKS; i++) {
            if (pagefile_state[i] == FREE) {
                pagefile_state[i] = IN_USE;
                break;
            }
        }

        if (i == PAGEFILE_BLOCKS) {
            printf("All disk slots in use\n");
            addToHead(modified_head, curr_page);
            WaitForSingleObject(pagefile_blocks_available, 0);
            continue;
        }

        ULONG64 conv_pfn = curr_page->pte->transition_format.frame_number;
        if (MapUserPhysicalPages (modified_page_va, 1, &conv_pfn) == FALSE) {

            printf ("full_virtual_memory_test : could not map VA %p to page %llX\n", modified_page_va, conv_pfn);
            DebugBreak();

        }

        memcpy(&pagefile_contents[i * PAGE_SIZE], modified_page_va, PAGE_SIZE);
        
        // We don't need this, store as disc_number
        // in the page's PTE.
        // curr_page->pagefile = i;


        if (MapUserPhysicalPages (modified_page_va, 1, NULL) == FALSE) {

            printf ("full_virtual_memory_test : could not unmap VA %p\n", modified_page_va);
            DebugBreak();

        }
        

        addToHead(&standby_list, curr_page);

        curr_page->pte->disc_format.disc_number = i;
        curr_page->pte->disc_format.valid = 0;
        curr_page->pte->disc_format.in_memory = 0;
     }
}

// is this how you define it?
HANDLE aging_event;

void handle_aging(PAGE_TABLE* pgtb) {
    while (1) {
        WaitForSingleObject(aging_event, 0);

        // EnterCriticalSection(&pgtb->pte_lock);
        for (unsigned i = 0; i < pgtb->num_ptes; i++) {
            PTE* curr_pte = &pgtb->pte_array[i];

            if (curr_pte->memory_format.valid == 1) {
                continue;
            }

        }
        // LeaveCriticalSection(&pgtb->pte_lock);

    }
}