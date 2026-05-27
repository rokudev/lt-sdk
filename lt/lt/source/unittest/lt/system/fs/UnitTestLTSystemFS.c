/*******************************************************************************
 * LTSystemFS / LTFile Unit Test
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/system/fs/LTFile.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>
#include <tilt/JiltEngine.h>

static JiltEngine *s_engine;

#define BASE_DIR "/tmp/test/"

LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(UnitTestLTSystemFS,
    (LTDeviceConfig)
);

typedef enum {
    kFile1Traversed = 1 << 0,
    kFile2Traversed = 1 << 1,
    kFile3Traversed = 1 << 2,
    kFile4Traversed = 1 << 3,
    kFile5Traversed = 1 << 4,
    kInFolder1      = 1 << 5,
    kInFolder2      = 1 << 6,
    kInFolder3      = 1 << 7,
} TraverseFlags;

typedef struct {
    u8 **writeArray;
    u32 nWrites;
    u32 nBytesPerWrite;
} WriteArray;

typedef struct {
    u64 position;
    u32 size;
    u8 *data;
} SeekModification;

/*_______________________
  Test helper functions*/

static void DeleteTestFileDirectory(Tilt *tilt) {
    LTFile *file = lt_createobject(LTFile);

    /* Delete all files under BASE_DIR, if they exist */
    file->API->SetName(file, BASE_DIR);
    file->API->Delete(file, true);
    /* Make sure the BASE_DIR folder no longer exists */
    TILT_ASSERT_FALSE(tilt, file->API->Exists(file), "Test directory still exists: %s", BASE_DIR);

    lt_destroyobject(file);
}

static bool DoesFSDriverExist(void){
    const char *driverName = LT_GetLTDeviceConfig()->GetDriverAt("LTDeviceFS", 0);
    return driverName != NULL;
}

static void OpenAndCreateFile(Tilt *tilt, LTFile *file) {
    bool openSuccess = file->API->Open(file, true);
    TILT_EXPECT_TRUE(tilt, openSuccess, "File failed to open");
    TILT_EXPECT_TRUE(tilt, file->API->Exists(file), "File does not exist after creating");
}

static void DeleteFile(Tilt *tilt, LTFile *file) {
    file->API->Delete(file, false);
    TILT_EXPECT_FALSE(tilt, file->API->Exists(file), "File still exists after deleting");
    TILT_EXPECT_FALSE(tilt, file->API->Open(file, false), "File still opens after deleting");
}

static void CloseFile(Tilt *tilt, LTFile *file) {
    file->API->Close(file);
    TILT_EXPECT_FALSE(tilt, file->API->IsOpen(file), "File is still open after closing");
}

static bool ExpectFilePosition(Tilt *tilt, LTFile *file, u64 expectedPosition) {
    u64 position = file->API->GetPosition(file);
    bool isPositionCorrect = (position == expectedPosition);
    TILT_EXPECT_TRUE(tilt,
                     isPositionCorrect,
                     "Position: %llu, Expected: %llu",
                     LT_Pu64(position),
                     LT_Pu64(expectedPosition));
    return isPositionCorrect;
}

/* Returns the data that was generated and written to the file.
 * Assumes the file has been opened and is ready to write. */
static u8 *MallocAndWriteArbitraryBytesToFile(Tilt *tilt, LTFile *file, u32 nBytes) {
    u8 *data = lt_malloc(nBytes);
    if (!data) {
        TILT_REPORT_CANNOT_RUN(tilt, "Failed to malloc write data");
        return NULL;
    }
    for (u32 i = 0; i < nBytes; i++) {
        // Fill data with an arbitrary sequence of bytes (u32 cast to u8)
        data[i] = (u8)i;
    }
    
    u32 nWritten = file->API->Write(file, nBytes, data);
    TILT_EXPECT_TRUE(tilt, nWritten == nBytes, "Did not write all data");

    return data;
}

