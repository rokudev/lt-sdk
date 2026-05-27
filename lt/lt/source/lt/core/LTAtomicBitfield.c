/******************************************************************************
 * lt/source/core/LTAtomicBitfield.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include "LTCoreImpl.h"
#include "LTAtomicBitfield.h"

#define LTABF_BITFIELD_BITS_PER_WORD        32  /* bitfield is array of LTAtomic, which is always 32 bits */
#define LTABF_BITFIELD_BYTES_PER_WORD       (LTABF_BITFIELD_BITS_PER_WORD >> 3) /* bits>>3 == bits/8 */            
#define LTABF_BITPOS_TO_WORDINDEX(bitPos)   ((bitPos) >> 5)     /* bitPos >> 5 == bitPos / 32 */
#define LTABF_BITPOS_TO_WORDBIT(bitPos)     ((bitPos) & 0x1f)
#define LTABF_BITPOS_TO_WORDBITMASK(bitPos) ((u32)1 << LTABF_BITPOS_TO_WORDBIT(bitPos))
#define LTABF_BITPOS_PRIME_ALLOC_ADVANCE    23                  /* primes between 13 and 100 are: 13 17 19 23 29 31 37 41 43 47 53 59 61 67 71 73 79 83 89 97 */
#define LTABF_WORDBITS_FULL                 ((u32)-1)

DEFINE_LTLOG_SECTION("ltcore.abf");

LT_INLINE u32 LTAtomicBitfield_CalculateNumWordsInBitfield(u32 numBitfieldBits) {
    /* round number of bitfield bits up to the next full word boundary */
    numBitfieldBits +=  (LTABF_BITFIELD_BITS_PER_WORD - 1);
    numBitfieldBits &= ~(LTABF_BITFIELD_BITS_PER_WORD - 1);
    /* convert rounded up number of bitfield bits to number of words and return */
    return (numBitfieldBits / LTABF_BITFIELD_BITS_PER_WORD);
}

u32 LTAtomicBitfield_CalculateNumBytesInBitfield(u32 numBitfieldBits) {
    /* calculate number of words, convert to bytes and return  */
    return (LTAtomicBitfield_CalculateNumWordsInBitfield(numBitfieldBits) * LTABF_BITFIELD_BYTES_PER_WORD);
 }

void LTAtomicBitfield_Init(LTAtomicBitfield *bitField, LTAtomic *bitfieldWordStorage, u32 numBitfieldBits, bool bReserveBitZero) {
    
    /* set first bitfield word according to bit zero reservation request
       zero the rest of the bitfield words, and mark an extra leftover bits on the last one in-use */
    u32 i, numBitfieldWords = LTAtomicBitfield_CalculateNumWordsInBitfield(numBitfieldBits);
    LTAtomic_Store(&bitfieldWordStorage[0], bReserveBitZero ? 0x1 : 0x0);
    for (i = 1; i < numBitfieldWords; i++) LTAtomic_Store(&bitfieldWordStorage[i], 0x0);
    if (0 != (i = (numBitfieldBits & (LTABF_BITFIELD_BITS_PER_WORD - 1)))) { /* i is number of usable bits in last word */
        i = ((u32)1 << i) - 1; 
        LTAtomic_Store(&bitfieldWordStorage[numBitfieldWords - 1], ~i);
    }
    
                    bitField->bitfieldWords =     bitfieldWordStorage;
    LTAtomic_Store(&bitField->numBitfieldWords,   numBitfieldWords);
    LTAtomic_Store(&bitField->numBitfieldBits,    numBitfieldBits);
    LTAtomic_Store(&bitField->lastBitAllocated,   numBitfieldBits - 1); /* so our first allocation offsets from bit 0 */
    LTAtomic_Store(&bitField->numDebugCollisions, 0);
}

bool LTAtomicBitfield_IsUsed(const LTAtomicBitfield *bitField, u32 bitPos) {
    return (bitPos < LTAtomic_Load((LTAtomic *)&bitField->numBitfieldBits)) ? (0 != (LTAtomic_Load(&bitField->bitfieldWords[LTABF_BITPOS_TO_WORDINDEX(bitPos)]) & LTABF_BITPOS_TO_WORDBITMASK(bitPos))) : false;
}

/* swap a newly sized bitfield array into the manager */
bool LTAtomicBitfield_Swap(LTAtomicBitfield *bitField, LTAtomicBitfield *swapField)
{
    if (LTAtomic_Load(&swapField->numBitfieldWords) < LTAtomic_Load(&bitField->numBitfieldWords)) return false; /* MAY ONLY GROW */

    LTAtomicBitfield tempField; /* store existing config */
                     tempField.bitfieldWords =                    bitField->bitfieldWords;
    LTAtomic_Store( &tempField.numBitfieldWords,   LTAtomic_Load(&bitField->numBitfieldWords));
    LTAtomic_Store( &tempField.numBitfieldBits,    LTAtomic_Load(&bitField->numBitfieldBits));
    LTAtomic_Store( &tempField.lastBitAllocated,   LTAtomic_Load(&bitField->lastBitAllocated));
    LTAtomic_Store( &tempField.numDebugCollisions, LTAtomic_Load(&bitField->numDebugCollisions));

    /* if the last word in the existing bitfield has lefover extra bits marked used, we can clear them now since we're extending the number of words */
    u32 i = LTAtomic_Load(&bitField->numBitfieldBits) & (LTABF_BITFIELD_BITS_PER_WORD - 1);
    if (i) {
        /* create a mask of the usable bits in the last word and AND it with the last word to zero the formerly unusable bits,
           making them available */
        i = (((u32)1 << i) - 1);
        LTAtomic_FetchAnd(&swapField->bitfieldWords[LTAtomic_Load(&bitField->numBitfieldWords) - 1], i);
    }

    /* by individually copying only the important fields,
     * we preserve tracking fields like 'numDebugCollisions'
     */
    bitField->bitfieldWords = swapField->bitfieldWords;
    LTAtomic_Store(&bitField->numBitfieldWords, LTAtomic_Load(&swapField->numBitfieldWords));
    LTAtomic_Store(&bitField->numBitfieldBits, LTAtomic_Load(&swapField->numBitfieldBits));
    LTAtomic_Store(&bitField->lastBitAllocated, LTAtomic_Load(&swapField->lastBitAllocated));

    swapField->bitfieldWords = tempField.bitfieldWords;
    LTAtomic_Store(&swapField->numBitfieldWords, LTAtomic_Load(&tempField.numBitfieldWords));
    LTAtomic_Store(&swapField->numBitfieldBits, LTAtomic_Load(&tempField.numBitfieldBits));
    LTAtomic_Store(&swapField->lastBitAllocated, LTAtomic_Load(&tempField.lastBitAllocated));
    LTAtomic_Store(&swapField->numDebugCollisions, LTAtomic_Load(&tempField.numDebugCollisions));

    return true;
}

