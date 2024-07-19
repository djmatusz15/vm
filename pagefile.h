#ifndef PAGEFILE_H
#define PAGEFILE_H


#include <stdio.h>
#include <windows.h>

typedef struct pagefile {
    PUCHAR pagefile_state;
    PUCHAR pagefile_contents;
    unsigned free_pagefile_blocks;
} pagefile_t;


#endif  // PAGEFILE_H