static void FreeWriteArray(WriteArray writes) {
    for (u8 i = 0; i < writes.nWrites; i++) {
        lt_free(writes.writeArray[i]);
    }
    lt_free(writes.writeArray);
}

/* File must be open */
static WriteArray RepeatWriteRandomBytesToFile(Tilt *tilt, LTFile *file, u32 nWrites, u32 nBytesPerWrite) {
    /* Allocate array to store individual writes */
    WriteArray writes = {NULL, 0, 0};
    writes.writeArray = lt_malloc(sizeof(writes.writeArray[0]) * nWrites);
    if (!writes.writeArray) {
        TILT_REPORT_CANNOT_RUN(tilt, "Failed to malloc writeArray");
        return writes;
    }
    
    /* Repeat requested number of writes */
    for (u32 i = 0; i < nWrites; i++) {
        writes.writeArray[i] = MallocAndWriteArbitraryBytesToFile(tilt, file, nBytesPerWrite); // Will report TILT error on failure
        if (writes.writeArray[i] == NULL) {
            FreeWriteArray(writes);
            return (WriteArray){NULL, 0, 0};
        }
        ++writes.nWrites;

        if (!ExpectFilePosition(tilt, file, (i+1)*nBytesPerWrite)) { // Will report TILT error on failure
            FreeWriteArray(writes);
            return (WriteArray){NULL, 0, 0};
        }
    }

    writes.nBytesPerWrite = nBytesPerWrite;
    return writes;
}

/* File must be open and at position 0. Read and compare 1 byte at a time for simplicity with array boundaries. */
static bool VerifyReadData(Tilt *tilt, LTFile *file, WriteArray expectedWrites, SeekModification *modArray, u32 modArrayLength) {
    u8 readBuffer[1]; // Buffer of size of 1 to read into
    u64 readPosition = 0;
    while (readPosition < expectedWrites.nWrites * expectedWrites.nBytesPerWrite) {
        /* Read 1 byte */
        u32 bytesRead = file->API->Read(file, 1, readBuffer);
        if (bytesRead != 1) {
            TILT_REPORT_FAILURE(tilt, "Failed to read 1 byte");
            break;
        }
        if (!ExpectFilePosition(tilt, file, readPosition + bytesRead)) break; // Will report TILT error on failure

        /* Compare 1 read byte with the appropriate expected byte */
        u8 *expectedByte = NULL;
        // First, check modifications if they exist
        for (u8 i = 0; modArray && i < modArrayLength; i++) {
            SeekModification *current = modArray + i;
            if (readPosition >= current->position && readPosition < current->position + current->size) {
                expectedByte = current->data + (readPosition - current->position);
                break; // Exit for loop
            }
        }
        // Then, check writeArray if there was no seek-modified byte
        if (expectedByte == NULL) {
            u8 *expectedData = expectedWrites.writeArray[readPosition / expectedWrites.nBytesPerWrite];
            expectedByte = expectedData + (readPosition % expectedWrites.nBytesPerWrite);
        }
        // Finally, compare the expectedByte with the read byte
        if (lt_memcmp(readBuffer, expectedByte, 1) != 0) {
            TILT_REPORT_FAILURE(tilt, "Data mismatch at position %lu", LT_Pu32(readPosition));
            return false;
        }

        readPosition += bytesRead;
    }   
    return true;
}

// TODO: Replace with VerifyReadData()
static void AssertReadDataCorrect(Tilt *tilt,
                                  LTFile                           *file,
                                  u8                               *expectedData,
                                  u32                               expectedDataLen) {
    if (!expectedData) TILT_REPORT_FAILURE(tilt, "expectedData was NULL");
    u8 *readData = lt_malloc(expectedDataLen);
    if (!readData) TILT_REPORT_CANNOT_RUN(tilt, "Failed to malloc readData");
    u32 nRead = file->API->Read(file, expectedDataLen, readData);
    TILT_ASSERT_TRUE(tilt, nRead == expectedDataLen, "Did not read all data");
    TILT_ASSERT_TRUE(tilt,
                     lt_memcmp(readData, expectedData, expectedDataLen) == 0,
                     "Read data did not match expected data");
    lt_free(readData);
}

