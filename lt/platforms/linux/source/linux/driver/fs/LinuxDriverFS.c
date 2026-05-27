/*******************************************************************************
 * platforms/linux/source/linux/driver/fs/LinuxDriverFS.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/driver/fs/LTDriverFS.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

DEFINE_LTLOG_SECTION("linux.fs")
#define PLOG(...)
// #define PLOG(...) LTLOG_DEBUG(__VA_ARGS__)

/*  ___________________________
 *  Object private data members
 */
typedef_LTObjectImpl(LTDriverFS, LinuxDriverFSImpl) {
    LTString filename;
    int      fd;
} LTOBJECT_API;

static bool LinuxDriverFSImpl_Open(LinuxDriverFSImpl *file, bool bCreate);
static void LinuxDriverFSImpl_Close(LinuxDriverFSImpl *file);

/*  _________________________________
 *  Object constructor and destructor
 */
static void LinuxDriverFSImpl_DestructObject(LinuxDriverFSImpl *file) {
    if (file->fd > 0) LinuxDriverFSImpl_Close(file);
    ltstring_destroy(file->filename);
}

static bool LinuxDriverFSImpl_ConstructObject(LinuxDriverFSImpl *file) {
    do {
        file->filename = ltstring_create("");
        if(!file->filename) break;
        return true;
    } while(false);

    LTLOG_YELLOWALERT("init.fail", NULL);
    LinuxDriverFSImpl_DestructObject(file);
    return false;
}

/*  ________________________________
 *  Object public API implementation
 */
static void LinuxDriverFSImpl_SetName(LinuxDriverFSImpl *file, const char *name) {
    ltstring_set(&file->filename, name);
}

static void LinuxDriverFSImpl_GetName(LinuxDriverFSImpl *file, LTString *stringToFillWithName) {
    ltstring_set(stringToFillWithName, file->filename);
}

static bool LinuxDriverFSImpl_Exists(LinuxDriverFSImpl *file) {
    return access(file->filename, F_OK) == 0;
}

static bool LinuxDriverFSImpl_IsFile(LinuxDriverFSImpl *file) {
    struct stat statbuf;
    if (fstat(file->fd, &statbuf) < 0) {
        LTLOG_DEBUG("isfile.stat", "errno: %d", errno);
        return false;
    }
    return S_ISREG(statbuf.st_mode);
}

static bool LinuxDriverFSImpl_IsDirectory(LinuxDriverFSImpl *file) {
    struct stat statbuf;
    if (fstat(file->fd, &statbuf) < 0) {
        LTLOG_DEBUG("isdir.stat", "errno: %d", errno);
        return false;
    }
    return S_ISDIR(statbuf.st_mode);
}

static bool LinuxDriverFSImpl_IsOpen(LinuxDriverFSImpl *file) {
    return file->fd > 0;
}

static bool LinuxDriverFSImpl_DeleteSingle(LinuxDriverFSImpl *file) {
    /* Close file before attempting to delete */
    if (file->fd > 0) LinuxDriverFSImpl_Close(file);

    /* Delete the file or empty directory. First assume target is a file. */
    int ret = unlink(file->filename);
    if (ret < 0) {
        // Not a file, assume it is an empty directory
        if (errno == EISDIR) {
            ret = rmdir(file->filename);
            // Success, don't log any errors
            if (ret == 0) return true;
            LTLOG_DEBUG("delete.rmdir.err", "errno: %d", errno);
        } else {
            LTLOG_DEBUG("delete.unlink.err", "errno: %d", errno);
        }
        return false;
    }
    return true;
}

static bool LinuxDriverFSImpl_MakeDirectory(LinuxDriverFSImpl *file, bool bMakeFullPath) {
    if (bMakeFullPath) {
        char *path = file->filename;
        for (char *p = path+1; *p; p++) {
            if (*p == '/') {
                *p = 0;
                int ret = mkdir(path, S_IRWXU);
                if (ret < 0 && errno != EEXIST) {
                    LTLOG_DEBUG("mkdir.fullpath.err", "errno: %d", errno);
                    // make sure string is back to normal before return
                    *p = '/';
                    return false;
                }
                *p = '/';
            }
        }
    } else {
        int ret = mkdir(file->filename, S_IRWXU);
        if (ret < 0 && errno != EEXIST) {
            LTLOG_DEBUG("mkdir.nofullpath.err", "errno: %d", errno);
            return false;
        }
    }
    return true;
}

static void ConcatFilePath(LTString directoryName, LTString fileName, LTString *out) {
    u32         directoryNameLength = lt_strlen(directoryName);
    const char *dividingSlash = directoryName[directoryNameLength - 1] == '/' ? "" : "/";
    ltstring_format(out, "%s%s%s", directoryName, dividingSlash, fileName);
}

