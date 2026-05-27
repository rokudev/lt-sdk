/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreQsortTests.c>
 *    __  __      _ __ ______          __  __  ____________
 *   / / / /___  (_) //_  __/__  _____/ /_/ / /_  __/ ____/___  ________
 *  / / / / __ \/ / __// / / _ \/ ___/ __/ /   / / / /   / __ \/ ___/ _ \
 * / /_/ / / / / / /_ / / /  __(__  ) /_/ /___/ / / /___/ /_/ / /  /  __/
 * \____/_/ /_/_/\__//_/  \___/____/\__/_____/_/  \____/\____/_/   \___/
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <tilt/JiltEngine.h>
#include "UnitTestLTCoreHelpers.h"

struct sortingElement { /* the element of the array for qsort() to sort */
    char letter;
    int position;  /* original position in the array */
};

u64 clientData;     /* test client-specific data pointer and its test pattern */
static u64 const clientDataPattern = 0xE456FACEDEADBEEF;
/* TODO the compare functions should ensure that the pattern appears at the
 *      pointer they are passed.  They shouldn't necessarily be using this to
 *      pass back an error condition. */

/* Sort in ascending order by letter */

int sortCompareLetter(void const * p, void const * q, void * c) {
    if ((u64 *) c != &clientData)
        clientData = 0;
    return   ((struct sortingElement const *) p)->letter
           - ((struct sortingElement const *) q)->letter;
}

/* Sort in ascending order by position */

int sortComparePosition(void const * p, void const * q, void * c) {
    if ((u64 *) c != &clientData)
        clientData = 0;
    return   ((struct sortingElement const *) p)->position
           - ((struct sortingElement const *) q)->position;
}

/* Search compare function */
int searchCompareLetter(const void * pSearchTerm, const void * pValue, void * pClientData) {
    if ((u64 *) pClientData != &clientData) clientData = 0;
    return   *((char *)pSearchTerm) - ((struct sortingElement const *) pValue)->letter;
}

int searchComparePosition(const void * pSearchTerm, const void * pValue, void * pClientData) {
    if ((u64 *) pClientData != &clientData) clientData = 0;
    return   *((int *)pSearchTerm) - ((struct sortingElement const *) pValue)->position;
}

/* Make a char string out of the array letters, mostly for instrumentation:
 * Pass in pointers to the beginning of the sorting array and the beginning
 * of the char buffer, which has to be one char bigger than the size of
 * the sorting array (to accommodate a terminating null char), and the
 * expected length of the string: */

void extractArrayLetters(struct sortingElement const * p, char *b, int length) {
    for (; length; --length, ++p, ++b)
        *b = p->letter;
    *b = '\0';
}

void Testqsort(Tilt * tilt) {
    static char const * const pattern = "The quick brown fox jumped over the "
                                        "lazy dog's back.";
    enum { kLen = 52 };

    struct sortingElement *sortingArray = lt_malloc(kLen * sizeof(struct sortingElement));
    TILT_EXPECT_TRUE(tilt, NULL != sortingArray, "failed to alloc sortingArray");
    if (NULL == sortingArray) return;

    struct sortingElement * p = sortingArray;
    char letters[kLen + 1];
    char const * c = pattern;
    for (int i = 0; i < kLen; ++i, ++p, ++c) {  /* fill with the test pattern */
        p->letter = *c;
        p->position = i;            /* mark the original order, for resorting */
    }
    /* The compare functions will change clientData if they detect a problem. */
    clientData = clientDataPattern;
    extractArrayLetters(sortingArray, letters, kLen);
    TILT_DEBUG(tilt, "qsort.alpha.B: '%s'", letters);
    /* 1. Sort the array alphabetically by letter. */
    lt_qsort((void *) sortingArray, kLen, sizeof (struct sortingElement),
                        sortCompareLetter, &clientData);
    extractArrayLetters(sortingArray, letters, kLen);
    TILT_DEBUG(tilt, "qsort.alpha.A: '%s'", letters);
    TILT_EXPECT_TRUE(tilt, clientData == clientDataPattern, "qsort.alpha.client.data");
    --p;                         /* p points to the last element in the array */
    struct sortingElement * q = p--; /* p points to second-to-last, q to last */
    for (s32 i = kLen - 1; i; --i, --p, --q) {
        /* Letters should be sorted in ascending order. */
        TILT_EXPECT_TRUE(tilt, q->letter >= p->letter, "qsort.alpha: position=%ld, q='%c', p='%c'",
                         i, q->letter, p->letter);
    }
    /* Return to original order: */
    extractArrayLetters(sortingArray, letters, kLen);
    TILT_DEBUG(tilt, "qsort.position.B: '%s'", letters);
    lt_qsort((void *) sortingArray, kLen, sizeof (struct sortingElement),
                        sortComparePosition, &clientData);
    extractArrayLetters(sortingArray, letters, kLen);
    TILT_DEBUG(tilt, "qsort.position.A: '%s'", letters);
    TILT_EXPECT_TRUE(tilt, clientData == clientDataPattern, "qsort.position.client.data");
    /* The buffer should hold the same as the original pattern. */
    if (!strequ(letters, pattern)) {
        TILT_REPORT_FAILURE(tilt, "qsort.pos: string '%s', pattern '%s'",
                            letters, pattern);
    }
    lt_free(sortingArray);
}

