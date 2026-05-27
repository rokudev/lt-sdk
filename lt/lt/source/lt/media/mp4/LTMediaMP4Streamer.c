/*******************************************************************************
 * lt/source/media/mp4/LTMediaMP4Streamer.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTArray.h>
#include <lt/net/httpclient/LTNetHttpClient.h>
#include <lt/device/media/LTDeviceMedia.h>
#include <lt/media/mp4/LTMediaMP4.h>

/*******************************************************************************
 * Static variables
 ******************************************************************************/

DEFINE_LTLOG_SECTION("mp4.streamer");

// #define P(...) LTLOG(__VA_ARGS__)
#define PLOG(...) LTLOG(__VA_ARGS__)
// #define P(...) LTLOG_DEBUG(__VA_ARGS__)
#define P(...)

typedef struct {
    void *               stream;
    u8 *                 buffer;
    u32                  size;
    u32                  bytesUsed;
    bool                 endPart;
} Buffer; // buffer for producer thread

enum { // buffer size was chosen arbitrarily
    kBufferBlockSize = 20480,
};

//============================================================================Object Implementation========================================================================

typedef_LTObjectImpl(LTMediaMP4Streamer, LTMediaMP4StreamerImpl) {
    LTMediaMP4_Builder * builder; // our "generic" builder

    // variables
    bool            bodyReady;          // true = the body is ready to be written into 
    bool            moovWrite;          // true = moov is being written so halt the upload process
    u32             bytesInBody;        // debug tool used to keep track of file size
    u8 *            mdatSize;           // mdat size
    LTOThread *     uploadMP4Thread; 
    Buffer *        currentWrite;       // buffer to write MP4 data into
    LTMutex *       bufferMutex;        // mutex for modifying bufferList
    LTEvent         uploadStatusEvent;  // TODO: this should update the main thread when the upload thread is finished

    // http client libraries
    LTNetHttpClient          *Http;
    LTString                  httpUploadUrl;

    LTArray                  *bufferList;
    LTArray                  *pointerList;
    LTHttpRequest             bodyBufferRequest;
    LTMediaMP4               *MediaMP4;
} LTOBJECT_API;

//-------------------------------------------------Helper Function-----------------------------------------------

static Buffer *MallocBufferEntry(LTMediaMP4StreamerImpl * stream, u32 nBytes) {
    Buffer *buf = lt_malloc(sizeof(Buffer) + nBytes);
    if (!buf) {
        LTLOG_YELLOWALERT("streamer.buffer", "failed to malloc new buffer, video may be deformed or missing data");
        return NULL;
    }
    /* Init new buffer entry */
    buf->stream    = stream;
    buf->size      = kBufferBlockSize;
    buf->bytesUsed = 0;
    buf->endPart   = false;
    // Point buffer to the block allocated after initial structure
    buf->buffer = (u8 *)&buf[1];
    return buf;
}

void AppendToBufferList(LTMediaMP4StreamerImpl * stream, Buffer * newEntry) {
    stream->bufferMutex->API->Lock(stream->bufferMutex);
    stream->bufferList->API->Append(stream->bufferList, newEntry);
    stream->bufferMutex->API->Unlock(stream->bufferMutex);
}

//-------------------------------------------------Thread/Libraries-----------------------------------------------

static void CleanupStream(LTMediaMP4StreamerImpl * stream) {
    lt_destroyobject(stream->bufferList);
    lt_destroyobject(stream->pointerList);
    stream->uploadMP4Thread->API->Terminate(stream->uploadMP4Thread);
    lt_closelibrary(stream->Http);
}

static bool InitStreamInterface(LTMediaMP4StreamerImpl *stream) { 
    P("init.lib", "initializing libraries");
    do {
        stream->Http = lt_openlibrary(LTNetHttpClient);
        if (!stream->Http) break;
        stream->MediaMP4 = lt_openlibrary(LTMediaMP4);
        if (!stream->MediaMP4) break;
        P("init.lib", "finished libraries");
        return true;
    } while (false);
    CleanupStream(stream);
    return false; 
}

static bool InitThread(LTMediaMP4StreamerImpl *stream) { 
    P("init.thread", "initializing thread");
    stream->uploadMP4Thread = lt_createobject(LTOThread); // generate our new thread
    stream->uploadMP4Thread->API->SetStackSize(stream->uploadMP4Thread, 2048);
    stream->uploadMP4Thread->API->Start(stream->uploadMP4Thread, "uploadMP4Thread", NULL, NULL);
    return true;
}

static void LTMediaMP4StreamerImpl_SetURL(LTMediaMP4StreamerImpl *stream, const char * url) {
    ltstring_set(&stream->httpUploadUrl, url);
}

//------------------------------------------------Thread Events--------------------------------------------

