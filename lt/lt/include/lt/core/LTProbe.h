/******************************************************************************
 * <lt/core/LTProbe.h>                                     LT Performance Probe
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTPROBE_H
#define ROKU_LT_INCLUDE_LT_CORE_LTPROBE_H

/*
 * LTProbe provides a non-invasive probe for collection of time-samples for performance
 * measurement during software development.  Its purpose is to report the time elapsed
 * between probe points without introducing measurement error, reporting error, or
 * non-deterministic behavior.
 *
 * LTLOG or LTProbe?
 * LTLOG can also be used for performance measurement by inserting LTLOG entries at two code points
 * and manually subtracting the reported kernel time of the second from the first.  However,
 * a small amount of time is consumed operating the LTLOG machinery which is non-deterministic
 * because LTLOG internally may block waiting for a mutex, and a small but varied time is spent
 * formatting log strings.
 *
 * In contrast, LTProbe has a minimum of machinery that operates in constant time and stores
 * the probe data for each probe in a pre-allocated block of memory.  The client supplies a
 * const char * tag with each probe capture to identify the probe, and these string pointers
 * are cached; the client must ensure all such strings are valid when LTProbe_ConsolePrintReport()
 * is called.
 * _____
 * USAGE
 *
 * LTProbe *probe = LTProbe_Create("WiFi Association", 5, true);
 *   / * create args are: subject of report, max # of probe samples, also probe stack and heap                  * /
 * LTProbe_CaptureProbe(probe, "Tag1");
 * LTProbe_CaptureProbe(probe, "Tag2"); / * CaptureProbe is thread safe-using atomics and can be called         * /
 * LTProbe_CaptureProbe(probe, "Tag3"); / *   by multiple threads.  All other functions must only be            * /
 * LTProbe_CaptureProbe(probe, "Tag4"); / *   called when all threads are done capturing probes.                * /
 * LTProbe_CaptureProbe(probe, "Tag5");
 * LTProbe_CaptureProbe(probe, "Tag6"); / * Tag6 replaces Tag5 (in lieu of overwriting memory or being dropped) * /
 * LTProbe_CaptureProbe(probe, "Tag7"); / * Tag7 replaces Tag6                                                  * /
 * LTProbe_ConsolePrintReport(pProbe);
 * LTProbe_Destroy(pProbe);             / * or call LTProbe_Reset(pProbe) to reuse                              * /
 * _____________
 * SAMPLE OUTPUT
 * LT> ltrun LTExampleParseJson
 * __________________
 * LTProbe Report --.  RokuLT-0.1.dev.unofficial_09:44:30am_PST.20230116-lt.jsontest-esp32.wroom32-release
 *     115.021836    `--> Parse 600K of JSON Text, 4 probes captured
 *                                     ____________________________________________________________________________
 *                                     kernel time    elapsed cumulative | stack   curr    max | heap curr      max
 *   1: Baseline                        114.667405   0.000000   0.000000 |   768    256    256 |       472      472
 *   2: Before Json Parse               114.667611   0.000205   0.000205 |   768    256    488 |       602      858
 *   3: After Json Parse                115.018909   0.351298   0.351504 |   768    256    488 |      2482     2482
 *   4: Finished                        115.023028   0.004119   0.355623 |   768    256    488 |       472     2482
 *                                   Total elapsed   0.355623
 *
 */
#include <lt/core/LTCore.h>
LT_EXTERN_C_BEGIN

/*__________________
  LTProbe TYPEDEFS /
        _______________________                                             ______          ________
        struct LTProbe_Entry */                                         /* |32 64 |         32   64 | */
typedef struct LTProbe_Entry {                                          /* | size | cumulative_size | */
    LTTime          probeTime;  /**< kernel time of probe       */      /* | 8  8 |          8    8 | */
    const char *    pTag;       /**< probe identifier           */      /* | 4  8 |         12   16 | */
    u32             stackSize;  /**< probe thread stack size    */      /* | 4  4 |         16   20 | */
    u32             stackCurr;  /**< stack size at probe        */      /* | 4  4 |         20   24 | */
    u32             stackMax;   /**< max stack at probe         */      /* | 4  4 |         24   28 | */
    u32             heapCurr;   /**< heap used at probe         */      /* | 4  4 |         28   32 | */
    u32             heapMax;    /**< heap hiwatermark at probe  */      /* | 4  4 |         32   36 | */
    u32             reserved;   /**< for alignment              */      /* | 4  4 |         36   40 | */
    LT_SIZE         reserved2;  /**< for alignment              */      /* | 4  8 |         40   48 | */
} LTProbe_Entry; /**< represents a probe entry */
#ifndef DOXY_SKIP
                                                 LT_STATIC_ASSERT_SIZE_32_64(LTProbe_Entry, 40,  48);
