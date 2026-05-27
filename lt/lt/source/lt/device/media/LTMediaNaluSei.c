/*******************************************************************************
 * Object definition for constructing Supplemental Enhancement Information NAL Units
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/
#include <lt/LT.h>
#include <lt/device/media/LTDeviceMedia.h>

DEFINE_LTLOG_SECTION("ltmedia.nalusei");


/*******************************************************************************
 * Static Variables & Types
 ******************************************************************************/

/* Miscellaneous constants used within this module */
enum {
    kSeiType_UserDataUnregistered = 0x05,   // SEI message sub-type as per ITU-T Rec H.264
    kSeiFragmentDelimiter = 0xFF,           // delimiter between SEI message fragments to avoid runs of 0x00 bytes
    kSeiRbspTrailingBits = 0x80,            // delimiter inserted at end of SEI NALU, see section 7.3.2.3 of ITU-T Rec H.264
    kSeiSizeOfUUID = 16,                    // 16-byte UUID at the start of each message fragment
};

typedef struct {
    LTList_Node node;
    u8 seiUUID[kSeiSizeOfUUID];
    u8* seiFragment;
    u16 seiFragmentSize;
} NaluSEIFragment;

typedef struct {
    u32 totalLength;    // metadata for building the SEI, not sent
    u8 *writePtr;       // metadata for building the SEI, not sent
    u8 nalPrefix[4];    // always 0x00.00.00.01
    u8 nalUnitType;     // always 0x06
    u8 seiContents[];   // variable-length array containing complete built SEI
    // rbsp_trailing_bits goes here
} NaluSEI;

typedef_LTObjectImpl(LTMediaNaluSei, LTMediaNaluSeiImpl) {
    LTList MessageFragments;
    NaluSEI *pEncodedSei;
} LTOBJECT_API;


/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

static u32
GetSeiMessageLength(u32 payloadLen, u8 *payload) {
    /* Scan the given payload looking for 0x00.00.0[0123] words, which need to have an emulation prevention byte inserted */
    u32 i, threeCount = 0;
    u32 msgLen = payloadLen;

    if (payloadLen >= 3) {
        for(i=0; i<payloadLen-2; i++) {
            if (payload[i]==0x00 && payload[i+1]==0x00 && (payload[i+2]==0x00 || payload[i+2]==0x01 || payload[i+2]==0x02 || payload[i+2]==0x03)) {
                threeCount++;
                i++;    // consume the i+1 0x00 from the input stream as well
            }
        }
    }

    /* Add on the size of the UUID and the FF delimiter */
    msgLen += kSeiSizeOfUUID + 1;

    /* Calculate the number of bytes required for the message size field (size accumulates 0xFF bytes until remaining size is <256) */
    u8 numSizeBytes = (msgLen + 254) / 255;
    if (! ((msgLen + 1) & 255)) {
        /* Corner case - the size was an exact multiple of 255, so an extra 0 byte will be stuffed */
        numSizeBytes++;
    }

    u32 encodedLen = msgLen + threeCount + numSizeBytes + 1; // Extra 1 byte for SEI payload type parameter
    if (payloadLen > 255) {
        LTLOG_DEBUG("sei.len", "%d => %d 3=%d sz=%d", payloadLen, encodedLen, threeCount, numSizeBytes);
    }
    return encodedLen;
}

#define READOUT_SEI_BUF 0

