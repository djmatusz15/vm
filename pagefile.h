#ifndef PAGEFILE_H
#define PAGEFILE_H


#include <stdio.h>
#include <windows.h>
#include "globals.h"

typedef struct pagefile {
    PUCHAR pagefile_state;
    PUCHAR pagefile_contents;
    unsigned free_pagefile_blocks;

    // Putting a lock on the pf separately,
    // instead of just using the modified
    // list lock
    CRITICAL_SECTION pf_lock;

    // This array will keep track of free
    // pagefile slots. The mod writer will
    // pop free slots from the tail to write
    // to, and the pagefaulter (rescuer and 
    // disk reads) will add it newly freed
    // spots to the head
    int* free_slots_available;
} pagefile_t;



void addFreePagefileSlot(int free_spot);
int takeFreePagefileSlot();


#endif  // PAGEFILE_H