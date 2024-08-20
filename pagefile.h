#ifndef PAGEFILE_H
#define PAGEFILE_H


#include <stdio.h>
#include <windows.h>

typedef struct pagefile {
    PUCHAR pagefile_state;
    PUCHAR pagefile_contents;
    unsigned free_pagefile_blocks;

    // Putting a lock on the pf separately,
    // instead of just using the modified
    // list lock
    CRITICAL_SECTION pf_lock;
} pagefile_t;


#endif  // PAGEFILE_H