#endif

/*      _________________
        struct LTProbe */
typedef struct LTProbe {
    LTAtomic        nProbesCaptured;            /**< number of probes captured (may exceed nTotalProbes @see LTProbe_Create, LTProbe_CaptureProbe() */
    LTAtomic        nLastEntryIndex;            /**< 'index' of probe captured in last entry (last entry reused when filled) */
    LTAtomic        nLastEntryAtomic;           /**< atomic guard for access to last entry; */
    u32             nNumEntriesAllocated;       /**< number of probe entries allocated */
    const char *    pSubject;                   /**< subject of probe, e.g. "WiFi Association" */
    ILTThread *     iThread;                    /**< cached thread interface */
    char            buff[128];                  /**< output buffer for ConsolePrintReport (so on heap not stack) */
    LTProbe_Entry   probes[];                   /**< array of probes */
} LTProbe; /**< represents a probe activity */

#ifndef DOXY_SKIP
LT_STATIC_ASSERT_SIZE_32_64(LTProbe, 152, 160);

/*_______________________________
/ LTProbe FORWARD DECLARATIONS */
static void LTProbe_Reset(LTProbe * pProbe);
static void LTProbe_ResetSubject(LTProbe * pProbe, const char * pSubject);
static void LTProbe_CaptureProbe(LTProbe * pProbe, const char * pTag);
static void LTProbe_FormatTime(LTTime time, char * buff, u32 fieldWidth);
static void LTProbe_ConsolePrintReport(LTProbe * pProbe);
#endif

/*______________________________
/ LTProbe INTERFACE FUNCTIONS */

/* LTProbe_Create */
static LTProbe * LTProbe_Create(const char * pSubject, u32 nMaxProbes, bool probeThreadMemory) {
    /* These LT_UNUSEDs are here to prevent "defined but not used" compiler errors for any functions that aren't used */
    LT_UNUSED(LTProbe_Reset); LT_UNUSED(LTProbe_ResetSubject); LT_UNUSED(LTProbe_CaptureProbe); LT_UNUSED(LTProbe_FormatTime); LT_UNUSED(LTProbe_ConsolePrintReport);

    if (nMaxProbes < 2) nMaxProbes = 2;
    LTProbe * pProbe = (LTProbe *)lt_malloc(sizeof(LTProbe) + (sizeof(LTProbe_Entry) * nMaxProbes));
    if (pProbe) {
        LTAtomic_Store(&pProbe->nProbesCaptured, 0);
        LTAtomic_Store(&pProbe->nLastEntryIndex, 0);
        LTAtomic_Store(&pProbe->nLastEntryAtomic, 0);
        pProbe->nNumEntriesAllocated = nMaxProbes;
        pProbe->pSubject = pSubject ? pSubject : "";
        pProbe->iThread = probeThreadMemory ? lt_getlibraryinterface(ILTThread, LT_GetCore()) : 0;
    }
    return pProbe;
}
    /**< creates and initializes a new LTProbe object
     *
     * %LTProbe_Create creates and initializes a new LTProbe object.  After creating the LTProbe object,
     * Call %LTProbe_CaptureProbe() wherever timing information is to be captured.
     * Call dLTProbe_ConsolePrintReport() to print the probe timing report to the console.
     * Finally, call LTProbe_Destroy() to free the memory used by the LTProbe Object.
     *
     * @param  pSubject The subject of the probe that will be printed in the report
     * @param  nMaxProbes The max number of probes that will be captured
     * @param  probeThreadMemory whether or not thread stack and thread heap usage should be probed
     * @return A new initialized LTProbe pointer or NULL if LTProbe allocation failed
     * @see    Destroy
     * @note   All memory allocation for LTProbe is performed in %Create() so that
     *         %LTProbe_CaptureProbe does not need to allocate memory as this would introduce
     *         non-determanistic inaccuracies in the measurements.  If more probes are captured
     *         then nMaxProbes than each additional probe captured will replace the entry
     *         at index (nMaxProbes - 1) so that the final report will contain the final probe
     *         captured and the total elapsed time will be accurate.
     * @note   Setting probeThreadMemory to true will in theory add an extra quantum of time
     *         to each probe capture.  In practice, the amount is hardly measurable.  Nevertheless,
     *         If timing accuracy is most important, then %LTProbe_Create may be called with
     *         probeThreadMemory set to false.
     *
     */

