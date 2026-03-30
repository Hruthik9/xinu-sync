/* swait.c - swait */
/* Author 1: Nikhil Pembadi */
/* Author 2: S.Hruthik */

#include <xinu.h>

/*------------------------------------------------------------------------
 *  swait  -  Simultaneous wait on two semaphores (AND synchronization).
 *
 *  PURPOSE:
 *    Blocks the calling process until BOTH semaphores are available, then
 *    decrements both counts atomically.  A process never holds one semaphore
 *    while waiting for the other, eliminating the "hold-and-wait" condition
 *    required for deadlock.
 *
 *  ALGORITHM (retry loop):
 *    1. Disable interrupts and validate both semaphore IDs (must be in range,
 *       allocated/S_USED, and different from each other).  Return SYSERR if
 *       any check fails.
 *    2. If both scount > 0, decrement each, restore interrupts, return OK.
 *    3. Otherwise block on the FIRST semaphore whose scount < 1, exactly as
 *       the standard wait() call would: decrement scount, set process state
 *       to PR_WAIT, enqueue on that semaphore's wait queue, call resched().
 *    4. When the process is woken (resched returns), increment the count of
 *       the semaphore it was waiting on to undo the blocking decrement.
 *    5. Restore interrupts and loop back to step 1 to retry.
 *
 *  EXCEPTION CASE 1 — semdelete() called while blocked in swait:
 *    semdelete() sets sstate = S_FREE *before* waking all waiting processes.
 *    It then enters a loop: while (scount++ < 0) { ready(getfirst(queue)); }
 *    This increments scount once per waiter and moves each to the ready list.
 *    When the swait process resumes from resched(), it performs its own
 *    scount++ (the undo step), pushing scount one above what semdelete left.
 *    This extra increment is harmless: the semaphore is S_FREE so no one
 *    will ever allocate it or inspect its count again.  On the very next
 *    loop iteration, the re-validation check (sstate == S_FREE) detects the
 *    deleted semaphore and returns SYSERR to the caller.
 *
 *  EXCEPTION CASE 2 — semreset() called while blocked in swait:
 *    semreset() removes all waiters with getfirst() (without adjusting
 *    scount) and then sets scount = new_count unconditionally.  When the
 *    swait process resumes and does its undo scount++, the count becomes
 *    new_count + 1 — one higher than intended.  On the next loop iteration:
 *    - The semaphore is still S_USED, so validation passes.
 *    - If both counts are now > 0 the process acquires them (scount--
 *      brings the count back to new_count, which is the correct post-acquire
 *      value).
 *    - If a count is still <= 0 the process re-blocks and retries.
 *    In either path the transient +1 is immediately corrected; the final
 *    observable semaphore state is always consistent.
 *
 *  Returns: OK on success, SYSERR if either semaphore argument is invalid.
 *------------------------------------------------------------------------
 */
syscall swait(sid32 semA, sid32 semB) {
  intmask mask;           /* Saved interrupt mask         */
  struct procent *prptr;  /* Ptr to current process entry */
  struct sentry *semAptr; /* Ptr to semA table entry      */
  struct sentry *semBptr; /* Ptr to semB table entry      */

  while (1) {

    mask = disable(); /* Disable interrupts for atomicity */

    /* --- Validate both semaphore arguments --- */
    /* Both IDs must be in range, both entries must be allocated (S_USED),
     * and they must identify two different semaphores.  If semA == semB
     * the double-decrement in the success path would corrupt the count. */
    if (isbadsem(semA) || isbadsem(semB) || semA == semB) {
      restore(mask);
      return SYSERR;
    }
    semAptr = &semtab[semA];
    semBptr = &semtab[semB];
    if (semAptr->sstate == S_FREE || semBptr->sstate == S_FREE) {
      restore(mask);
      return SYSERR;
    }

    /* --- Check if both semaphores are available --- */
    /* Both counts must be strictly > 0.  Decrement atomically (interrupts
     * are still disabled), restore, and return. */
    if (semAptr->scount > 0 && semBptr->scount > 0) {
      semAptr->scount--;
      semBptr->scount--;
      restore(mask);
      return OK;
    }

    /* --- Block on the first semaphore that is not available --- */
    /* "First" means semA takes priority: if semA's count is < 1 we block
     * there; otherwise semB must be the unavailable one. */
    prptr = &proctab[currpid];

    if (semAptr->scount < 1) {
      /* Decrement scount to register this process as a waiter, set
       * process state, enqueue on semA's wait queue, and yield CPU. */
      semAptr->scount--;
      prptr->prstate = PR_WAIT;
      prptr->prsem = semA;
      enqueue(currpid, semAptr->squeue);
      resched(); /* Process suspends here; returns when woken */

      /* Undo the decrement: we are no longer queued on this semaphore.
       * The waking signal already incremented scount once; our undo
       * nets to: we consumed the signal and leave scount unchanged.
       * See exception case notes in the header for semdelete/semreset. */
      semAptr->scount++;
    } else {
      /* semA was available; semB is not */
      semBptr->scount--;
      prptr->prstate = PR_WAIT;
      prptr->prsem = semB;
      enqueue(currpid, semBptr->squeue);
      resched();

      semBptr->scount++; /* Undo — symmetric to the semA path above */
    }

    restore(mask);
    /* Retry: re-validate and re-check both counts from the top */
  }
}
