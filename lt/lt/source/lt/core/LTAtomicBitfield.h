/******************************************************************************
 * lt/source/core/LTAtomicBitfield.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTATOMICBITFIELD_H
#define ROKU_LT_SOURCE_LT_CORE_LTATOMICBITFIELD_H

/********************************************
 * LTAtomicBitfield Library Private Helper macros */
typedef struct LTAtomicBitfield {
    volatile LTAtomic *bitfieldWords;
    LTAtomic           numBitfieldWords;
    LTAtomic           numBitfieldBits;
    LTAtomic           lastBitAllocated;
    LTAtomic           numDebugCollisions;
} LTAtomicBitfield;

u32  LTAtomicBitfield_CalculateNumBytesInBitfield(u32 numBitfieldBits);
void LTAtomicBitfield_Init(LTAtomicBitfield *bitField, LTAtomic *bitfieldWordStorage, u32 numBitfieldBits, bool bReserveBitZero);
bool LTAtomicBitfield_IsUsed(const LTAtomicBitfield *bitField, u32 bitPos);
bool LTAtomicBitfield_Swap(LTAtomicBitfield *bitField, LTAtomicBitfield *swapField);
    /* Swaps a new bitfield INTO bitField, and returns the prior config into swapField.
     * For managing growing bitfields. swapField MUST have been initialized with
     * LTAtomicBitfield_Init().  Caller must Free( swapField->bitfieldWords ) at a safe time.
     * Note the new bitfield can only grow.
     */
bool LTAtomicBitfield_AllocOne(LTAtomicBitfield *bitField, u32 *bitPosAllocated);
void LTAtomicBitfield_FreeOne(LTAtomicBitfield *bitField, u32 bitPos);
void LTAtomicBitfield_DebugPrint(LTAtomicBitfield *bitField);

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_SOURCE_LT_CORE_LTATOMICBITFIELD_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  17-Aug-23   augustus    created by moving functions out of LTCoreImpl.h
 */