/* LTProbe_Destroy */
static void LTProbe_Destroy(LTProbe * pProbe) {
    lt_free(pProbe);
}
    /**< destroys a previously created LTProbe object
     *
     * %LTProbe_Destroy frees the memory associated with an LTProbe.  Make sure %Destroy
     * is called to free the LTProbe memory when the LTProbe is no longer needed
     *
     * @param  pProbe The LTProbe to destroy
     */

/* LTProbe_Reset */
static void LTProbe_Reset(LTProbe * pProbe)  {
    LTAtomic_Store(&pProbe->nProbesCaptured, 0);
    LTAtomic_Store(&pProbe->nLastEntryIndex, 0);
    LTAtomic_Store(&pProbe->nLastEntryAtomic, 0);

}
    /**< resets an LTProbe object
     *
     * %LTProbe_Reset() may be used to reset the internal state of an LTProbe object so that
     * it can be reused.
     *
     * @param  pProbe The LTProbe to reset
     */

/* LTProbe_ResetSubject */
static void LTProbe_ResetSubject(LTProbe * pProbe, const char * pSubject)  {
    LTProbe_Reset(pProbe);
    pProbe->pSubject = pSubject;
}
    /**< resets an LTProbe object and sets a replacement subject
     *
     * %LTProbe_ResetSubject() may be used to reset the internal state of an LTProbe object so that
     * it can be reused, and to give it a new subject.
     *
     * @param  pProbe The LTProbe to reset
     * @param  pSubject The new subject for printing in the report that will replace
     *         the LTProbe's existing subject.
     * @note   The pSubject passed in must point to a constant c-string that remains valid
     *         for the lifetime of the LTProbe object.  (Technically it only need be valid
     *         whenever &LTProbe_ConsolePrintReport() is called).
    */

/* LTProbe_CaptureProbe */
static void LTProbe_CaptureProbe(LTProbe * pProbe, const char * pTag) {
    u32 nIndex = LTAtomic_FetchAdd(&pProbe->nProbesCaptured, 1);
    LTProbe_Entry * pEntry;
    if (nIndex < (pProbe->nNumEntriesAllocated-1)) {
        pEntry = &pProbe->probes[nIndex];
        pEntry->probeTime = LT_GetCore()->GetKernelTime();
        if (pProbe->iThread) {
            pProbe->iThread->GetStackUsage(pProbe->iThread->GetCurrentThread(), &pEntry->stackSize, &pEntry->stackCurr, &pEntry->stackMax);
            pProbe->iThread->GetHeapUsage(pProbe->iThread->GetCurrentThread(), &pEntry->heapCurr, &pEntry->heapMax);
        }
        pEntry->pTag = pTag;
    }
    else {
        pEntry = &pProbe->probes[pProbe->nNumEntriesAllocated - 1];
        while (nIndex > LTAtomic_Load(&pProbe->nLastEntryIndex)) {
            if (0 == LTAtomic_FetchAdd(&pProbe->nLastEntryAtomic, 1)) {
                if (nIndex > LTAtomic_Load(&pProbe->nLastEntryIndex)) {
                    pEntry->probeTime = LT_GetCore()->GetKernelTime();
                    if (pProbe->iThread) {
                        pProbe->iThread->GetStackUsage(pProbe->iThread->GetCurrentThread(), &pEntry->stackSize, &pEntry->stackCurr, &pEntry->stackMax);
                        pProbe->iThread->GetHeapUsage(pProbe->iThread->GetCurrentThread(), &pEntry->heapCurr, &pEntry->heapMax);
                    }
                    pEntry->pTag = pTag;
                    LTAtomic_Store(&pProbe->nLastEntryIndex, nIndex);
                }
                LTAtomic_Store(&pProbe->nLastEntryAtomic, 0);
            }
            else lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds(1));
        }
    }
}
    /**< captures a probe
     *
     * Call %LTProbe_CaptureProbe() wherever timing information is to be captured.
     * When finished capturing probes, call %LTProbe_ConsolePrintReport to see a summary.
     *
     * @param  pProbe The LTProbe to capture into
     * @param  pTag a tag string that will be printed in the report to identify the probe captured
     * @note   The pTag passed in must point to a constant c-string that remains valid
     *         for the lifetime of the LTProbe object.  (Technically it only need be valid
     *         whenever &LTProbe_ConsolePrintReport() is called).
     */
