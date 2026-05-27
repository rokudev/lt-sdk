/*******************************************************************************
 * lt/source/media/mp4/LTMediaMP4Impl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include "LTMediaMP4Impl.h"
#include "ParseSPS.h"

#include <lt/device/media/LTDeviceMedia.h>
#include <lt/media/mp4/LTMediaMP4.h>
#include <lt/net/httpclient/LTNetHttpClient.h>

/*******************************************************************************
 * Static variables
 ******************************************************************************/

DEFINE_LTLOG_SECTION("mp4");

// #define P(...) LTLOG(__VA_ARGS__)
#define PLOG(...) LTLOG(__VA_ARGS__)
// #define P(...) LTLOG_DEBUG(__VA_ARGS__)
#define P(...)

static const u32 UnitaryMatrix[9] = { 0x00010000,0,0,0,0x00010000,0,0,0,0x40000000 };
static const u16 LangUND = (('u'-'a'+1) << 10) + (('n'-'a'+1) << 5) + ('d'-'a'+1); // ISO-639-2/T language code
static const u32 HandlerVideU32 = ('v' << 24) + ('i' << 16) + ('d' << 8) + 'e';
static const u32 HandlerSoun = ('s' << 24) + ('o' << 16) + ('u' << 8) + 'n';

/*******************************************************************************
 * Private implementation
 ******************************************************************************/

// GCD using the Euclidean algorithm
static u32 GCD(u32 a, u32 b) {
    if (a < b) return GCD(b, a);
    while (b != 0) {
        u32 rem = a % b;
        a = b;
        b = rem;
    }
    return a;
}

static u32 LCM(u32 a, u32 b) {
    // using a*b = gcd(a,b) * lcm(a,b)
    return (a / GCD(a,b)) * b;
}

static TimingInfo ComputeReducedTimingInfo(u32 timescale, u32 delta, u32 sampleCount, LTArray * durations) {
    u32 div = GCD(timescale, delta);
    timescale /= div;
    delta /= div;

    u32 computedSampleCount = sampleCount;
    // Some samples may be played for longer than 1 sample duration
    if (durations->API->GetCount(durations) == sampleCount && sampleCount > 0) {
        computedSampleCount = 0;
        for (u32 i = 0; i < sampleCount; i++) {
            u32 * sampleDuration = durations->API->Get(durations, i, NULL);
            computedSampleCount += *sampleDuration;
        }
    }

    return (TimingInfo){
        .timescale = timescale,
        .delta = delta,
        .duration = delta * computedSampleCount,
    };
};

static void PushContainerEntry(LTMediaMP4_Builder * builder, char * name) {
    ContainerData * newContainer = lt_malloc(sizeof(ContainerData));
    if (!newContainer) {
        LTLOG_YELLOWALERT("push.container", "failed to append container %s", name);
        return;
    }
    newContainer->containerName = name;
    newContainer->isStart = true;
    builder->containerStack->API->Insert(builder->containerStack, 0, newContainer); // push to the stack
}  

static void FreeContainerEntry(LTMediaMP4_Builder * builder) {
    ContainerData * entry = builder->containerStack->API->Get(builder->containerStack, 0, NULL);
    builder->containerStack->API->Remove(builder->containerStack, 0); // pop the container name from the containerStack
    lt_free(entry);
}

/*__________________________________________
  LTMediaMP4_Builder Forward Declarations */

static void WriteFTYP(LTMediaMP4_Builder * builder);
static void BeginContainer(LTMediaMP4_Builder * builder, char * name);

/*_______________________________
  LTMediaMP4_Builder Functions */

static void InitializeMP4IfNeeded(LTMediaMP4_Builder * builder) {
    // write ftyp and begin container if they do not exist
    if (builder && !builder->hasWritten) {
        P("mp4.begin", "write ftyp container and begin mdat container");
        builder->hasWritten = true;
        WriteFTYP(builder);
        BeginContainer(builder, "mdat");
    }
}

static u32 WriteRAW(LTMediaMP4_Builder * builder, const void * bytes, u32 length) {
    ContainerData * currentContainer = builder->containerStack->API->Get(builder->containerStack, 0, NULL);

    // write given data into chosen container
    u32 written = builder->writeCallback(builder, bytes, length, currentContainer->isStart, currentContainer->containerName, builder->clientData);

    if (currentContainer->isStart) { currentContainer->isStart = false;}

    if (written > 0) {
        builder->nOffset += written;
        builder->nFileSize += written;
    }
    return written;
}

static u32 WriteU8(LTMediaMP4_Builder * builder, u8 val) {
    return WriteRAW(builder, &val, 1);
}

static u32 WriteU16(LTMediaMP4_Builder * builder, u16 val) {
    return WriteRAW(builder, &(u16) { LT_HTONS(val) }, sizeof(u16));
}

static u32 WriteU32(LTMediaMP4_Builder * builder, u32 val) {
    return WriteRAW(builder, &(u32) { LT_HTONL(val) }, sizeof(u32));
}