static void DeleteNamedFile(Tilt *tilt, LTFile *file, const char *name) {
    /* Ensure file was deleted */
    file->API->SetName(file, name);
    DeleteFile(tilt, file);
}

static void CreateNamedFile(Tilt *tilt, LTFile *file, const char *name) {
    /* Open the file to create it */
    file->API->SetName(file, name);
    OpenAndCreateFile(tilt, file);
    file->API->Close(file);
    /* Ensure file exists, as an extra check */
    TILT_ASSERT_TRUE(tilt, file->API->Exists(file), "File does not exist");
}

/*______________________
  Test implementation */

static void TestOpenClose(Tilt *tilt) {
    LTFile *file = DoesFSDriverExist() ? lt_createobject(LTFile) : lt_createobject_typed(LTFile, LTMemoryFile);
    file->API->SetName(file, BASE_DIR "openclose");
    
    /* bCreate == false */
    bool openSuccess = file->API->Open(file, false);
    TILT_EXPECT_FALSE(tilt, openSuccess, "File opens without being created");
    TILT_EXPECT_FALSE(tilt, file->API->Exists(file), "File exists without being created");
    CloseFile(tilt, file);
    
    lt_destroyobject(file);
}

static void TestCreate(Tilt *tilt) {
    LTFile *file = DoesFSDriverExist() ? lt_createobject(LTFile) : lt_createobject_typed(LTFile, LTMemoryFile);
    file->API->SetName(file, BASE_DIR "create");

    /* bCreate = true */
    OpenAndCreateFile(tilt, file);
    CloseFile(tilt, file);

    DeleteFile(tilt, file);
    lt_destroyobject(file);
}

static void TestDelete(Tilt *tilt) {
    LTFile *file = DoesFSDriverExist() ? lt_createobject(LTFile) : lt_createobject_typed(LTFile, LTMemoryFile);
    file->API->SetName(file, BASE_DIR "delete");
    
    /* Create and write data to a file */
    OpenAndCreateFile(tilt, file);
    u8 *writtenData = MallocAndWriteArbitraryBytesToFile(tilt, file, 256);
    lt_free(writtenData); // Do not care what was written
    CloseFile(tilt, file);
    
    /* Ensure deletion was successful */
    DeleteFile(tilt, file);
    
    lt_destroyobject(file);
}

static void TestReadWrite(Tilt *tilt) {
    LTFile *file = DoesFSDriverExist() ? lt_createobject(LTFile) : lt_createobject_typed(LTFile, LTMemoryFile);
    file->API->SetName(file, BASE_DIR "readwrite");

    const LT_SIZE dataPerWrite = 777; // Arbitrary, non-power of 2 amount of data
    const u8 nWrites = 20;
    WriteArray writes = {};

    do {
        /* Create and write large amount of data to a file */
        OpenAndCreateFile(tilt, file);
        writes = RepeatWriteRandomBytesToFile(tilt, file, nWrites, dataPerWrite);
        if (!writes.writeArray) break;
        CloseFile(tilt, file);
        
        /* Read and verify written data */
        file->API->Open(file, false);
        file->API->SeekToPosition(file, 0);
        bool success = VerifyReadData(tilt, file, writes, NULL, 0);
        if (!success) break;
        CloseFile(tilt, file);
    } while (false);

    FreeWriteArray(writes);
    DeleteFile(tilt, file);
    lt_destroyobject(file);
}

