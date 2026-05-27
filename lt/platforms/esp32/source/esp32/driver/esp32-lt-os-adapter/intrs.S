
#include <xtensa/hal.h>
#include <xtensa/config/core.h>
/*
-------------------------------------------------------------------------------
  unsigned int xt_ints_on ( unsigned int mask )

  Enables a set of interrupts. Does not simply set INTENABLE directly, but
  computes it as a function of the current virtual priority if XT_USE_SWPRI is
  enabled.
  Can be called from interrupt handlers.
-------------------------------------------------------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
*/

    .text
    .align  4
    .global xt_ints_on
    .type   xt_ints_on,@function

xt_ints_on:
    entry   sp, 0x10
#if XCHAL_HAVE_INTERRUPTS
    movi    a3, 0
    xsr     a3, INTENABLE        /* Disables all interrupts   */
    rsync
    or      a2, a3, a2           /* set bits in mask */
    wsr     a2, INTENABLE        /* Re-enable ints */
    rsync
    mov     a2, a3               /* return prev mask */
#else
    movi    a2, 0                /* Return zero */
#endif
    retw
    .size   xt_ints_on, . - xt_ints_on


/*
-------------------------------------------------------------------------------
  unsigned int xt_ints_off ( unsigned int mask )

  Disables a set of interrupts. Does not simply set INTENABLE directly,
  but computes it as a function of the current virtual priority if XT_USE_SWPRI is
  enabled.
  Can be called from interrupt handlers.
-------------------------------------------------------------------------------
*/

    .text
    .align  4
    .global xt_ints_off
    .type   xt_ints_off,@function

xt_ints_off:
    entry   sp, 0x10
#if XCHAL_HAVE_INTERRUPTS
    movi    a4, 0
    xsr     a4, INTENABLE        /* Disables all interrupts   */
    rsync
    or      a3, a4, a2           /* set bits in mask */
    xor     a3, a3, a2           /* invert bits in mask set in mask, essentially clearing them */
    wsr     a3, INTENABLE        /* Re-enable ints */
    rsync
    mov     a2, a4               /* return prev mask */
#else
    movi    a2, 0                /* return zero */
#endif
    retw

    .size   xt_ints_off, . - xt_ints_off
