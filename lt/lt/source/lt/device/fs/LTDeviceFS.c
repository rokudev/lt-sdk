/*******************************************************************************
 * source/lt/device/fs/LTDeviceFS.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTArray.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/fs/LTDeviceFS.h>

static const LT_SIZE COPY_BUF_SIZE = 4096;

DEFINE_LTLOG_SECTION("device.fs")

/*  ___________________________
 *  Object private data members
 */
typedef_LTObjectImpl(LTDeviceFS, LTDeviceFSImpl) {
    LTDriverFS *driverFS;
    // LTString baseDir;
    // LTString filenameNoBase;
} LTOBJECT_API;

static bool LTDeviceFSImpl_Open(LTDeviceFSImpl *file, bool bCreate);
static void LTDeviceFSImpl_Close(LTDeviceFSImpl *file);
static bool LTDeviceFSImpl_TraverseDirectory(LTDeviceFSImpl               *deviceFile,
                                             LTFile                       *originalDirectoryFile,
                                             LTDeviceFS_TraversalCallback *traversalCallback,
                                             void                         *clientData);

/*  _________________________________
 *  Object constructor and destructor
 */
static void LTDeviceFSImpl_DestructObject(LTDeviceFSImpl *file) {
    lt_destroyobject(file->driverFS);
}

static bool LTDeviceFSImpl_ConstructObject(LTDeviceFSImpl *file) {
    // Open the file driver object using info obtained from DeviceConfig
    do {
        LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
        if (!deviceConfig) break;

        const char *driverName = deviceConfig->GetDriverAt("LTDeviceFS", 0);
        if (!driverName) break;
        u32 driverSection = deviceConfig->GetDriverSection("LTDeviceFS", driverName);
        if (!driverSection) break;
        const char *driverObjName = deviceConfig->ReadString(driverSection, "obj");
        if (!driverName) break;
        file->driverFS = (LTDriverFS *) lt_createobject_named("LTDriverFS", driverObjName);
        if (!file->driverFS) break;

        lt_closelibrary(deviceConfig);
        return true;
    } while(false);

    LTLOG_YELLOWALERT("init.fail", NULL);
    LTDeviceFSImpl_DestructObject(file);
    return false;
}

/*  _________________
 *  Helper functions
 */

// If given "/foo/bar/" , with any variation of trailing/leading slashes, returns "foo"
static void GetBaseDirName(const char *inputName, LTString *stringToFillWithName) {
    LTString name = ltstring_create(inputName);
    // Read from beginning / (if it exists) to next /,
    // then set that / to be null terminator
    for (char *p = name[0] == '/' ? name + 1 : name ; *p; p++) {
        if (*p == '/') {
            *p = 0;
            break;
        }
    }
    // This string ignores the first slash if it exists, and will end at the new null terminator
    ltstring_set(stringToFillWithName, name[0] == '/' ? name + 1 : name);
    ltstring_destroy(name);
}

// Get everything after the "second" slash (ignores leading slash whether or not it exists)
static void GetFilenameNoBase(LTDeviceFSImpl *file, LTString *stringToFillWithName) {
    LTString currentName = ltstring_create("");
    file->API->GetName((LTDeviceFS *)file, &currentName);
    char *fileDirPos = lt_strchr(currentName + 1, '/');  // Search AFTER initial slash (if any)
    if (fileDirPos == NULL) fileDirPos = currentName;
    else ++fileDirPos;
    ltstring_set(stringToFillWithName, fileDirPos);
    ltstring_destroy(currentName);
}

static void FreePointerArrayStrings(LTArray *list) {
    while (list->API->GetCount(list)) {
        LTString stringElement = list->API->Get(list, 0, NULL);
        list->API->Remove(list, 0);
        ltstring_destroy(stringElement);
    }
}

/*  ________________________________
 *  Object public API implementation
 */
static LTDriverFS* LTDeviceFSImpl_GetDriverFS(LTDeviceFSImpl *file) {
    return file->driverFS;
}

static void LTDeviceFSImpl_SetName(LTDeviceFSImpl *file, const char *name) {
    file->driverFS->API->SetName(file->driverFS, name);
}