static void TestReadPastEnd(Tilt *tilt) {
    LTFile *file = DoesFSDriverExist() ? lt_createobject(LTFile) : lt_createobject_typed(LTFile, LTMemoryFile);
    file->API->SetName(file, BASE_DIR "readpastend");
    const u32 nBytesToWrite = 20;

    /* Create and write data to a file */
    OpenAndCreateFile(tilt, file);
    u8 *writtenData = MallocAndWriteArbitraryBytesToFile(tilt, file, nBytesToWrite);
    CloseFile(tilt, file);
    
    /* Trying to read past the end of the file should only read the actual number of bytes */
    file->API->Open(file, false);       // Open file before reading
    file->API->SeekToPosition(file, 0); // Seek to the start of the file
    u8 *readBuffer = lt_malloc(nBytesToWrite * 2);
    if (!readBuffer) TILT_REPORT_CANNOT_RUN(tilt, "Failed to alloc read buffer");
    u32 bytesRead = file->API->Read(file, nBytesToWrite * 2, readBuffer);
    ExpectFilePosition(tilt, file, nBytesToWrite);
    TILT_EXPECT_TRUE(tilt,
                     bytesRead == nBytesToWrite,
                     "Read: %lu, Expected: %lu",
                     LT_Pu32(bytesRead),
                     LT_Pu32(nBytesToWrite));
    TILT_EXPECT_TRUE(tilt, lt_memcmp(writtenData, readBuffer, bytesRead) == 0, "Read data did not match written data");

    /* Cleanup */
    lt_free(writtenData);
    lt_free(readBuffer);
    DeleteFile(tilt, file);
    lt_destroyobject(file);
}

static void TestSeekModify(Tilt *tilt) {
    LTFile *file = DoesFSDriverExist() ? lt_createobject(LTFile) : lt_createobject_typed(LTFile, LTMemoryFile);
    file->API->SetName(file, BASE_DIR "seekmodify");

    const LT_SIZE dataPerWrite = 777; // Arbitrary, non-power of 2 amount of data
    const u8 nWrites = 20;
    SeekModification mods[2];
    mods[0] = (SeekModification){.position = 10000, .size = 500, .data = NULL};
    mods[1] = (SeekModification){.position = 20, .size = 430, .data = NULL};
    WriteArray writes = {};
    do {
        /* Create and write large amount of data to a file */
        OpenAndCreateFile(tilt, file);
        writes = RepeatWriteRandomBytesToFile(tilt, file, nWrites, dataPerWrite);
        if (!writes.writeArray) break;
        CloseFile(tilt, file);

        /* Seek and modify several sections across the file */
        file->API->Open(file, false);
        file->API->SeekToPosition(file, mods[0].position);
        mods[0].data = MallocAndWriteArbitraryBytesToFile(tilt, file, mods[0].size);
        if (!mods[0].data) break;
        file->API->SeekToPosition(file, mods[1].position);
        mods[1].data = MallocAndWriteArbitraryBytesToFile(tilt, file, mods[1].size);
        if (!mods[1].data) break;
        CloseFile(tilt, file);
        
        /* Read and validate modified and unmodified data */
        file->API->Open(file, false);
        file->API->SeekToPosition(file, 0);
        bool success = VerifyReadData(tilt, file, writes, mods, 2);
        if (!success) break;
        CloseFile(tilt, file);

    } while (false);
    
    FreeWriteArray(writes);
    lt_free(mods[0].data);
    lt_free(mods[1].data);
    DeleteFile(tilt, file);
    lt_destroyobject(file);
}

static void TestSeekPastEnd(Tilt *tilt) {
    LTFile *file = DoesFSDriverExist() ? lt_createobject(LTFile) : lt_createobject_typed(LTFile, LTMemoryFile);
    file->API->SetName(file, BASE_DIR "readpastend");
    const u32 nBytesToWrite = 20;
    
    /* Create and write data to a file */
    OpenAndCreateFile(tilt, file);
    u8 *writtenData = MallocAndWriteArbitraryBytesToFile(tilt, file, nBytesToWrite);
    lt_free(writtenData); // Does not matter what was written
    CloseFile(tilt, file);
    
    /* Seeking past the end of the file should only go to the actual end of the file */
    file->API->Open(file, false);
    file->API->SeekToPosition(file, nBytesToWrite * 2); // Seek past end
    ExpectFilePosition(tilt, file, nBytesToWrite);
    CloseFile(tilt, file);

    lt_destroyobject(file);
}

