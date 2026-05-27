/*******************************************************************************
 * source/lt/system/fs/LTSystemFS.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/system/fs/LTFile.h>

/*_____________________________________
  Library Initialization and Cleanup */
static bool LTSystemFSImpl_LibInit(void) {
    return true;
}

static void LTSystemFSImpl_LibFini(void) {
}

/*____________________________________________
  LTSystemFS library root interface binding */
typedef_LTLIBRARY_ROOT_INTERFACE(LTSystemFS, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTSystemFS)     LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTSystemFS,
    (LTFileImpl)
    (LTMemoryFile)
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jun-23   augustus    created
 */