static void LTDeviceFSImpl_GetName(LTDeviceFSImpl *file, LTString *stringToFillWithName) {
    file->driverFS->API->GetName(file->driverFS, stringToFillWithName);
}

static void LTDeviceFSImpl_SetBaseDirectory(LTDeviceFSImpl *file, const char *name) {
    LTString baseDirName = NULL;
    GetBaseDirName(name, &baseDirName);
    LTString filename = NULL;
    GetFilenameNoBase(file, &filename);

    // format and set new name
    LTString newName = NULL;
    ltstring_format(&newName, "/%s/%s", baseDirName, filename);
    file->API->SetName((LTDeviceFS *) file, newName);

    // Cleanup
    ltstring_destroy(newName);
    ltstring_destroy(filename);
    ltstring_destroy(baseDirName);
}

static void LTDeviceFSImpl_GetBaseDirectory(LTDeviceFSImpl *file, LTString *stringToFillWithName) {
    LTString currentName = ltstring_create("");
    file->API->GetName((LTDeviceFS *)file, &currentName);
    GetBaseDirName(currentName, stringToFillWithName);
    ltstring_destroy(currentName);
}

static void LTDeviceFSImpl_GetVolumeName(LTDeviceFSImpl *file, LTString stringToFillWithName) {
    LT_UNUSED(file);
    LT_UNUSED(stringToFillWithName);
}

static bool LTDeviceFSImpl_Exists(LTDeviceFSImpl *file) {
    return file->driverFS->API->Exists(file->driverFS);
}

static bool LTDeviceFSImpl_IsFile(LTDeviceFSImpl *file) {
    return file->driverFS->API->IsFile(file->driverFS);
}

static bool LTDeviceFSImpl_IsDirectory(LTDeviceFSImpl *file) {
    return file->driverFS->API->IsDirectory(file->driverFS);
}

static bool LTDeviceFSImpl_IsOpen(LTDeviceFSImpl *file) {
    return file->driverFS->API->IsOpen(file->driverFS);
}

static bool LTDeviceFSImpl_Copy(LTDeviceFSImpl *sourceFile, LTDeviceFSImpl *destFile) {
    // Open both files (assumes they were provided as closed files)
    if (!sourceFile->API->Open((LTDeviceFS *)sourceFile, false)) {
        LTLOG_YELLOWALERT("copy.source.open.err", "could not open source file");
        return false;
    }
    if(!destFile->API->Open((LTDeviceFS *)destFile, true)) {
        LTLOG_YELLOWALERT("copy.dest.open.err", "could not open dest file");
        sourceFile->API->Close((LTDeviceFS *)sourceFile);
        return false;
    }

    u64 sourceLen = sourceFile->API->GetSize((LTDeviceFS *)sourceFile);
    u64 bytesToRead = sourceLen;
    u8 *bytesReadBuf = lt_malloc(COPY_BUF_SIZE);
    if (!bytesReadBuf) {
        LTLOG_YELLOWALERT("copy.OOM", "no memory for copy buffer");
        sourceFile->API->Close((LTDeviceFS *)sourceFile);
        destFile->API->Close((LTDeviceFS *)destFile);
        return false;
    }
    // Read source data into destination
    u8 maxConsecutiveEmptyWrites = 5;
    u8 numConsecutiveEmptyWrites = 0;
    while (bytesToRead > 0) {
        sourceFile->API->SeekToPosition((LTDeviceFS *)sourceFile, sourceLen - bytesToRead);
        u32 bytesRead = sourceFile->API->Read((LTDeviceFS *)sourceFile, COPY_BUF_SIZE, bytesReadBuf);
        u32 bytesWritten = destFile->API->Write((LTDeviceFS *)destFile, bytesRead, bytesReadBuf);
        bytesToRead -= bytesWritten;

        // Safety to ensure thread does not get trapped in loop on write failure
        if (bytesWritten == 0) {
            ++numConsecutiveEmptyWrites;
        } else {
            numConsecutiveEmptyWrites = 0;
        }
        if (numConsecutiveEmptyWrites > maxConsecutiveEmptyWrites) {
            LTLOG_YELLOWALERT("copy.write.err", "too many consecutive empty writes, abort");
            sourceFile->API->Close((LTDeviceFS *)sourceFile);
            destFile->API->Close((LTDeviceFS *)destFile);
            lt_free(bytesReadBuf);
            return false;
        }
    }
    lt_free(bytesReadBuf);

    // Close files if files came in closed
    sourceFile->API->Close((LTDeviceFS *)sourceFile);
    destFile->API->Close((LTDeviceFS *)destFile);

    return true;
}