//TODO: make uploadMP4Thread notify events to this - when the thread is finished uploading, finish the main function
// static void OnUploadThreadEvent(LTMediaMP4StreamerImpl *stream, LTMediaMP4StreamerEvent event, void *clientData) {
//     LT_UNUSED(stream);
//     LT_UNUSED(clientData);
//     switch(event) {
//     case kLTMediaMP4Streamer_Done:
//         break;
//     case kLTMediaMP4Streamer_Error:
//         break;
//     default:
//         break;
//     }
// }

//-------------------------------------------------HTTP Logic-----------------------------------------------

static void OnHTTPRequestEvent(LTHttpRequest hRequest, LTHttpRequestEvent event, void *clientData) {
    LT_UNUSED(hRequest);
    LTMediaMP4StreamerImpl *stream = clientData;
    P("http", "event: %lu", LT_Pu32(event));

    switch (event) {
    case LTHttpRequestEvent_BodyWriteReady: 
        P("http.data", "size of array is %u", stream->bufferList->API->GetCount(stream->bufferList));
        stream->bodyReady = true;
        break;
    case LTHttpRequestEvent_HeadComplete:
        P("http.head", "head complete");
        break;
    case LTHttpRequestEvent_BodyDataAvailable:
        P("http.data", "req body data available");
        break;
    case LTHttpRequestEvent_RequestComplete:
        P("http.done", "req complete");
        stream->bytesInBody = 0; // clear it for the next write
        stream->bodyReady = false;
        break;
    case LTHttpRequestEvent_Disconnected:
    case LTHttpRequestEvent_DisconnectPending:
    case LTHttpRequestEvent_Error:    
    default:
        LTLOG("error", "error");
        break;
    }
}

static void DrainBufferQueueToSocket(void *clientData) {
    LTMediaMP4StreamerImpl *stream = clientData; // stream is passed in 
    ILTHttpRequest *request = lt_gethandleinterface(ILTHttpRequest, stream->bodyBufferRequest);

    if (stream->bufferList->API->GetCount(stream->bufferList) == 0) {
        // stop writing for now if the list is empty
        return;
    }

    if (!stream->bodyReady || stream->moovWrite) { 
        // if the body isn't ready to write
        P("body.write.ready", "halted writing");
        stream->uploadMP4Thread->API->QueueTaskProc(stream->uploadMP4Thread, &DrainBufferQueueToSocket, NULL, stream);
        return;
    }

    stream->bufferMutex->API->Lock(stream->bufferMutex);
    Buffer * currentEntry = stream->bufferList->API->Get(stream->bufferList, 0, NULL);
    stream->bufferMutex->API->Unlock(stream->bufferMutex);
    s32 written = request->StreamBodyData(stream->bodyBufferRequest, currentEntry->buffer, currentEntry->bytesUsed);

    while (written == 0) {
        written = request->StreamBodyData(stream->bodyBufferRequest, currentEntry->buffer, currentEntry->bytesUsed);
    }

    if (written == LTHttpStreamError) {
        stream->bodyReady = false;
        P("body.write.err", "error!");
        return;
    } 

    if (written > 0) {
        if (currentEntry->endPart) { // we need to end the part 
            request->WritePartBoundary(stream->bodyBufferRequest); 
        }
        stream->bytesInBody += written;
        stream->bufferMutex->API->Lock(stream->bufferMutex);
        stream->bufferList->API->Remove(stream->bufferList, 0);
        stream->bufferMutex->API->Unlock(stream->bufferMutex);
        lt_free(currentEntry);
    }

    P("body.write.suc", "size of array is %u  |  total written is %u", stream->bufferList->API->GetCount(stream->bufferList), stream->bytesInBody);
    stream->uploadMP4Thread->API->QueueTaskProc(stream->uploadMP4Thread, &DrainBufferQueueToSocket, NULL, stream);       
}

static void RegisterForHttpEvent(void * clientData) {
    LTMediaMP4StreamerImpl *stream = clientData;
    ILTHttpRequest *request = lt_gethandleinterface(ILTHttpRequest, stream->bodyBufferRequest);
    // register for events in UploadMP4Thread
    request->OnRequestEvent(stream->bodyBufferRequest, &OnHTTPRequestEvent, stream); 
}