static void TestCopy(Tilt *tilt) {
    if (!DoesFSDriverExist()) return; // TODO: Copy() is not implemented for LTMemoryFile
    LTFile *file1 = lt_createobject(LTFile);
    LTFile *file2 = lt_createobject(LTFile);

    /* Create file 1 and write test data */
    file1->API->SetName(file1, BASE_DIR "testfile1");
    bool createSuccess = file1->API->Open(file1, true);
    TILT_ASSERT_TRUE(tilt, createSuccess, "Failed to create file");
    const u32 dataLen   = 256;
    u8       *writeData = MallocAndWriteArbitraryBytesToFile(tilt, file1, dataLen);
    file1->API->Close(file1);

    /* Create file 2 to copy into */
    file2->API->SetName(file2, BASE_DIR "testfile2");
    createSuccess = file2->API->Open(file2, true);
    TILT_ASSERT_TRUE(tilt, createSuccess, "Failed to create file");
    file2->API->Close(file2);

    /* Copy file 1 into file 2. Both files must be closed beforehand */
    file1->API->Copy(file1, file2);

    /* Ensure file 2 contains the data */
    file2->API->Open(file2, false);
    AssertReadDataCorrect(tilt, file2, writeData, dataLen);
    file2->API->Close(file2);

    lt_free(writeData);
    DeleteFile(tilt, file1);
    DeleteFile(tilt, file2);
    lt_destroyobject(file1);
    lt_destroyobject(file2);
}

static void TestIsFile(Tilt *tilt) {
    if (!DoesFSDriverExist()) return;
    LTFile *file = lt_createobject(LTFile);

    /* Create a file */
    file->API->SetName(file, BASE_DIR "testfile");
    bool createSuccess = file->API->Open(file, true);
    TILT_ASSERT_TRUE(tilt, createSuccess, "Failed to create file");

    /* Make sure IsFile() reports the file correctly */
    bool isFile = file->API->IsFile(file);
    TILT_ASSERT_TRUE(tilt, isFile, "IsFile failed!");
    file->API->Close(file);

    lt_destroyobject(file);
}

static void TestIsDirectory(Tilt *tilt) {
    if (!DoesFSDriverExist()) return;
    LTFile *file = lt_createobject(LTFile);

    /* Create a file within a directory */
    file->API->SetName(file, BASE_DIR "folder/");
    bool createSuccess = file->API->MakeDirectory(file, true);
    TILT_ASSERT_TRUE(tilt, createSuccess, "Failed to create folder/");

    /* Make sure IsDirectory reports the directory correctly */
    bool openSuccess = file->API->Open(file, false);
    TILT_ASSERT_TRUE(tilt, openSuccess, "Could not open directory!");
    bool isDir = file->API->IsDirectory(file);
    TILT_ASSERT_TRUE(tilt, isDir, "IsDirectory failed!");
    file->API->Close(file);

    lt_destroyobject(file);
}

static TraverseFlags s_flags = 0;

static bool TraverseFiles(LTFile *directory, LTFile *child, void *clientData) {
    LT_UNUSED(clientData);

    /* Make sure directory object is what was passed in */
    LTString dirname = ltstring_create("");
    directory->API->GetName(directory, &dirname);
    if (lt_strcmp(dirname, BASE_DIR "folder/") == 0) {
        s_flags |= kInFolder1;
    } else if (lt_strcmp(dirname, BASE_DIR "folder/more/") == 0) {
        s_flags |= kInFolder2;
    } else if (lt_strcmp(dirname, BASE_DIR "folder/more/andmore/") == 0) {
        s_flags |= kInFolder3;
    }

    /* Mark the file that was traversed */
    LTString fullname = ltstring_create("");
    child->API->GetName(child, &fullname);
    const char *filename = fullname + lt_strlen(dirname);

    if (lt_strcmp(filename, "file1") == 0) {
        s_flags |= kFile1Traversed;
    } else if (lt_strcmp(filename, "file2") == 0) {
        s_flags |= kFile2Traversed;
    } else if (lt_strcmp(filename, "file3") == 0) {
        s_flags |= kFile3Traversed;
    } else if (lt_strcmp(filename, "file4") == 0) {
        s_flags |= kFile4Traversed;
    } else if (lt_strcmp(filename, "file5") == 0) {
        s_flags |= kFile5Traversed;
    }

    ltstring_destroy(dirname);
    ltstring_destroy(fullname);
    return true;
}