void Testbsearch(Tilt * tilt) {
    static char const * const pattern = "The quick brown fox jumped over the lazy dog's back.";
    enum { kLen = 52 };

    struct sortingElement *arrayByPosition = lt_malloc(kLen * sizeof(struct sortingElement));
    struct sortingElement *arrayByLetter = lt_malloc(kLen * sizeof(struct sortingElement));
    TILT_EXPECT_TRUE(tilt, NULL != arrayByPosition, "failed to alloc arrayByPosition");
    TILT_EXPECT_TRUE(tilt, NULL != arrayByLetter, "failed to alloc arrayByLetter");
    if (! (arrayByPosition && arrayByLetter)) {
        if (arrayByPosition) lt_free(arrayByPosition);
        if (arrayByLetter) lt_free(arrayByLetter);
    }

    struct sortingElement * p = arrayByPosition;
    struct sortingElement * q = arrayByLetter;
    char const * c = pattern;
    int i;
    for (i = 0; i < kLen; ++i, ++p, ++q, ++c) {  /* fill with the test pattern */
        p->letter = q->letter = *c;
        p->position = q->position = i;
    }

    /* Sort arrayByLetter */
    clientData = clientDataPattern;
    lt_qsort((void *) arrayByLetter, kLen, sizeof (struct sortingElement), sortCompareLetter, &clientData);

    /* find each element in the arrayByPosition */
    for (i = 0; i < kLen; i++) {
        p = lt_bsearch(&i, arrayByPosition, kLen, sizeof(struct sortingElement), searchComparePosition, &clientData);
        TILT_EXPECT_TRUE(tilt, p != NULL, "failed to find element in arrayByPosition at position %d", i);
        TILT_EXPECT_TRUE(tilt, p->position == i, "found element at position %d with reported position %d", i, p->position);
        TILT_EXPECT_TRUE(tilt, p->letter == pattern[i], "element at position %d with letter '%c' doesn't letter match with pattern\n", i, p->letter);
    }
    /* verify finding a bogus element returns null */
    i = 2048;
    p = lt_bsearch(&i, arrayByPosition, kLen, sizeof(struct sortingElement), searchComparePosition, &clientData);
    TILT_EXPECT_TRUE(tilt, p == NULL, "bogus element %d found!!", i);

    /* find each element in the arrayByLetter */
    for (i = 0; i < kLen; i++) {
        p = lt_bsearch(&pattern[i], arrayByLetter, kLen, sizeof(struct sortingElement), searchCompareLetter, &clientData);
        TILT_EXPECT_TRUE(tilt, p != NULL, "failed to find element in arrayByLetter with letter %c, position %d", pattern[i], i);
        TILT_EXPECT_TRUE(tilt, p->letter == pattern[i], "element at position %d with letter '%c' doesn't letter match with pattern\n", i, p->letter);
        TILT_EXPECT_TRUE(tilt, pattern[p->position] == p->letter, "found element with letter '%c' mismatching position %i", p->letter, p->position);
    }
    /* verify finding a bogus element returns null */
    char ch = '@';
    p = lt_bsearch(&ch, arrayByLetter, kLen, sizeof(struct sortingElement), searchCompareLetter, &clientData);
    TILT_EXPECT_TRUE(tilt, p == NULL, "bogus element %c found!!", ch);

    lt_free(arrayByPosition);
    lt_free(arrayByLetter);
}

