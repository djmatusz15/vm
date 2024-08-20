#include <windows.h>
#include <stdio.h>
#include "list.h"

void trimmer();
void acquireLock(volatile LONG* lock);
void releaseLock(volatile LONG* lock);
BOOL tryAcquireLock(volatile LONG* lock);
int acquireRandomFreelistLock();
int acquireRandomZerolistLock();