static void
AddSeiMessage(NaluSEI *sei, u8 *UUID, u32 payloadLen, u8 *payload) {
    //u8 *startPtr = sei->writePtr;
    *sei->writePtr++ = kSeiType_UserDataUnregistered;
    u32 remainingSize = payloadLen + kSeiSizeOfUUID + 1;    // +1 for delimiting 0xFF
    #if READOUT_SEI_BUF
    u32 sizeBytes = 1;
    #endif
    while (remainingSize > 254) {
        *sei->writePtr++ = 255;
        remainingSize -= 255;
        #if READOUT_SEI_BUF
        sizeBytes++;
        #endif
    }
    *sei->writePtr++ = remainingSize;
    lt_memcpy(sei->writePtr, UUID, kSeiSizeOfUUID);
    sei->writePtr += kSeiSizeOfUUID;

    u32 i;
    for(i=0; i<payloadLen-2; i++) {
        *sei->writePtr++ = payload[i];

        if (payload[i]==0x00 && payload[i+1]==0x00 && (payload[i+2]==0x00 || payload[i+2]==0x01 || payload[i+2]==0x02 || payload[i+2]==0x03)) {
            *sei->writePtr++ = 0x00;    // i+1
            *sei->writePtr++ = 0x03;    // stuff a 0x03 byte
            i++;
            /* Start encoding from the third byte of the input triplet - this copes with cases
             * where the third byte was a 0x00 and the subsequent bytes are 0x00 0x0n which again
             * needs further encoding.
             */
        }
        if (sei->writePtr >= &sei->seiContents[sei->totalLength - sizeof(sei->nalPrefix) - sizeof(sei->nalUnitType)]) {
            #if READOUT_SEI_BUF
            LTLOG("sei.buf", "Wrote %d bytes (sz %d) 0x%08x -> 0x%08x", (sei->writePtr-startPtr), sizeBytes, (u32)startPtr, (u32)sei->writePtr);
            #endif
            LTLOG_YELLOWALERT("sei.ovr", "Buffer overflow with %lu bytes left", LT_Pu32(payloadLen-2-i));
            return;
        }
    }

    /* Write the final two bytes at the end of the payload, but only if they haven't already been stuffed */
    for(; i<payloadLen; i++) {
        *sei->writePtr++ = payload[i];
    }

    /* Write a delimiter byte at the end of the message fragment. This is primarily to ensure that
     * if the last byte of the payload is 0x00 it is not consumed by the following NALU
     * separator. It also avoids three-stuffing a pair of 0x00,0x00 at the end of the message.
     */
    *sei->writePtr++ = kSeiFragmentDelimiter;

    /* Write rbsp_trailing_bits field after each SEI message fragment. This will get overwritten
     * if additional messages are added, but ensures the final message is always followed by this
     * delimiter field.
     */
    *sei->writePtr = kSeiRbspTrailingBits;

    #if READOUT_SEI_BUF
    LTLOG("sei.buf", "Wrote %d bytes (sz %d) 0x%08x -> 0x%08x", (sei->writePtr-startPtr), sizeBytes, (u32)startPtr, (u32)sei->writePtr);
    #endif
}

static void
BuildNaluSei(LTMediaNaluSeiImpl *sei) {
    /* Calculate the total NALU size for the provided set of message fragments */
    u32 seiLength = 0;
    LTList_ForEach(pNode, &sei->MessageFragments) {
        NaluSEIFragment *msgFrag = LT_CONTAINER_OF(pNode, NaluSEIFragment, node);
        seiLength += GetSeiMessageLength(msgFrag->seiFragmentSize, msgFrag->seiFragment);
    }
    LTList_EndForEach;

    if (!seiLength) {
        /* No message fragments available so nothing to build */
        return;
    }

    /* Account for rbsp_trailing_bits */
    seiLength++;

    /* Malloc the SEI */
    sei->pEncodedSei = lt_malloc(seiLength + sizeof(NaluSEI));
    if (!sei->pEncodedSei) {
        /* Don't bother logging out-of-memory error. This could occur 20 times per second
         * (video framerate) and if the system is OOM there will be other problems anyway.
         * Losing SEI data from the video stream is not critical.
         */
        return;
    }

    sei->pEncodedSei->writePtr = &sei->pEncodedSei->seiContents[0];
    sei->pEncodedSei->totalLength = seiLength + sizeof(sei->pEncodedSei->nalPrefix) + sizeof(sei->pEncodedSei->nalUnitType);
    sei->pEncodedSei->nalPrefix[0] = 0x00;
    sei->pEncodedSei->nalPrefix[1] = 0x00;
    sei->pEncodedSei->nalPrefix[2] = 0x00;
    sei->pEncodedSei->nalPrefix[3] = 0x01;
    sei->pEncodedSei->nalUnitType = kLTMediaNalTypeSei;
    //LTLOG("sei.prep", "len %lu, ptr=0x%08lx", LT_Pu32(sei->pEncodedSei->totalLength), LT_Pu32((u32)sei->pEncodedSei->writePtr));

    /* Add each message fragment to the SEI, removing them from the list as we go */
    LTList_ForEach(pNode, &sei->MessageFragments) {
        NaluSEIFragment *pMsgFrag = LT_CONTAINER_OF(pNode, NaluSEIFragment, node);
        AddSeiMessage(sei->pEncodedSei, pMsgFrag->seiUUID, pMsgFrag->seiFragmentSize, pMsgFrag->seiFragment);
        LTList_Remove(pNode);
        lt_free(pMsgFrag);
    }
    LTList_EndForEach;
}