static u32 FileBuilderWrite(LTMediaMP4_Builder *builder, const u8 * bytes, u32 length, bool isStart, const char * containerName, void * clientData) {
    LT_UNUSED(clientData);
    LT_UNUSED(isStart);
    LT_UNUSED(containerName);
    return builder->outputFile->API->Write(builder->outputFile, length, bytes);
}

static u32 FileBuilderInsertSize(LTMediaMP4_Builder *builder, u32 position, const u8 * data, u32 length, const char * name, void * clientData) {
    LT_UNUSED(clientData);
    LT_UNUSED(name);
    u32 endPos = builder->nOffset;
    builder->outputFile->API->SeekToPosition(builder->outputFile, position);
    builder->nFileSize -= WriteRAW(builder, data, length);    // compensate for WriteRAW increasing total file size
    // Return to the end of entry table so container can be ended properly
    builder->outputFile->API->SeekToPosition(builder->outputFile, endPos);
    builder->nOffset = endPos;
    return length; 
}

static void BeginContainer(LTMediaMP4_Builder * builder, char * name) {
    LT_ASSERT(lt_strlen(name) == 4);
    if (!builder) return;
    builder->containerOffsets[builder->nContainerLevel++] = builder->nOffset;
    PushContainerEntry(builder, name); // Add container name and pointer to builder->containerStack
    P("begin.container", "%s", name);
    WriteU32(builder, 0); // dummy size
    WriteRAW(builder, name, 4);
}

static void BeginContainerExt(LTMediaMP4_Builder * builder, char * name, u8 version, u8 flags) {
    BeginContainer(builder, name);
    u8 extd[4] = { version, 0, 0, flags };
    WriteRAW(builder, extd, 4);
}

static void EndContainer(LTMediaMP4_Builder * builder) {
    u32 offset = builder->nOffset;
    LT_ASSERT(builder->nContainerLevel > 0);
    u32 container_start = builder->containerOffsets[--builder->nContainerLevel];
    u32 size = offset - container_start;

    // insertCallback writes back the given size to the header of a container
    ContainerData * currentContainer = builder->containerStack->API->Get(builder->containerStack, 0, NULL);
    P("end.container", "%u", size);
    size = LT_HTONL(size);
    builder->insertCallback(builder, container_start, (u8 *)&size, sizeof(u32), currentContainer->containerName, builder->clientData);

    FreeContainerEntry(builder);
}

static void WriteFTYP(LTMediaMP4_Builder * builder) {
    BeginContainer(builder, "ftyp");

    WriteRAW(builder, "mp42", 4);
    WriteU32(builder, 0);
    WriteRAW(builder, "mp42", 4);
    WriteRAW(builder, "isom", 4);

    EndContainer(builder);
    P("ftyp.done", NULL);
}

static void WriteAVC1(LTMediaMP4_Builder * builder, TrakData * trak) {
    BeginContainer(builder, "avc1");

    // SampleEntry
    for (int i=0; i<3; i++) WriteU16(builder, 0); //reserved
    WriteU16(builder, 1); // data reference index

    // VisualSampleEntry
    for (int i=0; i<4; i++) WriteU32(builder, 0); //reserved/pre-defined
    WriteU16(builder, trak->mdia.stbl.stsd.avc1.width);
    WriteU16(builder, trak->mdia.stbl.stsd.avc1.height);
    WriteU32(builder, 0x00480000); // horiz res 72 dpi
    WriteU32(builder, 0x00480000); // vert res 72 dpi
    WriteU32(builder, 0); // reserved
    WriteU16(builder, 1); // frame count per sample
    for (int i=0; i < 32/4; i++) WriteU32(builder, 0); // compressorname (informative), 32 bytes
    WriteU16(builder, 0x0018); // depth: color, no alpha
    WriteU16(builder, -1); // pre-defined

    // [avcC] AVCDecoderConfigurationRecord
    {
        BeginContainer(builder, "avcC");

        WriteU8(builder, 1); // config version
        WriteU8(builder, trak->mdia.stbl.stsd.avc1.profile);
        WriteU8(builder, trak->mdia.stbl.stsd.avc1.profile_compat);
        WriteU8(builder, trak->mdia.stbl.stsd.avc1.level);
        WriteU8(builder, trak->mdia.stbl.stsd.avc1.NALUnitLengthMinusOne);
        WriteU8(builder, 1); // number_sequence_parameter_sets
        WriteU16(builder, trak->mdia.stbl.stsd.avc1.spsLength);
        WriteRAW(builder, trak->mdia.stbl.stsd.avc1.sps, trak->mdia.stbl.stsd.avc1.spsLength);
        WriteU8(builder, 1); // number_picture_parameter_sets
        WriteU16(builder, trak->mdia.stbl.stsd.avc1.ppsLength);
        WriteRAW(builder, trak->mdia.stbl.stsd.avc1.pps, trak->mdia.stbl.stsd.avc1.ppsLength);

        EndContainer(builder); //avcC
    }

    EndContainer(builder); // avc1
}

