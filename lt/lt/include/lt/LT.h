/******************************************************************************
 * <lt/LT.h>                   Main header file for the RokuLT Operating System
 *               ____
 *              / __ \ ____   __ __ __  __   __ _______
 *             / /_/ // __ \ / //_// / / /  / //__  __/
 *            / _, _// /_/ // ,<  / /_/ /  / /_  / /
 *           /_/ |_| \____//_/|_\ \__,_/  /____//_/
 *                      Real Time Operating System
 *
 * Min Spec: 100 Mhz CPU Core, 64 KiB RAM
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_LT_H
#define ROKU_LT_INCLUDE_LT_LT_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

int LT_Run(int argc, const char ** argv);
    /**<
     * Runs the LT Operating System
     *
     * LT_Run initializes the operating system, creates a thread at lowest thread
     * priority and opens and runs the user's genesis library Run() function in the
     * context of that thread.  LT_Run blocks until OS completion which occurs when
     * all threads spawned in the system have exited.  LT_Run returns the return
     * code returned by the genesis library's Run() function.
     *
     * The first non-switched argument starting with argv[1] is taken as the name
     * of the genesis library.
     *
     * @see LT_GetCore, LTCore_Terminate */

LTCore * LT_GetCore(void) LT_ISR_SAFE;
    /**<
     * Gets the globally available LTCore library interface.
     *
     * Gets the LTCore library interface the primary interface of LT.  The LTCore library
     * is globally available starting from the initial load of the Genesis library
     * until LT operating system shutdown has concluded.  If LT_GetCore() is
     * called from within an LT thread, it always returns a valid LTCore *.
     * If called from within an interrupt handler, the return value should
     * be checked for NULL in the event the interrupt handler is invoked
     * before OS initialization is complete or after OS shutdown has concluded.
     *
     * @see LT_GetCore, LTCore_Terminate */

LT_EXTERN_C_END
#include <lt/core/LTCore.h>

#endif // #ifndef ROKU_LT_INCLUDE_LT_LT_H

/******************************************************************************
*   LOG
*******************************************************************************
 *  19-Apr-20   augustus    LT C version
 *  31-May-20   augustus    LT_GetCore() doesn't return const
*/
