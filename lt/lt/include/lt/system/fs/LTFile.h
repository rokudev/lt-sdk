/*******************************************************************************
 * <lt/system/fs/LTFile.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/
/** @file LTFile.h header for LTFile object.
  *
  * @note Certain filesystem implementations may not support the full functionality
  *       of this API such as directories, copying or moving files, seeking
  *       during writes, etc.
  */

#ifndef LT_INCLUDE_LT_SYSTEM_FS_LTFILE_H
#define LT_INCLUDE_LT_SYSTEM_FS_LTFILE_H

#include <lt/LTObject.h>
#include <lt/core/LTCore.h>

LT_EXTERN_C_BEGIN

typedef struct LTFile LTFile;

typedef bool LTFile_TraversalCallback(LTFile *directory, LTFile *child, void *clientData);

/**< Callback type for TraverseDirectory
 *
 * @param directory LTFile that contains the name of a directory from SetName(). This is the same LTFile object passed
 * into TraverseDirectory()
 * @param child LTFile with the name set to file or directory encountered during enumeration.
 * @param clientData user supplied client data
 * @return true to continue the directory enumeration to the next file. False to end the directory
 * enumeration at the most recent file.
 * @see TraverseDirectory
 */

/*_________________
 / LTFile Object */
typedef_LTObject(LTFile, 1) {

    void (*SetName)(LTFile *file, const char *name);
    /**< Set the absolute file path and filename.
     *
     * @note Only sets the name. Open() still needs to be called to create or open the file.
     * @param file LTFile object
     * @param name absolute path and name of the file
     */

    void (*GetName)(LTFile *file, LTString *stringToFillWithName);
    /**< Get the absolute file path and filename
     *
     * @param[in] file LTFile object
     * @param[out] stringToFillWithName empty LTString to hold the read out name
     */

    void (*SetBaseDirectory)(LTFile *file, const char *name);
    /**< Set the name of base directory. If the name of the file
     * is /foo/bar/baz.txt, the /foo/ is the base directory.
     *
     * @param file LTFile object
     * @param name can contain leading/trailing slashes. For example, "/basedir/", "basedir", "/basedir", "basedir/" are
     * all OK
     */

    void (*GetBaseDirectory)(LTFile *file, LTString *stringToFillWithName);
    /**< Get the name of base directory. If the name of the file
     * is /foo/bar/baz.txt, then /foo/ is the base directory.
     *
     * @param[in] file LTFile object
     * @param[out] stringToFillWithName returns the name of the directory
     * with no leading or trailing slashes. If the name of the file
     * is /foo/bar/baz.txt, the string will be "foo".
     */

    void (*GetVolumeName)(LTFile *file, LTString *stringToFillWithName);
    /**< Get the volume name of the underlying filesystem.
     *
     * @param[in] file LTFile object
     * @param[out] stringToFillWithName returns the name of the volume.
     */

    bool (*Exists)(LTFile *file);
    /**< Check whether or not a file exists. The file should have a path and name set by SetName().
     *
     * @param file LTFile object. The file does not need to be open.
     * @return true if file exists, false otherwise
     */

    bool (*IsFile)(LTFile *file);
    /**< Check if the file referenced by the name is a file or not.
     *
     * @param file LTFile object that has been Open()'d
     * @returns true if the LTFile is a file, false otherwise
     */

    bool (*IsDirectory)(LTFile *file);
    /**< Check if the file referenced by the name is directory or not.
     *
     * @param file LTFile object that has been Open()'d
     * @return true if the LTFile is a directory, false otherwise
     */

    bool (*IsOpen)(LTFile *file);
    /**< Check to see if the LTFile object refers to a currently open file or directory.
     * NOTE: This function does NOT indicate if another object currently has the file open or not.
     *
     * @param file LTFile object
     * @return true if the LTFile corresponds to an opened file, false otherwise
     */

    bool (*Copy)(LTFile *sourceFile, LTFile *destFile);
    /**< Copy contents of sourceFile to path defined at destFile overwriting it if needed.
     *
     * @note Assumes both the sourceFile and destFile are CLOSED at the start. Copy() will 
     *      open() both files, copy, then close() both files.
     *  
     * @param sourceFile file that contains the data to copy
     * @param destFile location to copy the data to
     * @return true if copy succeeded, false otherwise
     */

    bool (*Move)(LTFile *sourceFile, LTFile *destFile);
    /**< Move sourceFile to path defined at destFile, overwriting the destination if needed.
     *
     * @note Assumes both the sourceFile and destFile are CLOSED at the start. Move() will
     *      open() both files, move, then close() both files.
     *
     * @param sourceFile file to move
     * @param destFile location to move the file to
     * @return true if move succeeded, false otherwise
     */

    void (*Delete)(LTFile *file, bool bRecursive);
    /**< Delete a file or directory. By default, this function will NOT recursively delete directories and
     * their contents. Clients must opt-in to recursive deletion with bRecursive.
     *
     * Deletion will silently fail. Clients should use LTFile->Exists() to check if a file has been deleted or not
     * if the outcome is relevant.
     * 
     * @param file LTFile that has a file or directory specified using SetName()
     * @param bRecursive whether or not recursive deletion should be performed. The purpose of this parameter
     * is to force clients to "opt-in" to the more dangerous recursive deletion behavior.
     */

    bool (*MakeDirectory)(LTFile *file, bool bMakeFullPath);
    /**< Makes the directories specified in a file name. If filename is "A/B/C.txt", directories "A" and "A/B/"" will be
     * created, and C.txt is assumed to be a file and not a directory. If the filename is "A/B/C/", directories  "A",
     * "A/B/" and "A/B/C/" will all be created.
     *
     * @param file that has a name with directories in it. The name after the final slash is always assumed to be the
     * name of the file itself, not a directory.
     * @param bMakeFullPath whether or not to create new directories if parent directories are missing
     * @return true if directories were successfully created, false otherwise
     */
    bool (*TraverseDirectory)(LTFile *directoryFile, LTFile_TraversalCallback *traversalCallback, void *clientData);
    /**< Traverse a directory by enumerating through files in the directory. This function will NOT explore
     * subdirectories or symbolic links. The directory MUST be opened before calling this function.
     *
     * @param file that is an open directory.
     * @param traversalCallback callback function to be called for every file in the directory
     * @param clientData user client data to be passed into the callback
     * @return true if directory was fully enumerated. False if enumeration was incomplete.
     */

    u64  (*GetSize)(LTFile *file);
    /**< Get the size of a file in bytes. If the file is a directory, 0 will be returned.
     *  @note Some filesystem implementations may require the file to be opened before the size can be determined.
     *
     * @param file LTFile object.
     * @return size of the file, in bytes.
     * @return zero if the file does not exist or is a directory.
     */

    u64  (*GetPosition)(LTFile *file);
    /**< Get the current byte position of the read/write pointer in the file
     *
     * @param file LTFile object that has been Open()'d
     * @return the current position of the read/write pointer in the file, in bytes
     */

    void (*SeekToPosition)(LTFile *file, u64 position);
    /**< Move the read/write pointer of the file to a specified byte position in the file.
     *
     * @note If a client attempts to seek past the end of the file, the position will be set to the end of the file
     * @param file LTFile object that has been Open()'d
     */

    u32  (*GetBlockSize)(LTFile *file);
    /**< Get the optimal block size for the OS and/or filesystem data.
     * This represents the optimal size and/or alignment used for reading and writing data to the file.
     *
     * @param file LTFile object.
     * @return optimal block size in bytes, or 0 if the optimal block size is unknown or does not matter.
     */

    bool (*Open)(LTFile *file, bool bCreate);
    /**< Open the file based on the path and name that was provided to SetName().
     *
     * @param file LTFile that has a file or directory specified using SetName()
     * @param bCreate whether or not to create the file if it currently does not exist. MakeDirectory() will also be
     * called to create any dependent parent directories of the file.
     * @return true if file was successfully opened(and created), false otherwise
     */

    u32  (*Read)(LTFile *file, u32 numBytes, u8 *buffer);
    /**< Read a specified number of bytes out a file. This will advance the read/write pointer of the file by
     * numBytes. Attempting to read past the end of the file will return the number of bytes that were able to be read.
     *
     * @param[in] file LTFile object that has been Open()'d
     * @param[in] numBytes number of bytes to read from the file and into client provided buffer
     * @param[out] buffer client supplied buffer that should be able to hold numBytes. The numBytes file data will be
     * copied into this buffer.
     * @return number of bytes read. The value will be 0 if there was a read error or no bytes were able to be read.
     */

    u32  (*Write)(LTFile *file, u32 numBytes, const u8 *bytes);
    /**< Write a specified number of bytes into the file. This will advance the read/write pointer of the file by
     * numBytes
     *
     * @param[in] file LTFile object that has been Open()'d
     * @param[in] numBytes number of bytes to write into the file from the client provided buffer
     * @param[in] bytes pointer to a buffer that contains the data to be written into the file
     * @return number of bytes written. The value will be 0 if there was a write error.
     */

    void (*Close)(LTFile *file);
    /**< Close a file or directory that was previously opened.
     *
     * @param file LTFile object that has been Open()'d
     */
} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef LT_INCLUDE_LT_SYSTEM_FS_LTFILE_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Jun-23   augustus    created
 */
