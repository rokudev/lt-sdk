/*******************************************************************************
 * <lt/driver/fs/LTDriverFS.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * Defines standard LTObject API for file system drivers
 *
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DRIVER_FS_LTDRIVERFS_H
#define LT_INCLUDE_LT_DRIVER_FS_LTDRIVERFS_H

#include <lt/LTTypes.h>
#include <lt/system/fs/LTFile.h>

LT_EXTERN_C_BEGIN

typedef bool LTDriverFS_TraversalCallback(LTFile *directory, LTFile *child, void *clientData);

typedef_LTObject(LTDriverFS, 1) {
    void (*SetName)(LTDriverFS *file, const char *name);
    void (*GetName)(LTDriverFS *file, LTString *stringToFillWithName);
    // void (*GetVolumeName)(LTDriverFS *file, LTString *stringToFillWithName);

    bool (*Exists)(LTDriverFS *file);
    bool (*IsFile)(LTDriverFS *file);
    bool (*IsDirectory)(LTDriverFS *file);
    bool (*IsOpen)(LTDriverFS *file);

    // bool (*Copy)(LTDriverFS *sourceFile, LTDriverFS *destFile, bool bOverwrite);
    // bool (*Move)(LTDriverFS *sourceFile, LTDriverFS *destFile, bool bOverwrite);
    bool (*DeleteSingle)(LTDriverFS *file);
    bool (*MakeDirectory)(LTDriverFS *file, bool bMakeFullPath);
    bool (*TraverseDirectory)(LTDriverFS                   *driverFile,
                              LTFile                       *originalDirectoryFile,
                              LTDriverFS_TraversalCallback *traversalCallback,
                              void                         *clientData);

    u64  (*GetSize)(LTDriverFS *file);
    u64  (*GetPosition)(LTDriverFS *file);
    void (*SeekToPosition)(LTDriverFS *file, u64 position);
    // u32  (*GetBlockSize)(LTDriverFS *file);

    bool (*Open)(LTDriverFS *file, bool bCreate);
    u32  (*Read)(LTDriverFS *file, u32 numBytes, u8 *buffer);
    u32  (*Write)(LTDriverFS *file, u32 numBytes, const u8 *bytes);
    void (*Close)(LTDriverFS *file);
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef LT_INCLUDE_LT_DRIVER_FS_LTDRIVERFS_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  09-Nov-23   aurelian    created
 */