static bool InitRequest(LTMediaMP4StreamerImpl *stream) {
    // generate a new request
    stream->bodyBufferRequest = stream->Http->CreateRequest(stream->httpUploadUrl, kLTHttpRequestMethod_POST, 0);
    stream->uploadMP4Thread->API->QueueTaskProc(stream->uploadMP4Thread, &RegisterForHttpEvent, NULL, stream); // register for event
    ILTHttpRequest *request = lt_gethandleinterface(ILTHttpRequest, stream->bodyBufferRequest);

    // send a new request
    LTHttpMultipartData formData[4] = {
            {
                .name        = "file",
                .contentType = "video/mp4",
                .filename    = "p1.mp4",
                .data        = NULL
            },
            {
                .name        = "file",
                .contentType = "video/mp4",
                .filename    = "p3.mp4",
                .data        = NULL
            },
            {
                .name        = "file",
                .contentType = "video/mp4",
                .filename    = "p4.mp4",
                .data        = NULL
            },
            {
                .name        = "file",
                .contentType = "video/mp4",
                .filename    = "p2.mp4",
                .data        = NULL
            }
    };
    request->SetBody(stream->bodyBufferRequest, "multipart/form-data", formData, 4);
    if (!request->SendRequest(stream->bodyBufferRequest)) {return false;}
    return true;
}


//-------------------------------------------------Write Logic-----------------------------------------------

static void StreamerWrite(LTMediaMP4StreamerImpl *stream, const u8 * bytes, u32 length) {
    u32 unwrittenBytes = length;
    Buffer *upload = NULL;
    u32 writeOffset = 0;
    /* Write until all given data is exhausted. 3 possible cases:
     * 1. length of data exceeds the current capacity of the buffer
     * 2. length of data equal to the current capacity of the buffer
     * 3. length of the data less than the current capacity of the buffer   
     */
    while (unwrittenBytes > 0) {
        if (!stream->currentWrite){
            LTLOG_YELLOWALERT("stream.write.err","failed to allocate buffer to write into");
            return;
        }
        
        u32 bufferBytesRemain = stream->currentWrite->size - stream->currentWrite->bytesUsed; // calculate how much space is left to write into the buffer
        u32 toWrite = LT_MIN(unwrittenBytes, bufferBytesRemain);

        if (toWrite > 0) { // if we need to write, send it up
            lt_memcpy(stream->currentWrite->buffer + stream->currentWrite->bytesUsed, bytes + writeOffset, toWrite); // write into the buffer with that amount
            stream->currentWrite->bytesUsed += toWrite; // update the size of the buffer
            unwrittenBytes -= toWrite;
            writeOffset += toWrite; // update the pointer of the bytes
            bufferBytesRemain -= toWrite;
        }
        
        if (bufferBytesRemain == 0) { // if the buffer is full, we need to upload it
            upload = stream->currentWrite; 
            if (!upload) {return;}
            AppendToBufferList(stream, upload);
            stream->uploadMP4Thread->API->QueueTaskProcIfRequired(stream->uploadMP4Thread, &DrainBufferQueueToSocket, NULL, stream);
            stream->currentWrite = MallocBufferEntry(stream, kBufferBlockSize);
        }
    }
}

static void EndCurrentPart(LTMediaMP4StreamerImpl *stream) {
    Buffer *current = stream->currentWrite;
    current->endPart = true;
    AppendToBufferList(stream, current);
    stream->uploadMP4Thread->API->QueueTaskProcIfRequired(stream->uploadMP4Thread, &DrainBufferQueueToSocket, NULL, stream);
    stream->currentWrite = MallocBufferEntry(stream, kBufferBlockSize); 
}

static void WriteFinalMDATData(LTMediaMP4StreamerImpl *stream, const void * size, u32 length) {
    Buffer * mdatBuf = MallocBufferEntry(stream, kBufferBlockSize);
    lt_memcpy(mdatBuf->buffer + mdatBuf->bytesUsed, size, length);
    mdatBuf->bytesUsed += length;
    mdatBuf->endPart = true;
    AppendToBufferList(stream, mdatBuf);
    stream->uploadMP4Thread->API->QueueTaskProcIfRequired(stream->uploadMP4Thread, &DrainBufferQueueToSocket, NULL, stream);
}

//-------------------------------------------------Write/Seek Functions-----------------------------------------------

static u32 StreamWrite_Callback(LTMediaMP4_Builder *builder, const u8 * bytes, u32 length, bool isStart, const char * containerName, void * clientData) {
    LT_UNUSED(builder);
    LTMediaMP4StreamerImpl * stream = clientData;

    if (isStart) { 
        u8 * pointer = stream->currentWrite->buffer + stream->currentWrite->bytesUsed;
        stream->pointerList->API->Insert(stream->pointerList, 0, pointer); // append to the list
        if (lt_strcmp(containerName, "mdat") == 0) {
           return 4; // return 4 to trick the program into thinking we wrote in mdat's size 
        } 
    }

    StreamerWrite(stream, bytes, length);
    return length;
}

