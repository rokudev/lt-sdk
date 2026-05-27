/******************************************************************************
 * lt/source/core/LTCoreDebugHelpers.h
 *
 * This file contains juicy #defines that enable dulce debugging features for
 * LOCAL BUILDS ONLY!  (Don't ever check-in changes to this file).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTCOREDEBUGHELPERS_H
#define ROKU_LT_SOURCE_LT_CORE_LTCOREDEBUGHELPERS_H

/* ______________________________________________
  / LT THREAD: RESOLVING STACK OVERFLOW CRASHES /
 /                                  \|/       */
#define LTTHREAD_STACK_FUDGE_BYTES  (0) /*<- CHECK IN WITH 0 */
/*__________________________________/|\ ALWAYS RESTORE THIS VALUE TO 0 (ZERO) BEFORE CHECKING IN !!
/
   TIP: If you are crashing with Stack Overflow errors,
        set LTTHREAD_STACK_FUDGE_BYTES to 1000.  This will cause
        every LTThread created to get an additional 1000 bytes of
        stack space.  Keep increasing this number until the crash resolves.
        Then use the ps command to examine the max stack of each thread.
        If any thread's max stack exceeds the thread's prior stack size
        (new StackSize - 1000 in this example) then that is the culprit thread.
        Increase that thread stack size using the following equation:
        newGoodStackSize = INT((maxStackFromTest+127)/64) * 64.  This will give
        you a stack size of the measured max taken up to the next 64 byte boundary (if not
        already on one), plus 64 bytes, i.e. a minimum add of 64 bytes guaranteed to fall
        on a 64 byte size boundary. (Do the aforementioned calculation in your head, on paper,
        or on a calculator and enter a constant literal into the code; no need to be calculating
        this at runtime. Then restore LTTHREAD_STACK_FUDGE_BYTES to ZERO and re-test.  Repeat as necessary.
*/

/* __________________________________________________________________________________
  / LT_DEBUG MODE: ACCOMMODATING INCREASED STACK SIZE REQUIREMENTS IN LT_DEBUG MODE /
 /                                               \\||//                           */
#define LTTHREAD_STACK_LT_DEBUG_INCREASAL_BYTES  (1000) /*<- space invader       */
/*_______________________________________________//||\\__________________________/
   TIP: If everything works fine in release mode, but you get stack overflow crashes
        in debug mode, then adjust LT_DEBUG_LTTHREAD_STACK_INCREASAL_BYTES.  This
        increasal is additive to LTTHREAD_STACK_FUDGE_BYTES. */

/*____________________________________________________________________
 / LT LOG: DUMPING RAM USAGE WITH EVERY LOG MESSAGE                  /
/                                                                 \|/  */
#define LTLOG_AUTOLOG_DUMP_RAM_STATS                              (0) /*<- CHECK IN WITH 0; valid values are 0 and 1 */
/*________________________________________________________________/|\ ALWAYS RESTORE THIS VALUE TO 0 (ZERO) BEFORE CHECKING IN !!
   TIP: If you would like to monitor ram usage as the system is spewing
   log messages, er as the system is running, then set LTLOG_AUTOLOG_DUMP_RAM_STATS
   to 1. Remember, when done marveling at the spew, er debugging,
         restore LTLOG_AUTOLOG_DUMP_RAM_STATS back to 0 (ZERO).
 */

/*____________________________________________________________________
 / LT LOG: DUMPING THREAD NAME WITH EVERY LOG MESSAGE                /
/                                                                 \|/  */
#define LTLOG_AUTOLOG_PREPEND_THREAD_NAME                         (0) /*<- valid values are 0 and 1 */
/*________________________________________________________________/|\ ALWAYS RESTORE THIS VALUE TO 0 (ZERO) BEFORE CHECKING IN !!
   TIP: If you would like to know what log messages are being generated
   by which threads, then this #define is for you!  Set LTLOG_AUTOLOG_PREPEND_THREAD_NAME
   to have the thread name of the thread performing a log prepended to the log message.
   Remeber, as ever, when done debugging, restore LTLOG_AUTOLOG_PREPEND_THREAD_NAME
   to 0 (ZERO).  Note: Don has received special dispensation from "The Council" to
   check in this file with LTLOG_AUTOLOG_PREPEND_THREAD_NAME set to 1, just to see if
   anyone notices.  Remember, after performing a log, restore LTLOG_AUTOLOG_PREPEND_THREAD_NAME
   back to 0 (ZERO) before checking in.
*/