static void WriteALAW(LTMediaMP4_Builder * builder, TrakData * trak) {
    // BeginContainer(builder, "alaw");
    // TODO: Temporarily change this label to ulaw since we are only supporting formatted ulaw audio
    //       LTMediaMP4 should be updated to allow either ulaw or alaw to be used without hardcoding.
    BeginContainer(builder, "ulaw");

    for (int i=0; i<3; i++) WriteU16(builder, 0); //reserved
    WriteU16(builder, 1); // data reference index

    // AudioSampleEntry
    for (int i=0; i<4; i++) WriteU16(builder, 0); // reserved
    WriteU16(builder, trak->mdia.stbl.stsd.alaw.channel_count);
    WriteU16(builder, trak->mdia.stbl.stsd.alaw.sample_size);
    WriteU32(builder, 0); // reserved
    WriteU32(builder, trak->mdia.stbl.stsd.alaw.samplerate);

    EndContainer(builder);
}

static void WriteSTBL(LTMediaMP4_Builder * builder, TrakData * trak) {
    BeginContainer(builder, "stbl");

    // [stsd]
    {
        BeginContainerExt(builder, "stsd", 0, 0);

        WriteU32(builder, 1); // entry count
        if (trak->mdia.handler_type == HandlerType_Video) {
            WriteAVC1(builder, trak);
        } else if (trak->mdia.handler_type == HandlerType_Sound) {
            WriteALAW(builder, trak);
        }
        EndContainer(builder); // stsd
    }

    // [stts]
    // Time between samples / Duration of each sample
    {
        BeginContainerExt(builder, "stts", 0, 0);
        // Write all stts entries
        LTArray * sampleDurations = trak->samples.sampleDurations;
        if (sampleDurations->API->GetCount(sampleDurations) == trak->samples.nSamples) {
            // Save the position of this container and write a placeholder value for the number of entries.
            // The correct number of entries will be written to at the end of this if-block
            u32 entryCountPos = builder->nOffset;
            PushContainerEntry(builder, "stts"); // keeps track of number of entries
            WriteU32(builder, 0);

            // Write the duration of each sample. 
            // Each entry is a run of consecutive samples with the same duration.
            u32 lastSampleDuration = 0;
            u32 consecutiveSamples = 0;
            u32 nEntries = 0;
            for (u32 i = 0; i < trak->samples.nSamples; i++) {
                u32 * pCurrentDuration = sampleDurations->API->Get(sampleDurations, i, NULL);
                // Initialize lastSampleDuration to the first duration
                if (i == 0) lastSampleDuration = *pCurrentDuration;

                // Write an entry when last duration differs from current duration
                if (*pCurrentDuration != lastSampleDuration) {
                    WriteU32(builder, consecutiveSamples); // number of samples
                    WriteU32(builder, lastSampleDuration); // duration of sample
                    P("stts.entry", "%lu: %lu", LT_Pu32(consecutiveSamples), LT_Pu32(lastSampleDuration));
                    consecutiveSamples = 0;
                    ++nEntries;
                }
                // Update current run
                ++consecutiveSamples;
                lastSampleDuration = *pCurrentDuration;
            }
            // Write the final run
            WriteU32(builder, consecutiveSamples); // number of samples
            WriteU32(builder, lastSampleDuration); // duration of sample
            ++nEntries;

            ContainerData * currentContainer = builder->containerStack->API->Get(builder->containerStack, 0, NULL);
            nEntries = LT_HTONL(nEntries);
            builder->insertCallback(builder, entryCountPos, (u8 *)&nEntries, sizeof(u32), currentContainer->containerName, builder->clientData);
            builder->containerStack->API->Remove(builder->containerStack, 0);
        } else {
            // No sample duration array was provided, so assume all samples are the same length
            WriteU32(builder, 1); // entry count
            WriteU32(builder, trak->samples.nSamples); // number of samples
            WriteU32(builder, trak->samples.nSampleDelta); // duration of each sample
        }
        EndContainer(builder);
    }

    // [stss] (optional)
    // Indexes of sync samples
    if (trak->samples.syncSampleIndexes) {
        BeginContainerExt(builder, "stss", 0, 0);
        LTArray * syncSampleIndexes = trak->samples.syncSampleIndexes;
        u32 nSyncSamples = syncSampleIndexes->API->GetCount(syncSampleIndexes);
        WriteU32(builder, nSyncSamples);
        for (u32 i = 0; i < nSyncSamples; i++) {
            u32 * val = syncSampleIndexes->API->Get(syncSampleIndexes, i, NULL);
            WriteU32(builder, *val);
        }
        EndContainer(builder);
    }

    // [stsc]
    // Samples Per Chunk. Compactly coded.
    {
        BeginContainerExt(builder, "stsc", 0, 0);

        // TODO: Do compact coding.
        LTArray * chunks = trak->samples.chunks;
        u32 nChunks = chunks->API->GetCount(chunks);
        WriteU32(builder, nChunks);
        for (u32 i = 0; i < nChunks; i++) {
            Chunk chunk = {};
            chunks->API->Get(chunks, i, &chunk);
            WriteU32(builder, i+1); // first chunk, 1-indexed
            WriteU32(builder, chunk.nSamples); // samples in chunk
            WriteU32(builder, 1); // sample description index
        }

        EndContainer(builder);
    }

    // [stsz]
    // Sample sizes
    {
        BeginContainerExt(builder, "stsz", 0, 0);
        WriteU32(builder, trak->samples.nSampleSize);
        WriteU32(builder, trak->samples.nSamples);
        if (trak->samples.sampleSizes) {
            for (u32 i = 0; i < trak->samples.nSamples; i++) {
                u32 * val = trak->samples.sampleSizes->API->Get(trak->samples.sampleSizes, i, NULL);
                WriteU32(builder, *val);
            }
        }
        EndContainer(builder);
    }

    // [stco]
    // Chunk offset information
    {
        BeginContainerExt(builder, "stco", 0, 0);
        LTArray * chunks = trak->samples.chunks;
        u32 nChunks = chunks->API->GetCount(chunks);
        WriteU32(builder, nChunks);
        for (u32 i = 0; i < nChunks; i++) {
            Chunk chunk = {};
            chunks->API->Get(chunks, i, &chunk);
            WriteU32(builder, chunk.offset);
        }
        EndContainer(builder);
    }

    EndContainer(builder); // stbl
}