#define LT_PROBE_REPORT_TITLE1 "__________________\nLTProbe Report --.  "
#define LT_PROBE_REPORT_TITLE2 "    `--> "
#define LT_PROBE_REPORT_TITLE3 ", "
#define LT_PROBE_REPORT_TITLE4 " probes captured\n"

#define LT_PROBE_REPORT_HEADER0 "                                    "
#define LT_PROBE_REPORT_HEADER1 "____________________________________________________________________________\n"
#define LT_PROBE_REPORT_HEADER2 "kernel time    elapsed cumulative | stack   curr    max | heap curr      max\n"
#define LT_PROBE_REPORT_TOTAL   "Total elapsed"

#ifndef DOXY_SKIP
static void LTProbe_FormatTime(LTTime time, char * buff, u32 fieldWidth) {
    /* try to right justify seconds where we want the decimal point, if it doesn't fit, left justify it and move the decimal point, and clip microseconds */
    u32 digitsPlaced = lt_u32toString((u32)LTTime_GetSeconds(time), buff, (fieldWidth-7) | kLTStdlib_FormatFlags_RightJustify | kLTStdlib_FormatFlags_PadWithSpaces);
    digitsPlaced = digitsPlaced ? fieldWidth - 7 : lt_u32toString((u32)LTTime_GetSeconds(time), buff, (fieldWidth-2) | kLTStdlib_FormatFlags_LeftJustify);
    if (digitsPlaced) {
        buff[digitsPlaced] = '.';
        lt_u32toString((u32)LTTime_GetMicroseconds(LTTime_FractionalSeconds(time)), &buff[digitsPlaced+1], 6 | kLTStdlib_FormatFlags_RightJustify | kLTStdlib_FormatFlags_PadWithZeros);
        buff[fieldWidth] = 0;
    }
}
#endif

/*
          1         2         3         4         5         6         7         8
0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
                                    0(@36)    1         2         3         4         5         6         7
                                    01234567890123456789012345678901234567890123456789012345678901234567890123456(null)789
__________________
LTProbe Report --.  RokuLT-0.1.dev.unofficial_05:15:16am_PST.20230104-lt.coretest-linux.x86-release
   2573.771451    `--> ParseJson, 7 probes captured
                                    ____________________________________________________________________________
                                    kernel time    elapsed cumulative | stack   curr    max | heap curr      max
  1: Opening  LTUtilityJson           57.415648   0.000000   0.000000 |  2048    280    216 |       424      424
  2: Opened   LTUtilityJson           57.415825   0.000177   0.000177 |  2048    280    512 |       514      810
  3: Creating LTJsonParser            57.415843   0.000017   0.000195 |  2048    280    512 |       514      810
  4: Created  LTJsonParser            57.415875   0.000032   0.000227 |  2048    280    512 |       554      810
  5: Begin    Validate Twitter Json   57.415902   0.000026   0.000254 |  2048    280    512 |       554      810
  6: End      Validate Twitter Json   57.771161   0.355258   0.355513 |  2048    280    512 |      4234     4234
  7: Parser   Destroyed               57.771419   0.000258   0.355771 |  2048    280    512 |       514     4234
                                  Total elapsed   0.355771
*/