/*____________________________________________________________________
 / LT CORE: TRACING HEAP ALLOCATIONS                                 /
/                                                                 \|/  */
#define LTCORE_TRACE_HEAP_ALLOCATIONS                             (0) /*<- valid values are 0 and 1 */
/*________________________________________________________________/|\ ALWAYS RESTORE THIS VALUE TO 0 (ZERO) BEFORE CHECKING IN !!
   TIP: If you would like to have extra visibility into heap usage including which blocks
   are allocated, which thread allocated them, and the source file and line number at which
   they were allocated, then remove LTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING in
   platform/Makefile.config, and set LTCORE_TRACE_HEAP_ALLOCATIONS to 1. Doing so will enable an
   extended memstat shell command which will display information about each block that has been
   allocated. This information can be very helpful for understanding memory usage and debugging
   memory leaks. When done debugging, set LTCORE_TRACE_HEAP_ALLOCATIONS back to 0 (ZERO).
*/

/*____________________________________________________________________
 / LT CORE: LTCORE_BREAK_ON_HEAP_ERRORS                                     /
/                                                                 \|/  */
#define LTCORE_BREAK_ON_HEAP_ERRORS                               (0) /*<- valid values are 0 and 1 */
/*________________________________________________________________/|\ ALWAYS RESTORE THIS VALUE TO 0 (ZERO) BEFORE CHECKING IN !!
   TIP: If you would like for a debug breakpoint to be generated when a heap allocation
   error occurs, then remove LTCORE_ELIDE_ALL_LTCORE_HEAPTRACKING in
   platform/Makefile.config, and set LTCORE_BREAK_ON_HEAP_ERRORS to 1.
*/

/*____________________________________________________________________
 / LT CORE: LTCORE_PATTERN_FILL_FREED_HEAP_MEMORY                                   /
/                                                                 \|/  */
#define LTCORE_PATTERN_FILL_FREED_HEAP_MEMORY                     (0) /*<- valid values are 0 and 1 */
/*________________________________________________________________/|\ ALWAYS RESTORE THIS VALUE TO 0 (ZERO) BEFORE CHECKING IN !!
   TIP: If you would like to overwrite the contents of all freed heap memory blocks with
   a debugging pattern (0xFEFEFEFE...) that simplifies the detection of attempts to use
   memory after it has been freed, then set LTCORE_PATTERN_FILL_FREED_HEAP_MEMORY to 1.
*/

/*____________________________________________________________________
 / LT CORE: ASSERT ISR UNSAFE APIs                                   /
/                                                                 \|/  */
#define LTCORE_ASSERT_IF_ISR_CALLS_NON_LT_ISR_SAFE_FUNCTION       (0) /*<- valid values are 0 and 1 */
/*________________________________________________________________/|\
   TIP: If you would like to raise an LT_ASSERT whenever an
        ISR calls a function that is not LT_ISR_SAFE, then set
        LTCORE_ASSERT_IF_ISR_CALLS_NON_LT_ISR_SAFE_FUNCTION to 1.
*/

/*____________________________________________________________________
 / LT CORE: LTCORE_LOG_SLEEP_MODE_ACTIVITY                           /
/                                                                 \|/  */
#define LTCORE_LOG_SLEEP_MODE_ACTIVITY                            (0) /*<- valid values are 0 and 1 */
/*________________________________________________________________/|\
   TIP: If you would like to see information relating to the
        activity of sleep mode logged to the console, then set
        LTCORE_LOG_SLEEP_MODE_ACTIVITY to 1.  WARNING: verbose!
*/

#endif /* #ifndef ROKU_LT_SOURCE_LT_CORE_LTCOREDEBUGHELPERS_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  10-Apr-21   augustus    created
 *  10-Apr-25   augustus    renamed standby mode to sleep mode
*/