static bool LTDeviceFSImpl_Move(LTDeviceFSImpl *sourceFile, LTDeviceFSImpl *destFile) {
    LT_UNUSED(sourceFile);
    LT_UNUSED(destFile);
    return false;
}

typedef struct {
    LTDeviceFSImpl *deviceFile;
    LTArray        *stack;
    LTArray        *filesToDelete;
} TraverseAndMarkForDeletionArgs;

static bool TraverseAndMarkForDeletion(LTFile *directory, LTFile *child, void *clientData) {
    LT_UNUSED(directory);
    TraverseAndMarkForDeletionArgs *args = clientData;

    /* If file is a directory, push its name onto the stack so it can be traversed next */
    child->API->Open(child, false);
    if (child->API->IsDirectory(child)) {
        LTString dirName = ltstring_create("");
        child->API->GetName(child, &dirName);
        args->stack->API->Insert(args->stack, 0, dirName);
    }
    /* If file is a non-directory, mark to be deleted after traversal is complete */
    else {
        LTString childName = ltstring_create("");
        child->API->GetName(child, &childName);
        args->filesToDelete->API->Append(args->filesToDelete, childName);
    }
    child->API->Close(child);

    return true;
}

/* Delete all files and subdirectories in a directory by using a modified depth-first search. The stack is used to
 * keep track of subdirectories to be explored and deleted */
static void DeleteRecursive(LTDeviceFSImpl *deviceFile, LTFile *directory) {
    /* Create a stack to store traversal progress and a list to store files marked for deletion */
    LTArray  *stack         = lt_createobject_typed(LTArray, List); // Directories to be traversed and deleted
    LTArray  *filesToDelete = lt_createobject_typed(LTArray, List); // Non-directory files to be deleted after every traversal

    /* Push current directory onto the stack */
    LTString baseDirName = ltstring_create("");
    directory->API->GetName(directory, &baseDirName);
    stack->API->Insert(stack, 0, baseDirName);

    /* When stack is empty, all directories will have been deleted */
    LTFile *currentFile = lt_createobject(LTFile);
    while (stack->API->GetCount(stack)) {
        /* Peek at top of stack and load directory name into an LTFile */
        u32      sizeBeforeTraversal = stack->API->GetCount(stack);
        LTString currentDirName      = stack->API->Get(stack, 0, NULL);
        currentFile->API->SetName(currentFile, currentDirName);

        /* Traverse the directory!
         * 1. Push child directories onto stack
         * 2. Mark child files for deletion (don't delete during traversal)
         * If Open() fails or Traverse() does not reach the end, abort */
        TraverseAndMarkForDeletionArgs args = {deviceFile, stack, filesToDelete};
        if (!currentFile->API->Open(currentFile, false)) break;
        if (!currentFile->API->TraverseDirectory(currentFile, &TraverseAndMarkForDeletion, &args)) break;
        currentFile->API->Close(currentFile);

        /* Delete all non-directory files discovered during traversal */
        bool deleteFailed = false;
        while (filesToDelete->API->GetCount(filesToDelete) && !deleteFailed) {
            LTString filename = filesToDelete->API->Get(filesToDelete, 0, NULL);
            filesToDelete->API->Remove(filesToDelete, 0);
            currentFile->API->SetName(currentFile, filename);
            currentFile->API->Delete(currentFile, false); // Delete single non-directory file
            ltstring_destroy(filename);
            deleteFailed = currentFile->API->Exists(currentFile);
        }
        // If a file failed to delete, end recursive deletion
        if (deleteFailed) break;

        /* No new directories were discovered during traversal and non-directory files have been deleted (above), so directory
         * is expected to be empty. Pop directory name from stack and delete the directory if it is empty */
        currentFile->API->SetName(currentFile, currentDirName);
        if (sizeBeforeTraversal == stack->API->GetCount(stack)) {
            stack->API->Remove(stack, 0);
            ltstring_destroy(currentDirName);

            // NOTE: The following Delete() will route into LTDeviceFSImpl_Delete().
            // Do NOT perform recursive deletion!
            currentFile->API->Delete(currentFile, false);
            // If directory failed to delete, end recursive deletion
            if (currentFile->API->Exists(currentFile)) break;
        }
    }

    /* Clean up. Accounts for failure cases. */
    currentFile->API->Close(currentFile);
    lt_destroyobject(currentFile);
    FreePointerArrayStrings(stack);
    lt_destroyobject(stack);
    FreePointerArrayStrings(filesToDelete);
    lt_destroyobject(filesToDelete);
}