static void WriteTRAK(LTMediaMP4_Builder * builder, TrakData * trak) {
    BeginContainer(builder, "trak");

    // [tkhd]
    {
        BeginContainerExt(builder, "tkhd", 0, trak->tkhd.flags);

        WriteU32(builder, 0); // creation
        WriteU32(builder, 0); // modification
        WriteU32(builder, trak->tkhd.track_id);
        WriteU32(builder, 0); // reserved
        WriteU32(builder, trak->tkhd.duration);

        WriteU32(builder, 0); // reserved
        WriteU32(builder, 0); // reserved
        WriteU16(builder, 0); // layer
        WriteU16(builder, 0); // alternate group
        WriteU16(builder, 0); // volume
        WriteU16(builder, 0); // reserved
        for (int i = 0; i < 9; i++) WriteU32(builder, UnitaryMatrix[i]);
        WriteU32(builder, trak->tkhd.width);
        WriteU32(builder, trak->tkhd.height);

        EndContainer(builder); // tkhd
    }

    // [edts]
    if (trak->edts.elst.nEntries > 0) {
        BeginContainer(builder, "edts");

        // [elst]
        {
            BeginContainerExt(builder, "elst", 0, 0); // size, type, version, flags
            WriteU32(builder, trak->edts.elst.nEntries);
            // entry 1 (12 bytes)
            WriteU32(builder, trak->edts.elst.entry1.duration);
            WriteU32(builder, trak->edts.elst.entry1.mediaTime);
            WriteU16(builder, trak->edts.elst.entry1.mediaRateWhole);
            WriteU16(builder, trak->edts.elst.entry1.mediaRateFraction);

            EndContainer(builder);
        }

        EndContainer(builder);
    }

    // [mdia]
    {
        BeginContainer(builder, "mdia");

        // [mdhd]
        {
            BeginContainerExt(builder, "mdhd", 0, 0);

            WriteU32(builder, 0); // creation
            WriteU32(builder, 0); // modification
            WriteU32(builder, trak->mdia.mdhd.timescale);
            WriteU32(builder, trak->mdia.mdhd.duration);
            WriteU16(builder, LangUND);
            WriteU16(builder, 0); // padding

            EndContainer(builder); // mdhd
        }

        // [hdlr]
        {
            BeginContainerExt(builder, "hdlr", 0, 0);

            WriteU32(builder, 0); // padding
            switch (trak->mdia.handler_type) {
                case HandlerType_Video: WriteU32(builder, HandlerVideU32); break;
                case HandlerType_Sound: WriteU32(builder, HandlerSoun); break;
                default:                WriteU32(builder, 0); break;
            }
            for (int i=0; i<3; i++) WriteU32(builder, 0); //reserved
            if (trak->mdia.handler_string) {
                WriteRAW(builder, trak->mdia.handler_string, lt_strlen(trak->mdia.handler_string)+1);
            } else {
                const char unknown[] = "Unknown";
                WriteRAW(builder, unknown, sizeof(unknown));
            }

            EndContainer(builder); //hdlr
        }

        // [minf]
        {
            BeginContainer(builder, "minf");

            // [vmhd]
            if (trak->mdia.handler_type == HandlerType_Video) {
                BeginContainerExt(builder, "vmhd", 0, 1);
                WriteU16(builder, 0); // graphics mode
                for (int i=0; i<3; i++) WriteU16(builder, 0); // opcolor
                EndContainer(builder); // vmhd
            }

            // [smhd]
            if (trak->mdia.handler_type == HandlerType_Sound) {
                BeginContainerExt(builder, "smhd", 0, 0);
                WriteU16(builder, 0); // balance
                WriteU16(builder, 0); // reserved
                EndContainer(builder); // smhd
            }

            // [dinf]
            {
                BeginContainer(builder, "dinf");
                BeginContainerExt(builder, "dref", 0, 0);
                WriteU32(builder, 1); // entry count
                BeginContainerExt(builder, "url ", 0, 1);
                // no data = local to file
                EndContainer(builder); // dref
                EndContainer(builder); // url
                EndContainer(builder); // dinf
            }

            WriteSTBL(builder, trak);

            EndContainer(builder);
        }

        EndContainer(builder); // mdia
    }
    P("trak","ending container");
    EndContainer(builder); // trak
}

