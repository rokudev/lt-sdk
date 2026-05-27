/*******************************************************************************
 * source/lt/system/fs/LTMemoryFile.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/system/fs/LTFile.h>

DEFINE_LTLOG_SECTION("memfile");

enum {
    // Slightly under 4k bytes to account for memory allocator overhead+headroom (32 bytes) and 4k page size
    kBufferChainBlockSize = 4064 - sizeof(LTBufferChain),
};

/*___________________________________________
  LTMemoryFile object private data members */

typedef_LTObjectImpl(LTFile, LTMemoryFile) {
    LTBufferChain *head;
    LTBufferChain *current;
    u32            readWriteHeadRelativeToCurrent;
    bool           bOpen;
} LTOBJECT_API;

/* ______________________
  Forward declarations */
static void LTMemoryFile_Close(LTMemoryFile *file);
static void LTMemoryFile_Delete(LTMemoryFile *file, bool bRecursive);

/*___________________________________
  LTMemoryFile object constructors */

static bool LTMemoryFile_ConstructObject(LTMemoryFile *file) {
    LT_UNUSED(file);
    return true;
}

static void LTMemoryFile_DestructObject(LTMemoryFile *file) {
    /* Close and delete the file */
    if (file->bOpen) LTMemoryFile_Close(file);
    LTMemoryFile_Delete(file, false);
}

/*________________________________
  LTMemoryFile helper functions */

static LTBufferChain *MallocBufferChainEntry(u32 nBytes) {
    LTBufferChain *buf = lt_malloc(sizeof(LTBufferChain) + nBytes);
    if (!buf) return NULL;

    /* Init new buffer chain entry */
    buf->next      = NULL;
    buf->size      = kBufferChainBlockSize;
    buf->bytesUsed = 0;
    // Point buffer to the block allocated after initial structure
    buf->buffer = (u8 *)&buf[1];
    return buf;
}

static bool ExtendBufferChain(LTMemoryFile *file, u32 nBytes) {
    file->current->next = MallocBufferChainEntry(nBytes);
    if (!file->current->next) return false;
    return true;
}

static void FreeBufferChain(LTBufferChain *head) {
    /* Enumerate through chain and free every entry */
    LTBufferChain *current = head;
    while (current != NULL) {
        LTBufferChain *next = current->next;
        lt_free(current);
        current = next;
    }
}

/*____________________________________
  LTMemoryFile public api functions */

static void LTMemoryFile_SetName(LTMemoryFile *file, const char *name) {
    LT_UNUSED(file);
    LT_UNUSED(name);
    LTLOG_YELLOWALERT("setname", "SetName() currently unsupported for LTMemoryFile!");
}

static void LTMemoryFile_GetName(LTMemoryFile *file, LTString *stringToFillWithName) {
    LT_UNUSED(file);
    LT_UNUSED(stringToFillWithName);
    LTLOG_YELLOWALERT("getname", "GetName() currently unsupported for LTMemoryFile!");
}

static void LTMemoryFile_SetBaseDirectory(LTMemoryFile *file, const char *name) {
    LT_UNUSED(file);
    LT_UNUSED(name);
    LTLOG_YELLOWALERT("setbasedir", "SetBaseDirectory() currently unsupported for LTMemoryFile!");
}

static void LTMemoryFile_GetBaseDirectory(LTMemoryFile *file, LTString *stringToFillWithName) {
    LT_UNUSED(file);
    LT_UNUSED(stringToFillWithName);
    LTLOG_YELLOWALERT("getbasedir", "GetBaseDirectory() currently unsupported for LTMemoryFile!");
}

static void LTMemoryFile_GetVolumeName(LTMemoryFile *file, LTString *stringToFillWithName) {
    LT_UNUSED(file);
    LT_UNUSED(stringToFillWithName);
    LTLOG_YELLOWALERT("getvolname", "GetVolumeName() currently unsupported for LTMemoryFile!");
}

static bool LTMemoryFile_Exists(LTMemoryFile *file) {
    return file->head != NULL;
}

