#include <windows.h>
#include <stdio.h>
#include "list.h"
#include "pagetable.h"
#include "globals.h"

#define NUM_HANDLES 3

void handle_trimming(PAGE_TABLE* pgtb, page_t* standby_head);
void handle_modifying(page_t* modified_head);
void handle_aging(PAGE_TABLE* pgtb);