static void WriteMOOV(LTMediaMP4_Builder * builder, MoovData * moov) {
    BeginContainer(builder, "moov");

    BeginContainerExt(builder, "mvhd", 0, 0);
    WriteU32(builder, 0); // creation
    WriteU32(builder, 0); // modification
    WriteU32(builder, moov->mvhd.timescale);
    WriteU32(builder, moov->mvhd.duration);
    WriteU32(builder, 0x00010000); // rate
    WriteU16(builder, 0x0100); // volume
    WriteU16(builder, 0); // reserved
    WriteU32(builder, 0); // reserved
    WriteU32(builder, 0); // reserved
    for (int i = 0; i < 9; i++) WriteU32(builder, UnitaryMatrix[i]); // transform matrix
    for (int i = 0; i < 6; i++) WriteU32(builder, 0); // padding
    WriteU32(builder, -1); // next available track id; all 1's = need to search when adding track
    EndContainer(builder); // mvhd

    WriteTRAK(builder, moov->videoTrak);
    if (moov->audioTrak) {
        WriteTRAK(builder, moov->audioTrak);
    }

    P("end.moov", "finalizing moov container");
    EndContainer(builder); // moov
}

/*******************************************************************************
 * Public interface
 ******************************************************************************/

static LTMediaMP4_Builder * CreateGenericBuilder(LTMediaMP4_AudioConfig config, LTMediaMP4_WriteCallback *writeCallback, LTMediaMP4_InsertCallback *insertCallback, void * clientData) {

    P("create.gen.builder", NULL);
    LTMediaMP4_Builder * builder = lt_malloc(sizeof(LTMediaMP4_Builder));
    lt_memset(builder, 0, sizeof(*builder));
    builder->hasWritten = false;

    builder->containerStack  = lt_createobject_typed(LTArray, List);
    builder->writeCallback   = writeCallback;
    builder->insertCallback  = insertCallback;
    builder->clientData      = clientData;

    builder->video.sampleSizes       = LTArray_CreateStructArray(sizeof(u32));
    builder->video.sampleDurations   = LTArray_CreateStructArray(sizeof(u32));
    builder->video.syncSampleIndexes = LTArray_CreateStructArray(sizeof(u32));
    builder->video.chunks            = LTArray_CreateStructArray(sizeof(Chunk));

    builder->audio.config            = config;
    builder->audio.chunks            = LTArray_CreateStructArray(sizeof(Chunk));
    builder->audio.sampleDurations   = LTArray_CreateStructArray(sizeof(u32));
    return builder;
}

// TODO: This is specifically an MP4FileBuilder and will be moved into its own object
static LTMediaMP4_Builder * CreateBuilder(LTMediaMP4_AudioConfig config, LTFile *outputFile) { 
    LTMediaMP4_Builder * builder = CreateGenericBuilder(config, &FileBuilderWrite, &FileBuilderInsertSize, NULL);
    builder->outputFile = outputFile;
    if (!outputFile->API->IsOpen(outputFile)) outputFile->API->Open(outputFile, true);
    return builder;
}

static void FreeBuilder(LTMediaMP4_Builder * builder) {
    if (builder->outputFile) {
        builder->outputFile->API->Close(builder->outputFile);
    }

    lt_destroyobject(builder->video.sampleSizes);
    lt_destroyobject(builder->video.sampleDurations);
    lt_destroyobject(builder->video.syncSampleIndexes);
    lt_destroyobject(builder->video.chunks);

    lt_free(builder->video.sps);
    lt_free(builder->video.pps);

    lt_free(builder->h264LastSps.pData);
    builder->h264LastSps.nDataLen = 0;
    lt_free(builder->h264LastPps.pData);
    builder->h264LastPps.nDataLen = 0;

    while (builder->containerStack->API->GetCount(builder->containerStack) > 0) {
        FreeContainerEntry(builder);
    }
    lt_destroyobject(builder->containerStack);
    lt_destroyobject(builder->audio.chunks);
    lt_destroyobject(builder->audio.sampleDurations);

    lt_free(builder);
}

