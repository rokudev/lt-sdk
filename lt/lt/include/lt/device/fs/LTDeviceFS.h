/*******************************************************************************
 * <lt/device/fs/LTDeviceFS.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * Defines device-level LTObject API for using LTDriverFS
 *
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_FS_LTDEVICEFS_H
#define LT_INCLUDE_LT_DEVICE_FS_LTDEVICEFS_H

#include <lt/LTTypes.h>
#include <lt/driver/fs/LTDriverFS.h>
#include <lt/system/fs/LTFile.h>

LT_EXTERN_C_BEGIN

typedef bool LTDeviceFS_TraversalCallback(LTFile *directory, LTFile *child, void *clientData);

typedef_LTObject(LTDeviceFS, 1) {
    LTDriverFS *(*GetDriverFS)(LTDeviceFS *file);

    void (*SetName)(LTDeviceFS *file, const char *name);
    void (*GetName)(LTDeviceFS *file, LTString *stringToFillWithName);
    void (*SetBaseDirectory)(LTDeviceFS *file, const char *name);
    void (*GetBaseDirectory)(LTDeviceFS *file, LTString *stringToFillWithName);
    void (*GetVolumeName)(LTDeviceFS *file, LTString *stringToFillWithName);

    bool (*Exists)(LTDeviceFS *file);
    bool (*IsFile)(LTDeviceFS *file);
    bool (*IsDirectory)(LTDeviceFS *file);
    bool (*IsOpen)(LTDeviceFS *file);

    bool (*Copy)(LTDeviceFS *sourceFile, LTDeviceFS *destFile);
    bool (*Move)(LTDeviceFS *sourceFile, LTDeviceFS *destFile);
    void (*Delete)(LTDeviceFS *deviceFile, LTFile *wrapperFile, bool bRecursive);
    bool (*MakeDirectory)(LTDeviceFS *file, bool bMakeFullPath);
    bool (*TraverseDirectory)(LTDeviceFS                   *deviceFile,
                              LTFile                       *originalDirectoryFile,
                              LTDeviceFS_TraversalCallback *traversalCallback,
                              void                         *clientData);

    u64  (*GetSize)(LTDeviceFS *file);
    u64  (*GetPosition)(LTDeviceFS *file);
    void (*SeekToPosition)(LTDeviceFS *file, u64 position);
    u32  (*GetBlockSize)(LTDeviceFS *file);

    bool (*Open)(LTDeviceFS *file, bool bCreate);
    u32  (*Read)(LTDeviceFS *file, u32 numBytes, u8 *buffer);
    u32  (*Write)(LTDeviceFS *file, u32 numBytes, const u8 *bytes);
    void (*Close)(LTDeviceFS *file);

} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef LT_INCLUDE_LT_DEVICE_FS_LTDEVICEFS_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  09-Nov-23   aurelian    created
 */