static bool LinuxDriverFSImpl_TraverseDirectory(LinuxDriverFSImpl            *driverFile,
                                                LTFile                       *directory,
                                                LTDriverFS_TraversalCallback *traversalCallback,
                                                void                         *clientData) {
    bool success = false;

    /* Open the directory. If the file is not a directory (or other error), abort traversal */
    DIR *dir = fdopendir(driverFile->fd);
    if (dir == NULL) {
        LTLOG_DEBUG("opendir.err", "errno: %d", errno);
        goto finish;
    }

    /* Traverse every file in directory and call callback */
    struct dirent *entry;
    while (errno = 0, (entry = readdir(dir))) {
        PLOG("traverse", "Found file or directory: %s\n", entry->d_name);

        // Skip "." and ".." files
        if (lt_strcmp(entry->d_name, ".") == 0 || lt_strcmp(entry->d_name, "..") == 0) continue;

        // File or directory encountered, load name into LTFile object
        LTFile *child = lt_createobject(LTFile);

        LTString directoryName = ltstring_create("");
        directory->API->GetName(directory, &directoryName);

        LTString childFilePath = ltstring_create("");
        ConcatFilePath(directoryName, entry->d_name, &childFilePath);

        child->API->SetName(child, childFilePath);

        ltstring_destroy(directoryName);
        ltstring_destroy(childFilePath);

        // Pass child file to callback
        bool continueTraversal = traversalCallback(directory, child, clientData);
        lt_destroyobject(child);
        if (!continueTraversal) {
            goto finish;
        }
    }
    /* Readdir() error, end traversal */
    if (errno != 0) {
        LTLOG_DEBUG("readdir.err", "errno: %d", errno);
        goto finish;
    }
    /* Made it to end, traversal was successful */
    success = true;

finish:
    if (dir == NULL || closedir(dir) < 0) {
        LTLOG_DEBUG("closedir.err", "errno: %d", errno);
        success = false;
    } else {
        // closedir() success, update internal fd since closedir() also closes the fd passed into fdopendir()
        driverFile->fd = -1;
    }
    return success;
}

static u64 LinuxDriverFSImpl_GetSize(LinuxDriverFSImpl *file) {
    struct stat buf;
    fstat(file->fd, &buf);
    off_t size = buf.st_size;
    if (size < 0) {
        LTLOG_DEBUG("get.size.err", "errno: %d", errno);
        return 0;
    }
    return (u64)size;
}

static void LinuxDriverFSImpl_SeekToPosition(LinuxDriverFSImpl *file, u64 position) {
    off_t pos;
    if (position > LinuxDriverFSImpl_GetSize(file)) {
        // lseek() DOES allow seeking past the end of the file,
        // but LTFile will not allow this
        pos = lseek(file->fd, 0, SEEK_END);
    } else {
        pos = lseek(file->fd, position, SEEK_SET);
    }
    if (pos < 0) {
        LTLOG_DEBUG("seek.pos.err", "errno: %d", errno);
    }
}

static u64 LinuxDriverFSImpl_GetPosition(LinuxDriverFSImpl *file) {
    off_t pos = lseek(file->fd, 0, SEEK_CUR);
    if (pos < 0) {
        LTLOG_DEBUG("get.pos.err", "errno: %d", errno);
        return 0;
    }
    return (u64)pos;
}

static bool LinuxDriverFSImpl_Open(LinuxDriverFSImpl *file, bool bCreate) {
    if (bCreate) {
        // Make the parent directories if needed
        LinuxDriverFSImpl_MakeDirectory(file, true);
        // Then create the file
        file->fd = open(file->filename, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
    } else {
        file->fd = open(file->filename, O_RDWR);
        // If errno indicates file was a directory, open the directory with O_RDONLY to get a valid fd
        if (file->fd < 0 && errno == EISDIR) {
            file->fd = open(file->filename, O_RDONLY);
        }
    }
    PLOG("file.open", "Opened: %d | %s ", file->fd, file->filename);
    if (file->fd < 0) {
        LTLOG_DEBUG("open.err", "errno %d", errno);
        return false;
    }
    return true;
}

static u32 LinuxDriverFSImpl_Read(LinuxDriverFSImpl *file, u32 numBytes, u8 *buffer) {
    if (file->fd <= 0) {
        LTLOG_DEBUG("read.fd.err", "file had invalid fd: %d", file->fd);
        return 0;
    }
    ssize_t readAmt = read(file->fd, buffer, numBytes);
    if (readAmt < 0) {
        LTLOG_DEBUG("read.err", "errno %d", errno);
        return 0;
    }
    return (u32)readAmt;
}

static u32 LinuxDriverFSImpl_Write(LinuxDriverFSImpl *file, u32 numBytes, const u8 *bytes){
    if (file->fd <= 0) {
        LTLOG_DEBUG("write.fd.err", "file had invalid fd: %d", file->fd);
        return 0;
    }
    ssize_t written = write(file->fd, bytes, numBytes);
    if (written < 0) {
        LTLOG_DEBUG("write.err",
                    "errno %d, for file: %s, fd: %d, tried to write numBytes: %lu",
                    errno,
                    file->filename,
                    file->fd,
                    LT_Pu32(numBytes));
        return 0;
    }
    return (u32)written;
}

static void LinuxDriverFSImpl_Close(LinuxDriverFSImpl *file){
    if (file->fd <= 0) return;
    PLOG("file.close", "Closing: %d | %s ", file->fd, file->filename);

    int ret = fsync(file->fd);
    if (ret < 0) LTLOG_DEBUG("fsync.err", "errno %d", errno);
    ret = close(file->fd);
    if (ret < 0) LTLOG_DEBUG("close.err", "errno %d", errno);

    file->fd = -1;
}

/*  ________________________________________________________
 *  Object API definition and library root interface binding
 */
define_LTObjectImplPublic(LTDriverFS, LinuxDriverFSImpl,
    SetName,
    GetName,
    // GetVolumeName,
    Exists,
    IsFile,
    IsDirectory,
    IsOpen,
    // Copy,
    // Move,
    DeleteSingle,
    MakeDirectory,
    TraverseDirectory,
    GetSize,
    GetPosition,
    SeekToPosition,
    // GetBlockSize,
    Open,
    Read,
    Write,
    Close
);

define_LTOBJECT_EXPORTLIBRARY(
    LinuxDriverFS, 1,
    LinuxDriverFSImpl
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  09-Nov-23   aurelian    created
 */
