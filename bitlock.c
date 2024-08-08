/**
 * Examples of creating locks using interlocked operations
 */

#include "bitlock.h"

long num_modified_pages;
void trimmer() {

    /**
     * Value is the new value of num_modified_pages
     * 
     * Increments in a single indivisible block
     */
    long value = InterlockedIncrement(&num_modified_pages);

    if (value == 1) {
        /**
         * We have just put the first page on
         * 
         * 
         */
    }

    long old_val = 0;
    long new_val = 1;

    old_val = num_modified_pages;

    /**
     * First parameter is our "lock field", or the one that we want to change
     * 
     * 
     */
    long real_old_value = InterlockedCompareExchange(&num_modified_pages, new_val, old_val);

    if (real_old_value == old_val) {
        /**
         * Succeeded
         */
    } else {
        old_val = real_old_value;
        new_val++;
        /**
         * Go to line 38, we failed and need to try again
         */
    }

    // Decrements in a single indivisible block
    InterlockedDecrement(&num_modified_pages);

}


void acquireLock(volatile LONG* lock) {
    /**
     * 1 means the lock is owned
     */
    long new_val = 1;

    /**
     * 0 means the lock is not owned
     */
    long old_val = 0;

    /**
     * First parameter is our "lock field", or the one that we want to change
     * 
     * Second parameter is the value that we want to change the "lock field" to (the "new value")
     * 
     * Third parameter is what the current value of the "lock field" must be in order to change the value. 
     * Otherwise, we fail and do not change the value at all
     * 
     * This is a CPU level instruction that keeps the "lock field" exclusive in the cache during that single instruction
     * 
     * This means that the given address cannot straddle two cache lines - as the CPU could end in deadlock
     */
    while (TRUE) {
        long real_old_value = InterlockedCompareExchange(lock, new_val, old_val);

        if (real_old_value == 0) {
            /**
             * Succeeded, it was not owned, but now it is set to 1 and we own it
             */
            break;
        }

        /**
         * We have failed, we have to keep trying until we can set it to 1 at 87
         */
    }
    
}

void releaseLock(volatile LONG* lock) {

    /**
     * 0 means the lock is not owned
     */
    long new_val = 0;


    /**
     * 1 means the lock is owned
     */
    long old_val = 1;

    /**
     * First parameter is our "lock field", or the one that we want to change
     * 
     * Second parameter is the value that we want to change the "lock field" to (the "new value")
     * 
     * Third parameter is what the current value of the "lock field" must be in order to change the value. 
     * Otherwise, we fail and do not change the value at all
     * 
     * This is a CPU level instruction that keeps the "lock field" exclusive in the cache during that single instruction
     * 
     * This means that the given address cannot straddle two cache lines - as the CPU could end in deadlock
     */
    while (TRUE) {
        long real_old_value = InterlockedCompareExchange(lock, new_val, old_val);

        if (real_old_value == 1) {
            /**
             * Succeeded, it was not owned, but now it is set to 0 and we don't own it
             */
            break;
        }

        /**
         * We have failed, we have to keep trying until we can set it to 0 at 121
         */
    }
}



BOOL tryAcquireLock(volatile LONG* lock) {
    long old_val = 0;
    long new_val = 1;

    long real_old_val = InterlockedCompareExchange(lock, new_val, old_val);

    // If the old val was 0, then we successfully 
    // switched the bit to 1, meaning we acquired the
    // lock. Otherwise, return false.

    if (real_old_val == 0) {
        return TRUE;
    }

    else {
        return FALSE;
    }
}

// Will get some random freelist lock, for when 
// we need some arbitrary freelist page

int acquireRandomFreelistLock() {

    if (freelist.num_of_pages == 0) {
        return -1;
    }



    while (TRUE) {

        // Return -1 if 0 pages on freelist

        if (freelist.num_of_pages == 0) {
            return -1;
        }


        for (unsigned i = 0; i < NUM_FREELISTS; i++) {
            

            // If the number of pages on this freelist is 0,
            // don't even bother and continue to next freelist

            if (freelist.freelists[i].num_of_pages == 0) {
                continue;
            }

            // Else, try and get the lock for this freelist

            if (TryEnterCriticalSection(&freelist.freelists[i].list_lock) == TRUE) {

                // If you get the lock, make sure there are still
                // pages on this freelist, since it could have changed
                // between the time you last checked and when you got
                // the lock. If it changed to 0, drop lock and keep trying

                if (freelist.num_of_pages <= 0) {
                    LeaveCriticalSection(&freelist.freelists[i].list_lock);
                    continue;
                }



                // DM: Fix to <= 0
                if (freelist.freelists[i].flink == &freelist.freelists[i]) {
                    //DebugBreak();
                    LeaveCriticalSection(&freelist.freelists[i].list_lock);
                    continue;
                }

                // Once we get here, we are guaranteed to have gotten
                // the lock to this freelist and that it has at least 
                // 1 page on it. Return the index so we can drop the
                // lock in the parent once we're done.

                if ((DWORD)freelist.freelists[i].list_lock.OwningThread != GetCurrentThreadId()) {
                    DebugBreak();
                }

                return i;


            }

        }

    }
}