static void EnsureChunkType(LTMediaMP4_Builder * builder, int trak) {
    LTArray * chunks = (trak == MP4_BUILDER_TRAK_VIDEO) ? builder->video.chunks : builder->audio.chunks;
    if (builder->currentTrak != trak) {
        builder->currentTrak = trak;
        chunks->API->Append(chunks, &(Chunk) { .offset = builder->nOffset });
    }

    u32 nChunks = chunks->API->GetCount(chunks);
    Chunk chunk = {};
    chunks->API->Get(chunks, nChunks - 1, &chunk);

    // Start new chunk if current one is too large
    if (builder->nOffset - chunk.offset > MP4_MAX_CHUNK_SIZE  || chunk.nSamples > MP4_MAX_CHUNK_SAMPLES) {
        chunks->API->Append(chunks, &(Chunk) { .offset = builder->nOffset });
    }
}

static void IncrementChunkSampleCount(LTMediaMP4_Builder * builder, int trak, u32 count) {
    LTArray * chunks = (trak == MP4_BUILDER_TRAK_VIDEO) ? builder->video.chunks : builder->audio.chunks;
    u32 nChunks = chunks->API->GetCount(chunks);

    Chunk chunk = {};
    chunks->API->Get(chunks, nChunks - 1, &chunk);
    chunk.nSamples += count;
    chunks->API->Set(chunks, nChunks - 1, &chunk);
}

static bool UpdateBytes(Bytes * bytes, u8 * pData, u32 nDataLen) {
    if (bytes->nDataLen != nDataLen) {
        bytes->nDataLen = nDataLen;
        lt_free(bytes->pData);
        bytes->pData = lt_malloc(nDataLen);
        if (!pData) {
            LTLOG_DEBUG("pps.sps.OOM", "sps and pps data unable to be saved, mp4 may become malformed");
            return false;
        }
    }
    lt_memcpy(bytes->pData, pData, nDataLen);
    return true;
}

static bool AddNALUnit(LTMediaMP4_Builder * builder, u8 * bytes, u32 length) {
    InitializeMP4IfNeeded(builder);

    // Skip start code if it's present
    if (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 1) {
        bytes += 4; // skip 00 00 00 01 header
        length -= 4;
    }
    u8 nal_type = *bytes & 0x1F;

    EnsureChunkType(builder, MP4_BUILDER_TRAK_VIDEO);

    if (MP4_KEEP_PPS_SPS_UNITS || nal_type == kLTMediaNalTypeIFrame || nal_type == kLTMediaNalTypePFrame || nal_type == kLTMediaNalTypeSei) {
        // Convert from H264 Annex B format to length format for MP4 file
        if (!WriteU32(builder, length)) return false;
        if (!WriteRAW(builder, bytes, length)) return false;
    }

    if (nal_type == kLTMediaNalTypeSps && !builder->video.sps) {
        P("nal.sps", NULL);
        builder->video.sps = lt_malloc(length);
        if (!builder->video.sps) return false;
        lt_memcpy(builder->video.sps, bytes, length);
        builder->video.spsLength = length;
    }

    if (nal_type == kLTMediaNalTypePps && !builder->video.pps) {
        P("nal.pps", NULL);
        builder->video.pps = lt_malloc(length);
        if (!builder->video.pps) return false;
        lt_memcpy(builder->video.pps, bytes, length);
        builder->video.ppsLength = length;
    }

    if (nal_type == kLTMediaNalTypeSei) {
        /* Accumulate SEI sizes, plus additional 4 bytes for each length prefix */
        builder->video.seiLength += length + 4;
    }

    // Only video frames beyond this point
    if (nal_type != kLTMediaNalTypeIFrame && nal_type != kLTMediaNalTypePFrame) {
        return true;
    }

    builder->video.nSamples++;
    // Add sample duration of "1 frame" for every new video sample
    u32 sampleDuration = 1;
    builder->video.sampleDurations->API->Append(builder->video.sampleDurations, &sampleDuration);

    u32 sampleSize = length + 4 + builder->video.seiLength;
    builder->video.seiLength = 0;   /* Reset SEI length for next sample */

    if (MP4_KEEP_PPS_SPS_UNITS) {
        // PPS and SPS NAL units are considered as part of the video frame sample.
        // Include their length padding
        sampleSize += builder->video.spsLength + builder->video.ppsLength + 8;
    }
    builder->video.sampleSizes->API->Append(builder->video.sampleSizes, &sampleSize);

    if (nal_type == kLTMediaNalTypeIFrame) {
        P("array", "IFrame Array Append");
        builder->video.syncSampleIndexes->API->Append(builder->video.syncSampleIndexes, &builder->video.nSamples); // 1-indexed
    }

    IncrementChunkSampleCount(builder, MP4_BUILDER_TRAK_VIDEO, 1);
    return true;
}

