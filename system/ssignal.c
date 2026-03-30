/* ssignal.c - ssignal */
/* Author 1: Nikhil Pembadi */
/* Author 2: S.Hruthik */
#include <xinu.h>

/*------------------------------------------------------------------------
 *  ssignal  -  Signal two semaphores simultaneously (AND synchronization).
 *
 *  Increments the count of both semaphores and, for each one, moves the
 *  longest-waiting process (if any) from WAIT state to READY state.
 *  Rescheduling is deferred until both semaphores have been signaled so
 *  that no context switch can occur between the two operations.
 *
 *  This is the release counterpart to swait: a process that acquired both
 *  semaphores via swait calls ssignal to release both atomically.
 *
 *  Returns: OK on success, SYSERR if either semaphore argument is invalid.
 *------------------------------------------------------------------------
 */
syscall ssignal(sid32 semA, sid32 semB) {
  intmask mask;           /* Saved interrupt mask    */
  struct sentry *semAptr; /* Ptr to semA table entry */
  struct sentry *semBptr; /* Ptr to semB table entry */

  mask = disable();

  /* Validate: both IDs in range, both allocated, and distinct */
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

  /* Defer rescheduling so BOTH semaphores are fully signaled before any
   * context switch can occur.  Without deferral, the ready() call inside
   * the first signal block could immediately switch to a higher-priority
   * process, leaving the second semaphore un-signaled.  resched_cntl uses
   * the same DEFER mechanism as signaln.c. */
  resched_cntl(DEFER_START);

  /* Signal semA: increment count; if it was negative a process was waiting,
   * dequeue it and move it to the ready list. */
  if ((semAptr->scount++) < 0) {
    pid32 pidA = dequeue(semAptr->squeue);
    if (pidA != EMPTY) { /* Guard: queue should never be empty here,
                          * but check defensively to avoid bad ready() */
      ready(pidA);
    }
  }

  /* Signal semB: same logic as semA above. */
  if ((semBptr->scount++) < 0) {
    pid32 pidB = dequeue(semBptr->squeue);
    if (pidB != EMPTY) {
      ready(pidB);
    }
  }

  /* Both semaphores signaled; allow the scheduler to run now. */
  resched_cntl(DEFER_STOP);

  restore(mask);
  return OK;
}