/*******************************************************************************
 * Object API
 ******************************************************************************/

static bool
LTMediaNaluSeiImpl_ConstructObject(LTMediaNaluSeiImpl *sei) {
    LTList_Init(&sei->MessageFragments);
    sei->pEncodedSei = NULL;
    return true;
}

static void
LTMediaNaluSeiImpl_DestructObject(LTMediaNaluSeiImpl *sei) {
    /* Remove any elements on the message fragment list */
    while (!LTList_IsEmpty(&sei->MessageFragments)) {
        LTList_Node *pHead = sei->MessageFragments.pNext;
        LTList_Remove(pHead);

        NaluSEIFragment *pMsgFrag = LT_CONTAINER_OF(pHead, NaluSEIFragment, node);
        lt_free(pMsgFrag);
    }

    /* Free up the encoded SEI, if it exists */
    lt_free(sei->pEncodedSei);
    sei->pEncodedSei = NULL;
}

static bool
LTMediaNaluSeiImpl_PrepareMessageFrag(LTMediaNaluSeiImpl *sei, u8 *pUUID, u32 nPayloadLen, void *pPayload) {
    if (sei->pEncodedSei) {
        LTLOG_YELLOWALERT("prep.msg.frag.err", "An encoded SEI has already been generated and must be discarded before preparing more message fragments");
        return false;
    }

    NaluSEIFragment *pMsgFrag = lt_malloc(sizeof(NaluSEIFragment));
    if (!pMsgFrag) {
        LTLOG_YELLOWALERT("frag.alloc.err", "Failed to alloc list elem");
        return false;
    }

    lt_memcpy(pMsgFrag->seiUUID, pUUID, sizeof(pMsgFrag->seiUUID));
    pMsgFrag->seiFragmentSize = nPayloadLen;
    pMsgFrag->seiFragment = pPayload;
    LTList_AddTail(&sei->MessageFragments, &pMsgFrag->node);
    return true;
}

static u32
LTMediaNaluSeiImpl_GetEncodedNalu(LTMediaNaluSeiImpl *sei, u8 **pPayload) {
    if (!sei->pEncodedSei) {
        BuildNaluSei(sei);
    }
    if (!sei->pEncodedSei) {
        return 0;
    }

    if (pPayload) {
        *pPayload = &sei->pEncodedSei->nalPrefix[0];
    }
    return sei->pEncodedSei->totalLength;
}

static void
LTMediaNaluSeiImpl_Discard(LTMediaNaluSeiImpl *sei) {
    /* Just call the object destructor to cleanup the internal state. The object remains valid. */
    LTMediaNaluSeiImpl_DestructObject(sei);
}

/*_______________________________________
  LTMediaNaluSei LTObjectApi definition */
define_LTObjectImplPublic(LTMediaNaluSei, LTMediaNaluSeiImpl,
    PrepareMessageFrag,
    GetEncodedNalu,
    Discard,
);
