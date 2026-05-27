/*******************************************************************************
 * lt/source/example/media/ExampleMediaMP4.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <lt/LT.h>
#include <lt/media/mp4/LTMediaMP4.h>
#include <lt/system/fs/LTFile.h>


LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(ExampleMediaMP4,
    //(LTFile)
    (LTMediaMP4)
);

static int TokenizeH264(u8 *buf, int bufsize) {
    LT_ASSERT(bufsize > 4);
    LT_ASSERT(buf[0] == 0);
    LT_ASSERT(buf[1] == 0);
    LT_ASSERT(buf[2] == 0);
    LT_ASSERT(buf[3] == 1);

    for (int idx = 4; bufsize-idx >= 4; idx++) {
        if (buf[idx+0] == 0 && buf[idx+1] == 0 && buf[idx+2] == 0 && buf[idx+3] == 1)
            return idx;
    }

    return bufsize;
}

static u32 ReadU32(int fd) {
    u32 value;
    read(fd, &value, sizeof(u32));
    return LT_HTONL(value);
}

static void AddVideoFrames(LTMediaMP4_Builder * builder, int inputFile) {
    int inputSize = lseek(inputFile, 0, SEEK_END);
    int readOffset = 0;
    u32 nSize = 2048;
    u8 * buf = lt_malloc(nSize);

    while (readOffset < inputSize) {
        lseek(inputFile, readOffset, SEEK_SET);
        int bytesRead = read(inputFile, buf, nSize);
        int idx = TokenizeH264(buf, bytesRead);

        if (idx == bytesRead && readOffset + bytesRead < inputSize) {
            // Buffer too small to hold NAL frame. Increase buffer size.
            nSize += 2048;
            buf = lt_realloc(buf, nSize);
            LT_GetCore()->ConsolePrint("Increasing video buffer to %d bytes\n", nSize);
            continue;
        }

        LT_GetLTMediaMP4()->AddNALUnit(builder, buf, idx);
        readOffset += idx;
    }

    lt_free(buf);
    LT_ASSERT(readOffset == inputSize);

    LT_GetCore()->ConsolePrint("Wrote video frames\n");
}

static int ExampleMediaMP4_Run(int argc, const char **argv) {
    if (argc < 4) {
        LT_GetCore()->ConsolePrint(
            "Usage: %s out.mp4 video.h264 [audio.au]\n"
            "  out.mp4:     path of the mp4 output file\n"
            "  video.h264:  path of h264 input file\n"
            "  audio.au:    path of audio format in Sun AU format\n",
            argv[1]
        );
        return 1;
    }

    int outputFile = open(argv[2], O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
    LT_ASSERT(outputFile > 0);

    int videoFile = open(argv[3], O_RDONLY);
    LT_ASSERT(videoFile > 0);

    int audioFile = -1;
    LTMediaMP4_AudioConfig audioConfig = {};
    if (argc >= 5) {
        audioFile = open(argv[4], O_RDONLY);
        LT_ASSERT(audioFile > 0);

        audioConfig.nBytesPerSample = 1; // 8-bit audio

        u8 buf[4];
        read(audioFile, buf, 4);
        LT_ASSERT(lt_memcmp(buf, ".snd", 4) == 0);
        u32 dataOffset = ReadU32(audioFile);
        ReadU32(audioFile); // data size
        u32 encoding = ReadU32(audioFile);
        if (encoding != 27) {
            LT_GetCore()->ConsolePrint("Audio must be 8-bit G.711 A-law");
            return 1;
        }
        audioConfig.nSamplesPerSec = ReadU32(audioFile);
        LT_GetCore()->ConsolePrint("Audio sample rate is %lu Hz\n", LT_Pu32(audioConfig.nSamplesPerSec));
        u32 nChannels = ReadU32(audioFile);
        if (nChannels != 1) {
            LT_GetCore()->ConsolePrint("Only mono audio is supported");
            return 1;
        }
        lseek(audioFile, dataOffset, SEEK_SET);
    }

    LTMediaMP4_Builder * builder = LT_GetLTMediaMP4()->CreateBuilder(audioConfig, outputFile);

    AddVideoFrames(builder, videoFile);

    if (audioFile > 0) {
        const int bufSize = 640;
        u8 * buf = lt_malloc(bufSize);
        u32 nBytes;
        do {
            nBytes = read(audioFile, buf, bufSize);
            LT_GetLTMediaMP4()->AddAudioSamples(builder, buf, nBytes);
        } while (nBytes > 0);
        lt_free(buf);
        LT_GetCore()->ConsolePrint("Wrote audio units\n");
    }

    LT_GetLTMediaMP4()->Finish(builder);
    
    return 0;
}

static bool ExampleMediaMP4Impl_LibInit(void) {
    return true;
}
static void ExampleMediaMP4Impl_LibFini(void) {
}

typedef_LTLIBRARY_ROOT_INTERFACE(ExampleMediaMP4, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(ExampleMediaMP4, ExampleMediaMP4_Run, 8192) LTLIBRARY_DEFINITION;