static u32 StreamInsert_Callback(LTMediaMP4_Builder *builder, u32 position, const u8 * data, u32 length, const char * containerName, void * clientData) {
    LT_UNUSED(builder);
    LTMediaMP4StreamerImpl * stream = clientData;
    LT_UNUSED(position);
    u8 * currentContainer = stream->pointerList->API->Get(stream->pointerList, 0, NULL);
    if (lt_strcmp(containerName, "mdat") == 0) {
        stream->mdatSize = lt_malloc(length);
        if (!stream->mdatSize) {
            LTLOG_YELLOWALERT("mdat.size", "failed to store mdat size, video may be corrupt");
            return 0;
        }
        lt_memcpy(stream->mdatSize, data, length);
        EndCurrentPart(stream);
        stream->moovWrite = true;
    } else if (lt_strcmp(containerName, "moov") == 0 || lt_strcmp(containerName, "ftyp") == 0) {
        lt_memcpy(currentContainer, data, length);
        EndCurrentPart(stream);
        stream->moovWrite = false;
        if (lt_strcmp(containerName, "moov") == 0) {stream->bodyReady = true;}
    } else {
        lt_memcpy(currentContainer, data, length);
    }
    stream->pointerList->API->Remove(stream->pointerList, 0); // pop the pointer
    return length;
}   


// TODO: the finish is currently a race condition and will block the caller
static void LTMediaMP4StreamerImpl_Finish(LTMediaMP4StreamerImpl *stream) {
    P("destruct.streamer", "finished builder");
    stream->MediaMP4->Finish(stream->builder);
    WriteFinalMDATData(stream, stream->mdatSize, sizeof(u32));
    lt_free(stream->mdatSize);
    while (true) {
        stream->bufferMutex->API->Lock(stream->bufferMutex);
        if (stream->bufferList->API->GetCount(stream->bufferList) == 0) {
            stream->bufferMutex->API->Unlock(stream->bufferMutex);
            break;
        }
        stream->bufferMutex->API->Unlock(stream->bufferMutex);
    }
    CleanupStream(stream);
}


static bool LTMediaMP4StreamerImpl_Start(LTMediaMP4StreamerImpl *stream, LTMediaMP4_AudioConfig config) { // set URL here
    P("stream.start", "starting stream");
    stream->builder = stream->MediaMP4->CreateGenericBuilder(config, &StreamWrite_Callback, &StreamInsert_Callback, stream); // initialize the builder
    stream->currentWrite = MallocBufferEntry(stream, kBufferBlockSize);
    if (!stream->currentWrite || !stream->builder || !InitRequest(stream)) {
        if (stream->builder) {
            stream->MediaMP4->FreeBuilder(stream->builder);
        }
        lt_destroyobject(stream->bufferList);
        lt_destroyobject(stream->pointerList);
        stream->uploadMP4Thread->API->Terminate(stream->uploadMP4Thread);
        return false;
    }

    // stream->uploadStatusEvent = LT_GetCore()->CreateEvent();
    // ILTEvent * iEvent = lt_gethandleinterface(ILTEvent, stream->uploadStatusEvent); // TODO: implement events for finishing
    // iEvent->RegisterForEvent(stream->uploadStatusEvent, &OnUploadThreadEvent, NULL, stream, true); // TODO: alert user that the user is finished    
    stream->uploadMP4Thread->API->QueueTaskProc(stream->uploadMP4Thread, &DrainBufferQueueToSocket, NULL, stream);
    return true;
}

//-------------------------------------------------Constructor/Destructor-----------------------------------------------

static void LTMediaMP4StreamerImpl_DestructObject(LTMediaMP4StreamerImpl* streamer) {
    CleanupStream(streamer);
}

static bool LTMediaMP4StreamerImpl_ConstructObject(LTMediaMP4StreamerImpl* streamer) {
    P("stream.construct", "constructing streamer");
    if (!InitStreamInterface(streamer)) {return false;} // initialize library
    streamer->bytesInBody = 0; // set all data member variables 
    streamer->bodyReady = false;
    streamer->moovWrite = false;
    streamer->currentWrite = MallocBufferEntry(streamer, kBufferBlockSize);

    // TODO: Have this set by user input
    streamer->httpUploadUrl = ltstring_create("http://192.168.108.98:8000");
    if (!streamer->httpUploadUrl) {return false;}
    streamer->bufferList = lt_createobject_typed(LTArray, List);
    if (!streamer->bufferList) {return false;}
    streamer->pointerList = lt_createobject_typed(LTArray, List);
    if (!streamer->pointerList) {return false;}
    streamer->bufferMutex = lt_createobject(LTMutex);
    if (!streamer->bufferMutex) {return false;}

    InitThread(streamer);

    return true;
}

static LTMediaMP4_Builder * LTMediaMP4StreamerImpl_GetBuilder(LTMediaMP4StreamerImpl* streamer) { // TODO: try to make this invisible later
    return streamer->builder;
}

define_LTObjectImplPublic(LTMediaMP4Streamer, LTMediaMP4StreamerImpl,
    Start,
    Finish,
    SetURL,
    GetBuilder,
);