static bool AddH264StreamData(LTMediaMP4_Builder * builder, u8 * bytes, u32 length) {
    // Skip start code if present
    if (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 1) {
        bytes += 4; // skip 00 00 00 01 header
        length -= 4;
    }
    u8 nal_type = *bytes & 0x1F;

    switch (nal_type) {
        case kLTMediaNalTypePps:
            if (!UpdateBytes(&builder->h264LastPps, bytes, length))
                return false;
            break;
        case kLTMediaNalTypeSps:
            if (!UpdateBytes(&builder->h264LastSps, bytes, length))
                return false;
            break;
        case kLTMediaNalTypeSei:
            if (!AddNALUnit(builder, bytes, length))
                return false;
            break;
        case kLTMediaNalTypeIFrame:
        case kLTMediaNalTypePFrame:
            // Repeat sps and pps before every I and P frame
            if (builder->h264LastSps.pData)
                if (!AddNALUnit(builder, builder->h264LastSps.pData, builder->h264LastSps.nDataLen))
                    return false;
            if (builder->h264LastPps.pData)
                if (!AddNALUnit(builder, builder->h264LastPps.pData, builder->h264LastPps.nDataLen))
                    return false;
            if (!AddNALUnit(builder, bytes, length))
                return false;
            break;
        default:
            // ignore
            break;
    }
    return true;
}

static bool ExtendLastH264Frame(LTMediaMP4_Builder * builder, u32 frames) {
    // No frames in current video, so nothing to extend
    if (builder->video.nSamples == 0) return false;

    // Replace duration entry for last frame with new duration
    LTArray * sampleDurations = builder->video.sampleDurations;
    u32 latestFrameIndex = sampleDurations->API->GetCount(sampleDurations) - 1;
    u32 currentDuration = 0;
    sampleDurations->API->Get(sampleDurations, latestFrameIndex, &currentDuration);
    sampleDurations->API->Remove(sampleDurations, latestFrameIndex);
    currentDuration += frames;
    P("extend", "Last frame duration is now: %lu frames", LT_Pu32(currentDuration));
    sampleDurations->API->Append(sampleDurations, &currentDuration);
    return true;
}

static bool AddAudioSamples(LTMediaMP4_Builder * builder, u8 * bytes, u32 length) {
    InitializeMP4IfNeeded(builder);

    LT_ASSERT(length % builder->audio.config.nBytesPerSample == 0);
    u32 samples = length / builder->audio.config.nBytesPerSample;

    EnsureChunkType(builder, MP4_BUILDER_TRAK_AUDIO);

    if (!WriteRAW(builder, bytes, length)) return false;

    builder->audio.nSamples += samples;
    IncrementChunkSampleCount(builder, MP4_BUILDER_TRAK_AUDIO, samples);
    return true;
}

static u32 GetCurrentAudioSampleCount(LTMediaMP4_Builder * builder) {
    return builder ? builder->audio.nSamples : 0;
}

static u32 GetCurrentVideoSampleCount(LTMediaMP4_Builder * builder) {
    return builder ? builder->video.nSamples : 0;
}

static u32 GetCurrentSize(LTMediaMP4_Builder * builder) {
    return builder ? builder->nFileSize : 0;
}

static u32 GetCurrentVideoDurationMillis(LTMediaMP4_Builder * builder) {
    ParsedSPS sps = LTMediaMP4_ParseSPS(builder->video.sps, builder->video.spsLength);
    TimingInfo video_timing = ComputeReducedTimingInfo(
        sps.timing_info.time_scale,
        1 + sps.timing_info.num_units_in_tick, // Account for extra tick, see ITU Rec. H264 Annex D
        builder->video.nSamples,
        builder->video.sampleDurations
    );
    u32 durationMillis = (video_timing.duration * 1000) / video_timing.timescale;
    return durationMillis;
}