static bool LTMemoryFile_IsFile(LTMemoryFile *file) {
    LT_UNUSED(file);
    // LTMemoryFile is currently ALWAYS a file
    return true;
}

static bool LTMemoryFile_IsDirectory(LTMemoryFile *file) {
    LT_UNUSED(file);
    return false;
}

static bool LTMemoryFile_IsOpen(LTMemoryFile *file) {
    return file->bOpen;
}

static bool LTMemoryFile_Copy(LTMemoryFile *sourceFile, LTMemoryFile *destFile) {
    // TODO: This implementation should be shared with LTDeviceFS?
    LT_UNUSED(sourceFile);
    LT_UNUSED(destFile);
    LTLOG_YELLOWALERT("copy", "Copy() unimplemented for LTMemoryFile!");
    return false;
}

static bool LTMemoryFile_Move(LTMemoryFile *sourceFile, LTMemoryFile *destFile) {
    // TODO: This implementation should be shared with LTDeviceFS?
    LT_UNUSED(sourceFile);
    LT_UNUSED(destFile);
    LTLOG_YELLOWALERT("move", "Move() unimplemented for LTMemoryFile!");
    return false;
}

static void LTMemoryFile_Delete(LTMemoryFile *file, bool bRecursive) {
    if (bRecursive) {
        LTLOG_YELLOWALERT("delete.recursive", "Recursive Delete() currently unsupported for LTMemoryFile!");
        return;
    }
    /* Free entire buffer chain and reset member variables */
    if (!file->head) return; // No buffer chain exists
    FreeBufferChain(file->head);
    file->head                           = NULL;
    file->current                        = NULL;
    file->readWriteHeadRelativeToCurrent = 0;
    file->bOpen                          = false;
}

static bool LTMemoryFile_MakeDirectory(LTMemoryFile *file, bool bMakeFullPath) {
    LT_UNUSED(file);
    LT_UNUSED(bMakeFullPath);
    return false;
}

static bool LTMemoryFile_TraverseDirectory(LTMemoryFile             *directoryFile,
                                           LTFile_TraversalCallback *traversalCallback,
                                           void                     *data) {
    LT_UNUSED(directoryFile);
    LT_UNUSED(traversalCallback);
    LT_UNUSED(data);
    return false;
}

static u64 LTMemoryFile_GetSize(LTMemoryFile *file) {
    if (!file->head) return 0;
    u64 totalSize = 0;

    /* Enumerate all entries in the chain and sum their size */
    LTBufferChain *current = file->head;
    while (current != NULL) {
        totalSize += current->bytesUsed;
        current    = current->next;
    }
    return totalSize;
}

static u64 LTMemoryFile_GetPosition(LTMemoryFile *file) {
    if (!file->bOpen || !file->current) return 0;
    u64 position = 0;

    /* Find the position of start of file->current */
    LTBufferChain *current = file->head;
    while (current != file->current) {
        position += current->bytesUsed;
        current   = current->next;
    }

    /* Add relative position to obtain the overall position */
    return position + file->readWriteHeadRelativeToCurrent;
}

static void LTMemoryFile_SeekToPosition(LTMemoryFile *file, u64 position) {
    if (!file->bOpen || !file->current) return;
    u64 distanceToPosition = position;

    /* Enumerate through chain to calculate position */
    LTBufferChain *current = file->head;
    while (distanceToPosition > current->bytesUsed) {
        // Decrement distance by amount traversed
        distanceToPosition -= current->bytesUsed;
        // No next entry to advance to, end
        if (current->next == NULL) break;
        // Move on to next entry, if it exists
        current = current->next;
    }

    /* If position is out of bounds, position will be set to the last position in the last chain entry */
    distanceToPosition = distanceToPosition > file->current->size ? file->current->size : distanceToPosition;

    /* Update relative position in buffer chain */
    file->current                        = current;
    file->readWriteHeadRelativeToCurrent =  distanceToPosition;
}

static u32 LTMemoryFile_GetBlockSize(LTMemoryFile *file) {
    LT_UNUSED(file);
    return kBufferChainBlockSize;
}