bool LTAtomicBitfield_AllocOne(LTAtomicBitfield *bitField, u32 *bitPosAllocated)
{
    u32 numBitfieldWords = LTAtomic_Load(&bitField->numBitfieldWords);
    u32 bitPos    = LTAtomic_Load(&bitField->lastBitAllocated) + LTABF_BITPOS_PRIME_ALLOC_ADVANCE;

    /* ensure bitPos falls within numBitfieldWords, and, if the new starting check bitPos falls in the same word as the last
       allocation we have to check that word twice for fullcoverage of it, so we increaese nIterations by 1 to catch k..31 and then 0..k-1
    */
    if (bitPos >= (numBitfieldWords * LTABF_BITFIELD_BITS_PER_WORD)) bitPos -= (numBitfieldWords * LTABF_BITFIELD_BITS_PER_WORD);
    u32 nIterations = (LTABF_BITPOS_TO_WORDINDEX(bitPos) == LTABF_BITPOS_TO_WORDINDEX(LTAtomic_Load(&bitField->lastBitAllocated))) ? numBitfieldWords + 1 : numBitfieldWords;
    
    while (nIterations--) {
        u32 wordIndex = LTABF_BITPOS_TO_WORDINDEX(bitPos);
        u32 bitfieldWord = LTAtomic_Load(&bitField->bitfieldWords[wordIndex]);

        if (LTABF_WORDBITS_FULL != bitfieldWord) {
            u32         bit = LTABF_BITPOS_TO_WORDBIT(bitPos);
            u32     bitmask = LTABF_BITPOS_TO_WORDBITMASK(bitPos);

            while (bit < LTABF_BITFIELD_BITS_PER_WORD) {
                if (0 == (bitfieldWord & bitmask)) { /* if the bit hasn't been set */
                    /* this bit appears available, attempt to reserve this bit atomically */
                    u32 oldval = LTAtomic_Load(&bitField->bitfieldWords[wordIndex]);
                    while (0 == (oldval & bitmask)) { /* while the bit hasn't been taken */
                        u32 newval = oldval | bitmask;
                        if (LTAtomic_CompareAndExchange(&bitField->bitfieldWords[wordIndex], oldval, newval)) {
                            LTAtomic_Store(&bitField->lastBitAllocated, bitPos);
                            *bitPosAllocated = bitPos;    /* position allocated */
                            return true;
                        }
                        LTAtomic_FetchAdd(&bitField->numDebugCollisions,1);
                        /* if we're here, TAS failed */
                        /* update oldval to see if it's been set */
                        oldval = LTAtomic_Load(&bitField->bitfieldWords[wordIndex]);
                    }
                }
                bit++; bitmask <<= 1; bitPos++;
            }
        }
        else {
            /* move to the next position */
            bitPos += LTABF_BITFIELD_BITS_PER_WORD;
            /* ensure we start on the bitmask boundary */
            bitPos &= ~(LTABF_BITFIELD_BITS_PER_WORD-1);
        }
        /* check for wrap */
        if (bitPos >= (numBitfieldWords * LTABF_BITFIELD_BITS_PER_WORD)) bitPos = 0;
    }

    return false;
}

void LTAtomicBitfield_FreeOne(LTAtomicBitfield *bitField, u32 bitPos) {
    if (bitPos >= LTAtomic_Load(&bitField->numBitfieldBits)) return;

    u32 wordIndex = LTABF_BITPOS_TO_WORDINDEX(bitPos);
    u32 bitmask = LTABF_BITPOS_TO_WORDBITMASK(bitPos);

    /* force the bit to be on */
    while (1) {
        u32 oldval = LTAtomic_Load(&bitField->bitfieldWords[wordIndex]);
        u32 newval = oldval & ~bitmask;    /* no longer in use */
        if (LTAtomic_CompareAndExchange(&bitField->bitfieldWords[wordIndex], oldval, newval)) break;
        LTAtomic_FetchAdd(&bitField->numDebugCollisions,1);
    }
}

void LTAtomicBitfield_DebugPrint(LTAtomicBitfield *  bitField) {
    LT_UNUSED(bitField); // because in release mode the pABVF is compiled out in LTLOG_DEBUG !
    LTLOG_DEBUG("dbg", "bitField:%p %2d %3d last %4d\n", bitField->bitfieldWords, (int)LTAtomic_Load(&bitField->numBitfieldWords), (int)LTAtomic_Load(&bitField->numBitfieldBits), (int)LTAtomic_Load(&bitField->lastBitAllocated));
}