static void TestTraverseDirectory(Tilt *tilt) {
    if (!DoesFSDriverExist()) return;
    DeleteTestFileDirectory(tilt);
    /* Create 5 files, in the following structure:
     * folder/file1
     * folder/file2
     * folder/more/file3
     * folder/more/file4
     * folder/more/andmore/file5 */
    LTFile *file = lt_createobject(LTFile);
    CreateNamedFile(tilt, file, BASE_DIR "folder/file1");
    CreateNamedFile(tilt, file, BASE_DIR "folder/file2");
    CreateNamedFile(tilt, file, BASE_DIR "folder/more/file3");
    CreateNamedFile(tilt, file, BASE_DIR "folder/more/file4");
    CreateNamedFile(tilt, file, BASE_DIR "folder/more/andmore/file5");

    /* Traverse folder. Only file1 and file2 should be iterated through */
    s_flags = 0;
    file->API->SetName(file, BASE_DIR "folder/");
    file->API->Open(file, false);
    file->API->TraverseDirectory(file, &TraverseFiles, NULL);
    TILT_ASSERT_TRUE(tilt, s_flags == (kFile1Traversed | kFile2Traversed | kInFolder1), "Traverse folder/ failed");
    file->API->Close(file);

    /* Traverse folder/more. Only file3 and file4 should be iterated through */
    s_flags = 0;
    file->API->SetName(file, BASE_DIR "folder/more/");
    file->API->Open(file, false);
    file->API->TraverseDirectory(file, &TraverseFiles, NULL);
    TILT_ASSERT_TRUE(tilt, s_flags == (kFile3Traversed | kFile4Traversed | kInFolder2), "Traverse folder/more/ failed");
    file->API->Close(file);

    /* Traverse folder/more/andmore. Only file5 should be iterated through */
    s_flags = 0;
    file->API->SetName(file, BASE_DIR "folder/more/andmore/");
    file->API->Open(file, false);
    file->API->TraverseDirectory(file, &TraverseFiles, NULL);
    TILT_ASSERT_TRUE(tilt, s_flags == (kFile5Traversed | kInFolder3), "Traverse folder/more/andmore/ failed");
    file->API->Close(file);

    lt_destroyobject(file);
}

static void TestDeleteNested(Tilt *tilt) {
    if (!DoesFSDriverExist()) return;
    DeleteTestFileDirectory(tilt);
    /* Create 5 files, in the following structure:
     * folder/file1
     * folder/file2
     * folder/more/file3
     * folder/more/file4
     * folder/more/andmore/file5 */
    LTFile *file = lt_createobject(LTFile);
    CreateNamedFile(tilt, file, BASE_DIR "folder/file1");
    CreateNamedFile(tilt, file, BASE_DIR "folder/file2");
    CreateNamedFile(tilt, file, BASE_DIR "folder/more/file3");
    CreateNamedFile(tilt, file, BASE_DIR "folder/more/file4");
    CreateNamedFile(tilt, file, BASE_DIR "folder/more/andmore/file5");

    /* Delete folder/ */
    file->API->SetName(file, BASE_DIR "folder");
    file->API->Delete(file, true);

    /* Try to open and read any of the created files. They should all have been deleted. */
    DeleteNamedFile(tilt, file, BASE_DIR "folder/file1");
    DeleteNamedFile(tilt, file, BASE_DIR "folder/file2");
    DeleteNamedFile(tilt, file, BASE_DIR "folder/more/file3");
    DeleteNamedFile(tilt, file, BASE_DIR "folder/more/file4");
    DeleteNamedFile(tilt, file, BASE_DIR "folder/more/andmore/file5");

    /* Also make sure base folder was deleted */
    file->API->SetName(file, BASE_DIR "folder");
    TILT_ASSERT_FALSE(tilt, file->API->Exists(file), "File still exists: %s", BASE_DIR "folder");

    lt_destroyobject(file);
}