static bool LTMemoryFile_Open(LTMemoryFile *file, bool bCreate) {
    /* If bCreate is enabled, always delete and re-allocate internal buffer */
    if (bCreate) {
        if (file->head) LTMemoryFile_Delete(file, false);
        file->head = MallocBufferChainEntry(kBufferChainBlockSize);
        if (!file->head) return false;
        file->current = file->head;
        file->readWriteHeadRelativeToCurrent = 0;
        file->bOpen = true;
        return true;
    }
    
    /* Else, open will only succeed if internal buffer was previously allocated */
    file->bOpen = (file->head != NULL); 
    return file->bOpen;
}

static u32 LTMemoryFile_Read(LTMemoryFile *file, u32 numBytes, u8 *buffer) {
    if (!file->bOpen || !file->current) return 0;

    /* Read from current position until all bytes are read OR end of chain is reached */
    u32 unreadBytes = numBytes;
    while (unreadBytes > 0) {
        // Read bytes from file into destination buffer, up to the end of the current entry
        u32 bytesUntilEnd = file->current->bytesUsed - file->readWriteHeadRelativeToCurrent;
        u32 toRead        = LT_MIN(unreadBytes, bytesUntilEnd);
        if (toRead > 0) {
            lt_memcpy(buffer + (numBytes - unreadBytes),
                      file->current->buffer + file->readWriteHeadRelativeToCurrent,
                      toRead);
            file->readWriteHeadRelativeToCurrent += toRead;
            unreadBytes                          -= toRead;
        }
        
        // All bytes read, exit early
        if (unreadBytes == 0) break;

        // Current was the last entry. No more possible bytes to read.
        if (!file->current->next) break;

        // Else, advance to the next entry and continue reading
        file->current                        = file->current->next;
        file->readWriteHeadRelativeToCurrent = 0;
    }

    /* Return number of bytes read */
    return numBytes - unreadBytes;
}

static u32 LTMemoryFile_Write(LTMemoryFile *file, u32 numBytes, const u8 *bytes) {
    if (!file->bOpen || !file->current) return 0;

    /* Write from current position until all bytes are written. There are 3 cases to cover:
     * 1. Overwrite existing data
     * 2. Overwrite some existing data and append data to the end of the file
     * 3. Append data to the end of the file */
    u32 unwrittenBytes = numBytes;
    while (unwrittenBytes > 0) {
        // Starting from the current position, write bytes UP TO the end of the current buffer chain entry
        u32 bytesUntilEnd = file->current->size - file->readWriteHeadRelativeToCurrent;
        u32 toWrite       = LT_MIN(unwrittenBytes, bytesUntilEnd);
        if (toWrite > 0) {
            lt_memcpy(file->current->buffer + file->readWriteHeadRelativeToCurrent,
                    bytes + (numBytes - unwrittenBytes),
                    toWrite);
            file->readWriteHeadRelativeToCurrent += toWrite; // Advance write head by the number of extra bytes written
            unwrittenBytes                       -= toWrite;
        }

        // If the write head was moved past the previous number of bytes used in the current buffer, 
        // update the bytesUsed counter for the current buffer
        file->current->bytesUsed = LT_MAX(file->current->bytesUsed, file->readWriteHeadRelativeToCurrent);
        LT_ASSERT(file->current->bytesUsed <= file->current->size);
 
        // All bytes written, exit early
        if (unwrittenBytes == 0) break;

        // Allocate next buffer if it does not exist, since there are still more bytes to write
        if (!file->current->next) {
            bool success = ExtendBufferChain(file, kBufferChainBlockSize);
            if (!success) break;
        }

        // Advance to next buffer to continue write
        file->current                        = file->current->next;
        file->readWriteHeadRelativeToCurrent = 0;
    }

    /* Return number of bytes written */
    return numBytes - unwrittenBytes;
}

static void LTMemoryFile_Close(LTMemoryFile *file) {
    LT_UNUSED(file);
    file->bOpen = false;
}

/*______________________________________
  LTMemoryFile LTObjectApi definition */

define_LTObjectImplPublic(LTFile, LTMemoryFile,
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
 *  21-May-24   aurelian    created
 */