static void LTDeviceFSImpl_Delete(LTDeviceFSImpl *deviceFile, LTFile *wrapperFile, bool bRecursive) {
    /* Assume file is a normal file or empty directory */
    if (deviceFile->driverFS->API->DeleteSingle(deviceFile->driverFS)) return;

    /* Recursive deletion */
    if (!bRecursive) return;

    // Open and verify file is a directory before attempting recursive deletion
    if (!LTDeviceFSImpl_IsOpen(deviceFile)) {
        bool openSuccess = LTDeviceFSImpl_Open(deviceFile, false);
        if (!openSuccess) return;  // Silently fail
    }
    bool isDirectory = LTDeviceFSImpl_IsDirectory(deviceFile);
    LTDeviceFSImpl_Close(deviceFile);

    // File is a directory. Perform recursive deletion!
    if (isDirectory)
        DeleteRecursive(deviceFile, wrapperFile);
}

static bool LTDeviceFSImpl_MakeDirectory(LTDeviceFSImpl *file, bool bMakeFullPath) {
    return file->driverFS->API->MakeDirectory(file->driverFS, bMakeFullPath);
}

static bool LTDeviceFSImpl_TraverseDirectory(LTDeviceFSImpl               *deviceFile,
                                             LTFile                       *originalDirectoryFile,
                                             LTDeviceFS_TraversalCallback *traversalCallback,
                                             void                         *clientData) {
    return deviceFile->driverFS->API->TraverseDirectory(deviceFile->driverFS,
                                                        originalDirectoryFile,
                                                        traversalCallback,
                                                        clientData);
}

static u64  LTDeviceFSImpl_GetSize(LTDeviceFSImpl *file) {
    return file->driverFS->API->GetSize(file->driverFS);
}

static u64  LTDeviceFSImpl_GetPosition(LTDeviceFSImpl *file) {
    return file->driverFS->API->GetPosition(file->driverFS);
}

static void LTDeviceFSImpl_SeekToPosition(LTDeviceFSImpl *file, u64 position) {
    file->driverFS->API->SeekToPosition(file->driverFS, position);
}

static u32  LTDeviceFSImpl_GetBlockSize(LTDeviceFSImpl *file) {
    LT_UNUSED(file);
    return 0;
}

static bool LTDeviceFSImpl_Open(LTDeviceFSImpl *file, bool bCreate) {
    // TODO: should bCreate=true also automatically make the file path?
    // file->driverFS->API->MakeDirectory(file->driverFS, true);

    // As safety, don't open the file if it's already open
    if (file->driverFS->API->IsOpen(file->driverFS)) return false;
    return file->driverFS->API->Open(file->driverFS, bCreate);
}

static u32  LTDeviceFSImpl_Read(LTDeviceFSImpl *file, u32 numBytes, u8 *buffer) {
    return file->driverFS->API->Read(file->driverFS, numBytes, buffer);
}

static u32  LTDeviceFSImpl_Write(LTDeviceFSImpl *file, u32 numBytes, const u8 *bytes) {
    return file->driverFS->API->Write(file->driverFS, numBytes, bytes);
}

static void LTDeviceFSImpl_Close(LTDeviceFSImpl *file) {
    file->driverFS->API->Close(file->driverFS);
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTDeviceFS, LTDeviceFSImpl,
    GetDriverFS,
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

define_LTOBJECT_EXPORTLIBRARY(
    LTDeviceFS, 1,
    LTDeviceFSImpl
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  10-Nov-23   aurelian    created
 */
