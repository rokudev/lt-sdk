/*******************************************************************************
 * source/lt/system/fs/LTFile.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/system/fs/LTFile.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/fs/LTDeviceFS.h>

/*_____________________________________
  LTFile object private data members */
typedef_LTObjectImpl(LTFile, LTFileImpl) {
    LTDeviceFS *deviceFS;
} LTOBJECT_API;

/*_____________________________
  LTFile object constructors */
static bool LTFileImpl_ConstructObject(LTFileImpl *file) {
    file->deviceFS = lt_createobject(LTDeviceFS);
    if (!file->deviceFS) return false;

    return true;
}

static void LTFileImpl_DestructObject(LTFileImpl *file) {
    lt_destroyobject(file->deviceFS);
}

/*_________________________________
  LTFile public api functions */
static void LTFileImpl_SetName(LTFileImpl *file, const char *name) {
    file->deviceFS->API->SetName(file->deviceFS, name);
}

static void LTFileImpl_GetName(LTFileImpl *file, LTString *stringToFillWithName) {
    file->deviceFS->API->GetName(file->deviceFS, stringToFillWithName);
}

static void LTFileImpl_SetBaseDirectory(LTFileImpl *file, const char *name) {
    file->deviceFS->API->SetBaseDirectory(file->deviceFS, name);
}

static void LTFileImpl_GetBaseDirectory(LTFileImpl *file, LTString *stringToFillWithName) {
    file->deviceFS->API->GetBaseDirectory(file->deviceFS, stringToFillWithName);
}

static void LTFileImpl_GetVolumeName(LTFileImpl *file, LTString *stringToFillWithName) {
    file->deviceFS->API->GetVolumeName(file->deviceFS, stringToFillWithName);
}

static bool LTFileImpl_Exists(LTFileImpl *file) {
    return file->deviceFS->API->Exists(file->deviceFS);
}

static bool LTFileImpl_IsFile(LTFileImpl *file) {
    return file->deviceFS->API->IsFile(file->deviceFS);
}

static bool LTFileImpl_IsDirectory(LTFileImpl *file) {
    return file->deviceFS->API->IsDirectory(file->deviceFS);
}

static bool LTFileImpl_IsOpen(LTFileImpl *file) {
    return file->deviceFS->API->IsOpen(file->deviceFS);
}

static bool LTFileImpl_Copy(LTFileImpl *sourceFile, LTFileImpl *destFile) {
    return sourceFile->deviceFS->API->Copy(sourceFile->deviceFS, destFile->deviceFS);
}

static bool LTFileImpl_Move(LTFileImpl *sourceFile, LTFileImpl *destFile) {
    return sourceFile->deviceFS->API->Move(sourceFile->deviceFS, destFile->deviceFS);
}

static void LTFileImpl_Delete(LTFileImpl *file, bool bRecursive) {
    file->deviceFS->API->Delete(file->deviceFS, (LTFile *)file, bRecursive);
}

static bool LTFileImpl_MakeDirectory(LTFileImpl *file, bool bMakeFullPath) {
    return file->deviceFS->API->MakeDirectory(file->deviceFS, bMakeFullPath);
}

static bool LTFileImpl_TraverseDirectory(LTFileImpl               *directoryFile,
                                         LTFile_TraversalCallback *traversalCallback,
                                         void                     *data) {
    /* The current LTFile must be passed all the way down to the driver level in order
     * for the driver to call the callback function with the original LTFile argument. Since calling down to the device
     * and driver layers involves "unwrapping" the LTFile object, the original LTFile should be passed down as well.
     * Hence, the strange function signature in LTDeviceFS and LTDriverFS */
    return directoryFile->deviceFS->API->TraverseDirectory(directoryFile->deviceFS,
                                                           (LTFile *)directoryFile,
                                                           traversalCallback,
                                                           data);
}

static u64  LTFileImpl_GetSize(LTFileImpl *file) {
    return file->deviceFS->API->GetSize(file->deviceFS);
}

static u64  LTFileImpl_GetPosition(LTFileImpl *file) {
    return file->deviceFS->API->GetPosition(file->deviceFS);
}

static void LTFileImpl_SeekToPosition(LTFileImpl *file, u64 position) {
    file->deviceFS->API->SeekToPosition(file->deviceFS, position);
}

static u32  LTFileImpl_GetBlockSize(LTFileImpl *file) {
    return file->deviceFS->API->GetBlockSize(file->deviceFS);
}

static bool LTFileImpl_Open(LTFileImpl *file, bool bCreate) {
    return file->deviceFS->API->Open(file->deviceFS, bCreate);
}

static u32  LTFileImpl_Read(LTFileImpl *file, u32 numBytes, u8 *buffer) {
    return file->deviceFS->API->Read(file->deviceFS, numBytes, buffer);
}

static u32  LTFileImpl_Write(LTFileImpl *file, u32 numBytes, const u8 *bytes) {
    return file->deviceFS->API->Write(file->deviceFS, numBytes, bytes);
}

static void LTFileImpl_Close(LTFileImpl *file) {
    file->deviceFS->API->Close(file->deviceFS);
}

/*_____________________________
  LTFile LTObjectApi definition */
define_LTObjectImplPublic(LTFile, LTFileImpl,
    SetName,
    GetName,
    SetBaseDirectory,
    GetBaseDirectory,
    GetVolumeName,
    Exists,
    IsFile,
    IsDirectory,
    IsOpen,
    Copy,
    Move,
    Delete,
    MakeDirectory,
    TraverseDirectory,
    GetSize,
    GetPosition,
    SeekToPosition,
    GetBlockSize,
    Open,
    Read,
    Write,
    Close
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jun-23   augustus    created
 *  10-Nov-23   aurelian    partial implementation
 */