static void LTProbe_ConsolePrintReport(LTProbe * pProbe) { LT_UNUSED(pProbe);
    enum { kBuffSize = 120 };
    LTCore * pCore = LT_GetCore();
    pCore->ConsolePutString(LT_PROBE_REPORT_TITLE1); pCore->ConsolePutString(pCore->GetLibraryBuildVersion()); pCore->ConsolePutChars("\n", 1);
    LTProbe_FormatTime(pCore->GetKernelTime(), pProbe->buff, 14);
    char * pCurr = pProbe->buff + 14; const char * pSrc = LT_PROBE_REPORT_TITLE2; while (*pSrc) *pCurr++ = *pSrc++;
    pSrc = (char *)pProbe->pSubject; while (*pSrc && (pCurr < (pProbe->buff + kBuffSize) - (sizeof(LT_PROBE_REPORT_TITLE3) + sizeof(LT_PROBE_REPORT_TITLE4) + 9))) *pCurr++ = *pSrc++;
    pSrc = LT_PROBE_REPORT_TITLE3; while (*pSrc) *pCurr++ = *pSrc++;
    u32 probesCaptured = LTAtomic_Load(&pProbe->nProbesCaptured);
    pCurr += lt_u32toString(probesCaptured, pCurr, 10 | kLTStdlib_FormatFlags_LeftJustify);
    pSrc = LT_PROBE_REPORT_TITLE4; while (*pSrc) *pCurr++ = *pSrc++;
    *pCurr = 0; pCore->ConsolePutString(pProbe->buff);
    if (probesCaptured) {
        pCore->ConsolePutString(LT_PROBE_REPORT_HEADER0); pCore->ConsolePutString(LT_PROBE_REPORT_HEADER1);
        pCore->ConsolePutString(LT_PROBE_REPORT_HEADER0); pCore->ConsolePutString(LT_PROBE_REPORT_HEADER2);
        LTTime prevTime = pProbe->probes[0].probeTime;
        u32 i, nNumProbes = probesCaptured < pProbe->nNumEntriesAllocated ? probesCaptured : pProbe->nNumEntriesAllocated;
        for (i = 0; i < nNumProbes; i++) {
            LTProbe_Entry * pEntry = &pProbe->probes[i];
            pCurr = pProbe->buff;
            lt_u32toString(
                (i == (pProbe->nNumEntriesAllocated-1)) ? LT_Pu32(LTAtomic_Load(&pProbe->nLastEntryIndex)+1) : LT_Pu32(i+1),
                pCurr, 3 | kLTStdlib_FormatFlags_RightJustify | kLTStdlib_FormatFlags_PadWithSpaces);
            pCurr += 3; *pCurr++ = ':'; *pCurr++ = ' ';
            lt_strncpyTerm(pCurr, (pEntry->pTag && *pEntry->pTag) ? pEntry->pTag : "untagged", 31); pCurr += lt_strlen(pCurr);
            while (pCurr < (pProbe->buff + 36)) *pCurr++ = ' ';
            LTProbe_FormatTime(pEntry->probeTime, pCurr, 11); pCurr += 11;  *pCurr++ = ' ';
            LTProbe_FormatTime(LTTime_Subtract(pEntry->probeTime, prevTime), pCurr, 10); pCurr += 10; *pCurr++ = ' ';
            LTProbe_FormatTime(LTTime_Subtract(pEntry->probeTime, pProbe->probes[0].probeTime), pCurr, 10); pCurr += 10;
            prevTime = pEntry->probeTime;
            if (pProbe->iThread) {
                *pCurr++ = ' '; *pCurr++ = '|';  *pCurr++ = ' ';
                lt_u32toString(pEntry->stackSize, pCurr, 5 | kLTStdlib_FormatFlags_RightJustify | kLTStdlib_FormatFlags_PadWithSpaces); pCurr += 5;  *pCurr++ = ' ';
                lt_u32toString(pEntry->stackCurr, pCurr, 6 | kLTStdlib_FormatFlags_RightJustify | kLTStdlib_FormatFlags_PadWithSpaces); pCurr += 6;  *pCurr++ = ' ';
                lt_u32toString(pEntry->stackMax,  pCurr, 6 | kLTStdlib_FormatFlags_RightJustify | kLTStdlib_FormatFlags_PadWithSpaces); pCurr += 6;  *pCurr++ = ' ';
                *pCurr++ = '|';  *pCurr++ = ' ';
                lt_u32toString(pEntry->heapCurr,  pCurr, 9 | kLTStdlib_FormatFlags_RightJustify | kLTStdlib_FormatFlags_PadWithSpaces); pCurr += 9;  *pCurr++ = ' ';
                lt_u32toString(pEntry->heapMax,   pCurr, 8 | kLTStdlib_FormatFlags_RightJustify | kLTStdlib_FormatFlags_PadWithSpaces); pCurr += 8;
            }
            *pCurr++ = '\n'; *pCurr++ = 0;
            pCore->ConsolePutString(pProbe->buff);
        }
        pCurr = pProbe->buff;
        i = 34; while (i--) *pCurr++ = ' ';
        lt_strncpyTerm(pCurr, LT_PROBE_REPORT_TOTAL, sizeof(LT_PROBE_REPORT_TOTAL)); pCurr += (sizeof(LT_PROBE_REPORT_TOTAL) - 1); *pCurr++ = ' ';
        LTProbe_FormatTime(LTTime_Subtract(prevTime, pProbe->probes[0].probeTime), pCurr, 10); pCurr += 10;
        *pCurr++ = '\n'; *pCurr++ = 0;
        pCore->ConsolePutString(pProbe->buff);
    }
}
    /**< prints the probe timing report to the console
     *
     * Call %LTProbe_ConsolePrintReport() to print the probe report to the console
     *
     * @param  pProbe The LTProbe to print the report for
     */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTPROBE_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  27-Feb-22   augustus    created
 *  03-Jan-23   augustus    provide switchable linkage between static and LT_INLINE
 *  02-Aug-23   augustus    use new LTAtomics
 */
