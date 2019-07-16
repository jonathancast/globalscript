/* Adapted from Plan 9 lock.c */

#include <u.h>
#include <libc.h>
#include <stdatomic.h>
#include <libglobalscript.h>

void
lock(Lock *lk)
{
    int i;

    /* once fast */
    if (!atomic_flag_test_and_set(&lk->val)) return;

    /* a thousand times pretty fast */
    for (i = 0; i < 1000; i++) {
        if (!atomic_flag_test_and_set(&lk->val)) return;
        sleep(0);
    }

    /* now nice and slow */
    for (i = 0; i < 1000; i++) {
        if (!atomic_flag_test_and_set(&lk->val)) return;
        sleep(100);
    }

    /* take your time */
    while (atomic_flag_test_and_set(&lk->val)) sleep(1000);
}

void
unlock(Lock *lk)
{
       atomic_flag_clear(&lk->val);
}