static void TestSetBaseDirectory(Tilt *tilt){
    if (!DoesFSDriverExist()) return;
    LTFile *file = lt_createobject(LTFile);
    LTString newBase = ltstring_create("");

    const char *base = "/newbase/";

    /* Setting base directory with no filename set */
    file->API->SetBaseDirectory(file, "/newbase/file1");
    file->API->GetName(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, "/newbase/") == 0, "Base directory does not match: %s, expected %s",
                      newBase, base);

    /* Trailing slash*/
    file->API->SetBaseDirectory(file, "newbase/");
    file->API->GetName(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, "/newbase/") == 0, "Base directory does not match: %s, expected %s",
                      newBase, base);

    /* Leading slash*/
    file->API->SetBaseDirectory(file, "/newbase");
    file->API->GetName(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, "/newbase/") == 0, "Base directory does not match: %s, expected %s",
                      newBase, base);

    /* Double leading slash*/
    file->API->SetBaseDirectory(file, "//newbase/");
    file->API->GetName(file, &newBase);
    TILT_EXPECT_FALSE(tilt, lt_strcmp(newBase, "/newbase/") == 0, "Must not set invalid base directory name %s",
                      newBase);

    /* Double trailing slash*/
    file->API->SetBaseDirectory(file, "new_base//file1");
    file->API->GetName(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, "/new_base/") == 0, "Base directory does not match: %s, expected %s",
                      newBase, base);

    /* Calling SetBaseDirectory repeatedly should replace base name with later call */
    file->API->SetName(file, "");
    file->API->SetBaseDirectory(file, "/oldbase/file1");
    file->API->SetBaseDirectory(file, "/newbase/file1");
    file->API->GetName(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, base) == 0, "Base directory does not match: %s, expected %s",
                      newBase, base);

    /* Calling SetBaseDirectory after SetName should replace base name only*/
    file->API->SetName(file, "/oldbase/file2");
    file->API->SetBaseDirectory(file, "newbase");
    file->API->GetName(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, "/newbase/file2") == 0, "Base directory does not match: %s, expected %s",
                      newBase, "/newbase/file2");

    /* Only first section will be picked as base from multi-directory path */
    file->API->SetName(file, "");
    file->API->SetBaseDirectory(file, "/newbase/folder1/folder2/");
    file->API->GetName(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, base) == 0, "Base directory does not match: %s, expected %s",
                      newBase, base);

    lt_destroyobject(file);
    ltstring_destroy(newBase);
}

static void TestGetBaseDirectory(Tilt *tilt){
    if (!DoesFSDriverExist()) return;
    LTFile *file = lt_createobject(LTFile);
    LTString newBase = ltstring_create("");

    const char *base = "newbase";

    /* Only first section will be picked as base directory from full name */
    file->API->SetName(file, "/newbase/file1");
    file->API->GetBaseDirectory(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, base) == 0, "Base directory does not match: %s, expected %s",
                      newBase, base);

    /* Both leading amd trailing slash*/
    file->API->SetName(file,"/newbase/");
    file->API->GetBaseDirectory(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, base) == 0, "Base directory does not match: %s, expected %s",
                      newBase, base);

    /* Leading slash*/
    file->API->SetName(file,"/newbase");
    file->API->GetBaseDirectory(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, base) == 0, "Base directory does not match: %s, expected %s",
                      newBase, base);

    /* Trailing slash*/
    file->API->SetName(file,"newbase/");
    file->API->GetBaseDirectory(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, base) == 0, "Base directory does not match: %s, expected %s",
                      newBase, base);

    /* Double leading slash */
    file->API->SetName(file,"//new_base/file1");
    file->API->GetBaseDirectory(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, base) < 0, "Invalid directory name %s, expected %s",
                      newBase, base);

    /* Only first section will be picked as base from multi-directory path */
    file->API->SetName(file, "/newbase/folder1/folder2/");
    file->API->GetBaseDirectory(file, &newBase);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(newBase, base) == 0, "Base directory does not match: %s, expected %s",
                      newBase, base);

    lt_destroyobject(file);
    ltstring_destroy(newBase);
}