static LTMediaMP4_Stats Finish(LTMediaMP4_Builder * builder) {
    // init stats all to 0
    LTMediaMP4_Stats stats = {};

    if (!builder->hasWritten) {
        LTLOG_YELLOWALERT("fin.nodata", "No MP4 data to finalize");
        goto out;
    }

    bool hasAudio = builder->audio.nSamples > 0;
    if (!builder->video.sps) {
        LTLOG_YELLOWALERT("sps.err", "Missing SPS. Aborting.\n");
        goto out;
    }

    EndContainer(builder); // end mdat container before proceeding with creating moov container

    ParsedSPS sps = LTMediaMP4_ParseSPS(builder->video.sps, builder->video.spsLength);

    u32 video_width = 16 * (sps.pic_width_in_mbs_minus1 + 1)
        - 2 * sps.frame_crop_left_offset
        - 2 * sps.frame_crop_right_offset;

    u32 video_height = 16 * (sps.pic_height_in_map_units_minus1 + 1)
        - 2 * sps.frame_crop_top_offset
        - 2 * sps.frame_crop_bottom_offset;

    TimingInfo video_timing = ComputeReducedTimingInfo(
        sps.timing_info.time_scale,
        1 + sps.timing_info.num_units_in_tick, // Account for extra tick, see ITU Rec. H264 Annex D
        builder->video.nSamples,
        builder->video.sampleDurations
    );

    TimingInfo audio_timing = ComputeReducedTimingInfo(
        LT_MAX(builder->audio.config.nSamplesPerSec, (u32)1),
        1, // delta
        builder->audio.nSamples,
        builder->audio.sampleDurations
    );

    u32 timescale_common = LCM(video_timing.timescale, audio_timing.timescale);
    u32 video_duration_common = (timescale_common / video_timing.timescale) * video_timing.duration;
    u32 audio_duration_common = (timescale_common / audio_timing.timescale) * audio_timing.duration;
    u32 timescale_duration = LT_MAX(video_duration_common, audio_duration_common);

    TrakData videoTrak = {
        .tkhd = {
            .flags = TKHD_TRACK_ENABLED | TKHD_TRACK_IN_MOVIE,
            .track_id = 1,
            .width = video_width<<16,
            .height = video_height<<16,
            .duration = video_duration_common,
        },
        .samples = {
            .chunks = builder->video.chunks,
            .sampleSizes = builder->video.sampleSizes,
            .nSamples = builder->video.nSamples,
            .sampleDurations = builder->video.sampleDurations,
            .nSampleDelta = video_timing.delta,
            .syncSampleIndexes = builder->video.syncSampleIndexes,
        },
        .mdia = {
            .handler_type = HandlerType_Video,
            .handler_string = "VideoHandler",
            .mdhd = {
                .timescale = video_timing.timescale,
                .duration = video_timing.duration,
            },
            .stbl = {
                .stsd.avc1 = {
                    .width = video_width,
                    .height = video_height,
                    .profile = sps.profile_idc,
                    .profile_compat = 0,
                    .level = sps.level_idc,
                    .NALUnitLengthMinusOne = 3,
                    .sps = builder->video.sps,
                    .spsLength = builder->video.spsLength,
                    .pps = builder->video.pps,
                    .ppsLength = builder->video.ppsLength,
                },
            },
        },
    };

    TrakData audioTrak = {
        .tkhd = {
            .flags = TKHD_TRACK_ENABLED | TKHD_TRACK_IN_MOVIE,
            .track_id = 2,
            .duration = audio_duration_common,
        },
        .samples = {
            .nSampleDelta = audio_timing.delta,
            .chunks = builder->audio.chunks,
            .nSamples = builder->audio.nSamples,
            .sampleDurations = builder->audio.sampleDurations,
            .nSampleSize = builder->audio.config.nBytesPerSample,
        },
        .mdia = {
            .handler_type = HandlerType_Sound,
            .handler_string = "SoundHandler",
            .mdhd = {
                .timescale = audio_timing.timescale,
                .duration = audio_timing.duration,
            },
            .stbl = {
                .stsd.alaw = {
                    .channel_count = 1,
                    .sample_size = builder->audio.config.nBytesPerSample * 8,
                    .samplerate = builder->audio.config.nSamplesPerSec << 16,
                },
            },
        },
    };

    // Limit audio track duration to be video track duration if audio is longer
    if (MP4_LIMIT_AUDIO_DURATION_TO_VIDEO && hasAudio && audio_duration_common > video_duration_common) {
        // Create an edit list to chop off excess audio
        audioTrak.edts.elst.nEntries = 1;
        audioTrak.edts.elst.entry1.duration = video_duration_common,
        audioTrak.edts.elst.entry1.mediaTime = 0;
        audioTrak.edts.elst.entry1.mediaRateWhole = 1;
        audioTrak.edts.elst.entry1.mediaRateFraction = 0;

        // audio track needs to be updated with edited duration
        audioTrak.tkhd.duration = video_duration_common;

        // moov duration should reflect edited duration
        timescale_duration = video_duration_common;
    }

    MoovData moov = {
        .mvhd = {
            .timescale = timescale_common,
            .duration = timescale_duration,
        },
        .videoTrak = &videoTrak,
        .audioTrak = hasAudio ? &audioTrak : NULL,
    };

    WriteMOOV(builder, &moov);

    // Fill out the final mp4 stats
    stats.nAudioSamples = builder->audio.nSamples;
    stats.nVideoSamples = builder->video.nSamples;
    stats.nSize = builder->nFileSize;
    stats.nDurationMillis = (timescale_duration * 1000) / timescale_common;

out:
    FreeBuilder(builder);
    return stats;
}

static bool LTMediaMP4Impl_LibInit(void) {
    return true;
}

static void LTMediaMP4Impl_LibFini(void) {
}

define_LTLIBRARY_ROOT_INTERFACE(LTMediaMP4)
    .CreateBuilder                  = CreateBuilder,
    .CreateGenericBuilder           = CreateGenericBuilder,
    .AddH264StreamData              = AddH264StreamData,
    .AddNALUnit                     = AddNALUnit,
    .ExtendLastH264Frame            = ExtendLastH264Frame,
    .AddAudioSamples                = AddAudioSamples,
    .GetCurrentAudioSampleCount     = GetCurrentAudioSampleCount,
    .GetCurrentVideoSampleCount     = GetCurrentVideoSampleCount,
    .GetCurrentSize                 = GetCurrentSize,
    .GetCurrentVideoDurationMillis  = GetCurrentVideoDurationMillis,
    .Finish                         = Finish,
    .FreeBuilder                    = FreeBuilder,
LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTMediaMP4,
    (LTMediaMP4StreamerImpl)
);

