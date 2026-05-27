/******************************************************************************
 * <lt/system/crashdump/LTSystemCrashdump.h> LTSystemCrashdump library interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_CRASHDUMP_LTSYSTEMCRASHDUMP_H_
#define ROKU_LT_INCLUDE_LT_SYSTEM_CRASHDUMP_LTSYSTEMCRASHDUMP_H_

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/*___________________________________
_/ LTSystemCrashdump Library Interface */
/*
 * LTDeviceFlash Usage Note:
 *
 *    LTSystemCrashdump uses LTDeviceFlash to store the crash dump and the logs leading up to the crash.
 *    The crashdump plugin (e.g., LTSystemCrashdumpPluginBinary) writes the crash dump and log data to
 *    to partitions designated to store such data upon a crash, assert, or any code which calls
 *    DebugBreak().  On most platforms, the crashdump plugin writes to these partitions from an exception
 *    context, wherein interrupts are disabled and normal threading and scheduling functionality is
 *    not available.  On some platforms, normal flash-write and flash-erase operations depend on
 *    interrupts to coordinate flash operations with other system operations, and are therefore not
 *    suited for use in exception context.  In such cases, the platform must provide alternate functions
 *    for LTSystemCrashdump and the LTSystemCrashdumpPlugin to use in order to perform flash-write and
 *    flash-erase operations from exception context.  The platform differentiates between normal and
 *    exception-context flash operations by providing two Device Units[1] for the underlying flash
 *    storage, one (Device Unit index 0) for normal-context use and another (Device Unit index 1) for
 *    the exception context.  On platforms that require no differentiation, the two Device Units
 *    refer to the same functions and operate identically.
 *
 *    LTSystemCrashdump obtains a handle to both of these Device Units, using one or the other as
 *    appropriate:
 *                 - All public-API functions use the normal-context Device Unit.
 *                 - All exception-context functions use the exception-context Device Unit, even
 *                   if that Device Unit provides functionality identical to the normal context.
 *
 *    [1] When LTDeviceFlash is reworked to provide access through LTObjects, the two individual Device
 *        Units provided for differentiation will be replaced by two LTDeviceFlash objects.
 */

typedef_LTLIBRARY_ROOT_INTERFACE(LTSystemCrashdump, 1) {
    LT_SIZE (* GetSize)(void);
        /**< Obtains the size of the crash dump, if any is available.
         *   @return     size of the available crash dump in bytes,
         *               0 if a crash dump is not available. */

    const char * (* GetVersion)(void);
        /**< Obtains the version of the LT firmware that generated a crash dump.
         *   @return     pointer to a null-terminated character array.
         *               The array can be empty if a crashdump is available but a version is not,
         *               or NULL, if there is no crashdump available.
         *
         *   LTSystemCrashdump library ensures that the version string is valid and does not change
         *   from the first call that returns non-NULL string until the crashdump is erased or this
         *   library is closed. The caller must copy the string if it's need after any of these
         *   events. */

    LT_SIZE      (* GetChunk)(void *pBuffer, LT_SIZE nBufferSize, LT_SIZE nOffset);
        /**< Copies a part of the crash dump to the provided buffer.
         *   @param[in]  pBuffer     buffer provided by the caller.
         *   @param[in]  nBufferSize size of the provided buffer in bytes.
         *   @param[in]  nOffset     offset in bytes within the core dump from where the data is
         *                           copied to the buffer.
         *   @return     number of bytes, lower or equal to \a nBufferSize bytes, that are copied to
         *               the buffer. If \a nOffset is equal or larger than the size of the available
         *               crashdump, this function returns 0. */

    bool         (* Erase)(void);
        /**< Erases the crashdump from flash.
         *   @return true if all the content is erased, false if any part of the crash dump is not
         *           successfully erased. */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif /* #define ROKU_LT_INCLUDE_LT_SYSTEM_CRASHDUMP_LTSYSTEMCRASHDUMP_H_ */
