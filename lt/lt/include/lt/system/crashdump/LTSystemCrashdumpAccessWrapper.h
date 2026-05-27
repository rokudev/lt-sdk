/*******************************************************************************
 * lt/system/crashdump/LTSystemCrashdumpAccessWrapper.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_CRASHDUMP_LTSYSTEMCRASHDUMPACCESSWRAPPER_H_
#define ROKU_LT_INCLUDE_LT_SYSTEM_CRASHDUMP_LTSYSTEMCRASHDUMPACCESSWRAPPER_H_

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/* LTSystemCrashdumpCrashdumpAccessWrapper
    A temporary solution which allows the use of existing LTSystemCrashdumpPlugin instances
    with different methods to access crashdump data.  The wrapper provides access to crashdump data,
    either directly through LTSystemCrashdump or via some other means, e.g., collecting crashdump
    data through an IPC buffer from another core.

    One implementation of this wrapper simply passes through calls to LTSystemCrashdump.
    The wrapper is likely to be removed when the crashdump plugin architecture is reworked to better
    separate crashdump collection and crashdump upload.  That rework is likely to entail either
    breaking apart collection and upload into separate Libraries, or changing the LTSystemCrashdump
    interface to an LTObject which would provide polymorphic access to the crashdump data.
*/

typedef_LTObject(LTSystemCrashdumpAccessWrapper, 1) {

    /* This is necessarily a verbatim copy of the LTSystemCrashdump root interface,
       with the addition of the instance pointer. */

    LT_SIZE      (* GetSize)(LTSystemCrashdumpAccessWrapper *instance);
        /**< Obtains the size of the crash dump, if any is available.
         *   @param[in]  instance pointer to the LTSystemCrashdumpAccessWrapper instance
         *   @return     size of the available crash dump in bytes,
         *               0 if a crash dump is not available. */

    const char * (* GetVersion)(LTSystemCrashdumpAccessWrapper *instance);
        /**< Obtains the version of the LT firmware that generated a crash dump.
         *   @param[in]  instance pointer to the LTSystemCrashdumpAccessWrapper instance
         *   @return     pointer to a null-terminated character array.
         *               The array can be empty if a crashdump is available but a version is not,
         *               or NULL, if there is no crashdump available.
         *
         *   LTSystemCrashdump library ensures that the version string is valid and does not change
         *   from the first call that returns non-NULL string until the crashdump is erased or this
         *   library is closed. The caller must copy the string if it is needed after any of these
         *   events. */

    LT_SIZE      (* GetChunk)(LTSystemCrashdumpAccessWrapper *instance, void *pBuffer, LT_SIZE nBufferSize, LT_SIZE nOffset);
        /**< Copies a part of the crash dump to the provided buffer.
         *   @param[in]  instance pointer to the LTSystemCrashdumpAccessWrapper instance
         *   @param[in]  pBuffer     buffer provided by the caller.
         *   @param[in]  nBufferSize size of the provided buffer in bytes.
         *   @param[in]  nOffset     offset in bytes within the core dump from where the data is
         *                           copied to the buffer.
         *   @return     number of bytes, lower or equal to \a nBufferSize bytes, that are copied to
         *               the buffer. If \a nOffset is equal or larger than the size of the available
         *               crashdump, this function returns 0. */

    bool         (* Erase)(LTSystemCrashdumpAccessWrapper *instance);
        /**< Erases the crashdump from flash.
         *   @param[in]  instance pointer to the LTSystemCrashdumpAccessWrapper instance
         *   @return true if all the content is erased, false if any part of the crash dump is not
         *           successfully erased. */

} LTOBJECT_API;

LT_EXTERN_C_END

#endif  // ROKU_LT_INCLUDE_LT_SYSTEM_CRASHDUMP_LTSYSTEMCRASHDUMPACCESSWRAPPER_H_

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  18-Aug-25   constantine created
 */