static void TestGetSize(Tilt *tilt){
    LTFile *file = DoesFSDriverExist() ? lt_createobject(LTFile) : lt_createobject_typed(LTFile, LTMemoryFile);
    file->API->SetName(file, BASE_DIR "testfile");

    const LT_SIZE dataPerWrite = 777; // Arbitrary, non-power of 2 amount of data
    const u8 nWrites = 20;
    WriteArray writes = {};

    do {
        /* Create and write large amount of data to a file */
        OpenAndCreateFile(tilt, file);
        writes = RepeatWriteRandomBytesToFile(tilt, file, nWrites, dataPerWrite);
        if (!writes.writeArray) break;
        CloseFile(tilt, file);
    } while (false);

    file->API->Open(file, false);
    u64 fileSize = file->API->GetSize(file);
    TILT_EXPECT_TRUE(tilt, ((dataPerWrite*nWrites) == fileSize), "Unexpected file size %lu",fileSize);
    FreeWriteArray(writes);
    DeleteFile(tilt, file);
    lt_destroyobject(file);
}

/*______________________________
  Library/test initialization */
static const TiltEngineTest s_tests[] = {
    TILT_TEST_SECTION("File tests (no support for broader file system required)"),
    {TestOpenClose,         "OpenClose",         "Open and close a file without creating",                             0},
    {TestCreate,            "Create",            "Open, create, then close a file",                                    0},
    {TestDelete,            "Delete",            "Delete a file that contains data (nonrecursive)",                    0},
    {TestReadWrite,         "ReadWrite",         "Write data into a file and read to verify",                          0},
    {TestReadPastEnd,       "ReadPastEnd",       "Test reading past the end of the file",                              0},
    {TestSeekModify,        "SeekModify",        "Test seeking and writing, then read to verify",                      0},
    {TestSeekPastEnd,       "SeekPastEnd",       "Test seeking past the end of the file",                              0},
    {TestCopy,              "Copy",              "Test copying one file to another file",                              0},
    {TestGetSize,           "GetSize",           "Test for getting the file size",                                     0},
    TILT_TEST_SECTION("File system tests"),
    {TestIsFile,            "IsFile",            "Test if IsFile() is correct",                                        0},
    {TestIsDirectory,       "IsDirectory",       "Test if IsDirectory() is correct",                                   0},
    {TestTraverseDirectory, "TraverseDirectory", "Test traversing a directory",                                        0},
    {TestDeleteNested,      "DeleteNested",      "Test deleting a folder that contains both filled folders and files", 0},
    {TestSetBaseDirectory,  "SetBaseDirectory",  "Test for setting the base directory",                                0},
    {TestGetBaseDirectory,  "GetBaseDirectory",  "Test for getting the base directory",                                0}
};


static void UnitTestLTSystemFS_BeforeTest(Tilt *tilt) {
    LT_UNUSED(tilt);
}

static void UnitTestLTSystemFS_AfterTest(Tilt *tilt) {
    LT_UNUSED(tilt);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeTest = UnitTestLTSystemFS_BeforeTest,
    .AfterTest  = UnitTestLTSystemFS_AfterTest,
};

static int UnitTestLTSystemFSImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTSystemFSImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTSystemFSImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemFS, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTSystemFS, UnitTestLTSystemFSImpl_Run, 1536) LTLIBRARY_DEFINITION;
