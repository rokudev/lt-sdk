/******************************************************************************
 * LTMediaMP4Impl.h
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_MEDIA_MP4_LTMEDIAMP4IMPL_H
#define ROKU_LT_SOURCE_LT_MEDIA_MP4_LTMEDIAMP4IMPL_H

#include <lt/core/LTArray.h>
#include <lt/media/mp4/LTMediaMP4.h>
#include <lt/net/httpclient/LTNetHttpClient.h>

typedef struct {
    u32 offset;
    u32 nSamples;
} Chunk;

typedef struct {
    u8 * pData;
    u32  nDataLen;
} Bytes;

typedef struct {
    char *  containerName;
    bool    isStart;
} ContainerData;

#define MP4_BUILDER_TRAK_VIDEO 1
#define MP4_BUILDER_TRAK_AUDIO 2

struct LTMediaMP4_Builder {
    LTFile * outputFile;

    bool hasWritten;

    // function pointers 
    LTMediaMP4_WriteCallback * writeCallback;
    LTMediaMP4_InsertCallback * insertCallback;
    void * clientData;

    // Bookkeeping for keeping track of container sizes

    LTArray * containerStack; // a stack that contains the name of the containers and a pointer to the start of the container if needed

    u32 nOffset;
    u32 containerOffsets[16];
    u32 nContainerLevel;

    u32 nFileSize;          // size of the mp4 file as it is being built

    int currentTrak;

    Bytes h264LastSps;
    Bytes h264LastPps;

    struct {
        u8 * sps;
        u32 spsLength;
        u8 * pps;
        u32 ppsLength;
        u32 seiLength;

        u32 nSamples;
        LTArray * sampleDurations;   // array of u32; represents the duration of each sample

        LTArray * sampleSizes;       // array of u32
        LTArray * syncSampleIndexes; // array of u32
        LTArray * chunks;            // array of Chunk
    } video;

    struct {
        LTMediaMP4_AudioConfig config;

        u32 nSamples;
        LTArray * sampleDurations; // duration of each sample
        LTArray * chunks; // array of Chunk
    } audio;
};

typedef struct {
    u32 timescale;
    u32 delta;
    u32 duration;
} TimingInfo;

// Whether to keep and duplicate SPS and PPS H264 NALs for every P-Frame in video track
#define MP4_KEEP_PPS_SPS_UNITS 0

// Trim audio track duration to be the same as video track duration using an edit atom (edts)
#define MP4_LIMIT_AUDIO_DURATION_TO_VIDEO 1

// Use 1MB chunks similar to ffmpeg
#define MP4_MAX_CHUNK_SIZE (1024*1024)
// Maximum number of samples per chunk
#define MP4_MAX_CHUNK_SAMPLES 1000

#define TKHD_TRACK_ENABLED               0x01
#define TKHD_TRACK_IN_MOVIE              0x02
#define TKHD_TRACK_IN_PREVIEW            0x04
#define TKHD_TRACK_SIZE_IS_ASPECT_RATIO  0x08

typedef struct {
    u32 width;
    u32 height;
    // avc config
    u8 profile;
    u8 profile_compat;
    u8 level;
    u8 NALUnitLengthMinusOne; // 0, 1 or 3 for NAL Unit Length 1, 2 or 4
    // NOTE: ohly supporting one parameter set
    u8 * sps;
    u32 spsLength;
    u8 * pps;
    u32 ppsLength;
} AVC1;

typedef struct {
    u16 channel_count;
    u16 sample_size;
    u32 samplerate;
} ALAW;

enum HandlerType {
    HandlerType_Unknown,
    HandlerType_Video,
    HandlerType_Sound,
};

typedef struct {
    u32 sampleCount;
    u32 sampleDuration;
} sttsEntry;

typedef struct {
    struct {
        u32 flags;
        u32 track_id;
        u32 width;
        u32 height;
        u32 duration;
    } tkhd;
    struct {
        struct {
            u32 nEntries;   // currently only support for 1 entry

            struct { // 12 byte entry
                u32 duration;   // duration of the edit segment
                u32 mediaTime;  // starting time of the media of this segment
                u16 mediaRateWhole;  // the rate at which to play the media (fixed-point, so two u16s)
                u16 mediaRateFraction;
            } entry1;
        } elst;
    } edts;
    struct {
        enum HandlerType handler_type;
        char *handler_string;
        struct {
            u32 timescale;
            u32 duration;
        } mdhd;
        struct {
            union {
                AVC1 avc1;
                ALAW alaw;
            } stsd;
            struct {
                u32 nEntries;
                // sttsEntry[nEntries]
            } stts;
        } stbl;
    } mdia;
    struct {
        u32 nSamples;
        LTArray * sampleDurations;   // duration of each sample
        u32 nSampleDelta;
        LTArray * syncSampleIndexes; // type u32
        LTArray * chunks;            // type Chunk
        u32 nSampleSize;              // used if all samples have same size
        LTArray * sampleSizes;
    } samples;
} TrakData;

typedef struct {
    struct {
        u32 timescale;
        u32 duration;
    } mvhd;
    TrakData * videoTrak;
    TrakData * audioTrak;
} MoovData;

#endif // ROKU_LT_SOURCE_LT_MEDIA_MP4_LTMEDIAMP4IMPL_H