void TestbsearchIndex(Tilt * tilt) {
    static char const * const pattern = "The quick brown fox jumped over the lazy dog's back.";
    enum { kLen = 52 };

    struct sortingElement *arrayByPosition = lt_malloc(kLen * sizeof(struct sortingElement));
    struct sortingElement *arrayByLetter = lt_malloc(kLen * sizeof(struct sortingElement));
    TILT_EXPECT_TRUE(tilt, NULL != arrayByPosition, "failed to alloc arrayByPosition");
    TILT_EXPECT_TRUE(tilt, NULL != arrayByLetter, "failed to alloc arrayByLetter");
    if (! (arrayByPosition && arrayByLetter)) {
        if (arrayByPosition) lt_free(arrayByPosition);
        if (arrayByLetter) lt_free(arrayByLetter);
    }

    struct sortingElement * p = arrayByPosition;
    struct sortingElement * q = arrayByLetter;
    char const * c = pattern;
    int i;
    for (i = 0; i < kLen; ++i, ++p, ++q, ++c) {  /* fill with the test pattern */
        p->letter = q->letter = *c;
        p->position = q->position = i;
    }

    /* Sort arrayByLetter */
    clientData = clientDataPattern;
    lt_qsort((void *) arrayByLetter, kLen, sizeof (struct sortingElement), sortCompareLetter, &clientData);

    /* find each element in the arrayByPosition */
    LT_SIZE index = 0;
    for (i = 0; i < kLen; i++) {
        index = lt_bsearchIndex(&i, arrayByPosition, kLen, sizeof(struct sortingElement), searchComparePosition, &clientData);
        TILT_EXPECT_TRUE(tilt, index != kLen, "failed to find element in arrayByPosition at position %d", i);
        p = arrayByPosition + index;
        TILT_EXPECT_TRUE(tilt, p->position == i, "found element at position %d with reported position %d", i, p->position);
        TILT_EXPECT_TRUE(tilt, p->letter == pattern[i], "element at position %d with letter '%c' doesn't letter match with pattern\n", i, p->letter);
    }
    /* verify finding a bogus element returns kLen */
    i = 2048;
    index = lt_bsearchIndex(&i, arrayByPosition, kLen, sizeof(struct sortingElement), searchComparePosition, &clientData);
    TILT_EXPECT_TRUE(tilt, index == kLen, "bogus element %d found!!", i);

    /* find each element in the arrayByLetter */
    for (i = 0; i < kLen; i++) {
        index = lt_bsearchIndex(&pattern[i], arrayByLetter, kLen, sizeof(struct sortingElement), searchCompareLetter, &clientData);
        TILT_EXPECT_TRUE(tilt, index != kLen, "failed to find element in arrayByLetter with letter %c, position %d", pattern[i], i);
        p = arrayByLetter + index;
        TILT_EXPECT_TRUE(tilt, p->letter == pattern[i], "element at position %d with letter '%c' doesn't letter match with pattern\n", i, p->letter);
        TILT_EXPECT_TRUE(tilt, pattern[p->position] == p->letter, "found element with letter '%c' mismatching position %i", p->letter, p->position);
    }
    /* verify finding a bogus element returns null */
    char ch = '@';
    index = lt_bsearchIndex(&ch, arrayByLetter, kLen, sizeof(struct sortingElement), searchCompareLetter, &clientData);
    TILT_EXPECT_TRUE(tilt, index == kLen, "bogus element %c found!!", ch);

    lt_free(arrayByPosition);
    lt_free(arrayByLetter);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Aug-19   constantine created
 *  23-Dec-19   constantine changed name from LTTestNoFault to LTUnitTestCore
 *  17-Jul-20   constantine converted to C
 *  06-Dec-20   constantine reworked printf formatting
 *  19-Oct-21   constantine moved to UnitTestLTCore
 *  02-Aug-22   constantine reworked for TILT
 *  31-Mar-24   augustus    added tests for bsearch and bsearchIndex
*/
