/******************************************************************************
 * <lt/core/LTCore.h>                                  LTCore library interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTCORE_H
#define ROKU_LT_INCLUDE_LT_CORE_LTCORE_H

#include <lt/LTObject.h>
#include <lt/core/LTList.h>
#include <lt/core/LTEvent.h>
#include <lt/core/LTMutex.h>
#include <lt/core/LTThread.h>
#include <lt/core/LTStdlib.h>

LT_EXTERN_C_BEGIN

/**
 * @defgroup ltcore_cb Callbacks
 * @ingroup ltcore
 */

/**
 * @defgroup ltcore_enum Enumerations
 * @ingroup ltcore
 */

/**
 * @defgroup ltcore_struct Structs
 * @ingroup ltcore
 */

/********
* LTCore typedef forward declarations */
typedef struct LTCore_LibrarySnapshot LTCore_LibrarySnapshot;
typedef struct LTCore_HeapAllocatedBlockInfo LTCore_HeapAllocatedBlockInfo;
typedef struct LTKList_Node LTKList_Node;
declare_LTENUM_SIZED(LTCore_LibraryHookSite, u32);
declare_LTENUM_SIZED(LTCore_SleepAction, u32);

typedef void  (LTCore_ConsoleCharactersReceivedProc)(char * pChars, u32 nChars, void * pClientData);
/**< Callback signature used by SetConsoleCharactersReceivedProc.
 * This function receives (non-break) characters typed into the console.
 * @param[in] pChars Array of characters typed.
 * @param[in] nChars Number of characters in @p pChars.
 * @param[in] pClientData Client data passed to the SetConsoleCharactersReceivedProc call.
 * @see LTCore::SetConsoleCharactersReceivedProc.
 * @ingroup ltcore_cb
 */

typedef void  (LTCore_ConsoleBreakReceivedProc)(void * pClientData);
/**< Callback signature used by SetConsoleCharactersReceivedProc.
 *   This function receives break characters typed into the console.
 *   @param[in] pClientData Client data passed to the SetConsoleCharactersReceivedProc call.
 *   @see LTCore::SetConsoleCharactersReceivedProc.
 *   @ingroup ltcore_cb
 */

typedef bool  (LTCore_InstalledLibrariesEnumProc)(const char * pLibraryName, void * pClientData);

typedef void  (LTCore_InterruptHandler)(void);

typedef void  (LTCore_LibrarySnapshotCallbackProc)(LTCore_LibrarySnapshot * pSnapshot, void * pClientData);
/**< Callback signature used by SnapshotOpenLibraries.
 *   @param[in] pSnapshot Information about a single open library.
 *   @param[in] pClientData Client data passed to the SnapshotOpenLibraries call.
 *   @ingroup ltcore_cb
 */

typedef LTLibrary * (LTCore_LibraryHookFunction)(LTCore_LibraryHookSite site, LTLibrary *pLibrary, const char *pLibraryName);
/**< Callback to support library opening and closing.
 *
 *   The hook function can be used to manipulate an LTLibrary structure before it is opened, just after the
 *   library has been opened, or just before it is closed. It is called in (3) places, represented by LTCore_LibraryHookStage:
 *      1. kLTCore_LibraryHookSite_SubstituteOpen
 *          a. Location: LTCoreImpl_OpenLibrary() - BEFORE LTLibraryManager_OpenLibrary()
 *          b. Allows the LTLibrary to be completely replaced, including the library's LibInit function. If the hook
 *             returns a non-null return value, the return value will be treated as the replacement LTLibrary, and the
 *             LTLibraryManager_OpenLibrary() call will be omitted.
 *      2. kLTCore_LibraryHookSite_AfterOpen
 *          a. Location: LTCoreImpl_OpenLibrary() - AFTER LTLibraryManager_OpenLibrary()
 *          b. Allows the LTLibrary to be modified after it has been opened. The hook must return a non-null pointer,
 *             even if doesn't make any changes.
 *      3. kLTCore_LibraryHookSite_BeforeClose
 *          a. Location: LTCoreImpl_CloseLibrary() - BEFORE LTLibraryManager_CloseLibrary()
 *          b. Allows cleanup of hook operations, if desired
 *
 *   @param[in] site identifies the call location of the hook
 *   @see LTCore_LibraryHookStage
 *   @param[in] pLibrary Pointer to LTLibrary being opened or closed. NULL when hook is
 *                       called before library is opened.
 *   @param[in] pLibraryName For library open only, the string name of the library being opened.
 *   @return LTLibrary pointer, possibly different to the supplied pointer
 *   @ingroup ltcore_cb
 */

typedef void (LTCore_LogHookFunction)(const char *pSection, const char *pTag, u32 nFlags, const char *pFormat, lt_va_list args);
/**< Callback to dispatch logs to */

typedef void (*LTCore_TraceHookFunction)(LTTraceStream *stream, LTTracePayloadType type, lt_va_list args);
/**< Callback to dispatch traces to */

typedef void (LTCore_CrashdumpWriteCallback)(const u8 * pBuffer, LT_SIZE nBufferSize, LT_SIZE * pSpaceAvail) LT_ISR_SAFE;
/**< LTCore callback for writing crash dumps.
 * @param[in]  pBuffer Pointer to buffer for write. Set to NULL on first AND last invocation.
 * @param[in]  nBufferSize Size of buffer to write. Set to zero on first AND last invocation.
 * @param[out] pSpaceAvail Pointer to amount of space (in bytes) available for writes.
 * @ingroup ltcore_cb
 */

typedef bool (LTCore_HeapAllocatedBlockInfoEnumCB)(const LTCore_HeapAllocatedBlockInfo * pBlockInfoStructs, u32 nNumBlockInfoStructs, void * pClientData);
/**< LTCore callback for enumerating allocated heap blocks
 *
 * @param[in] pBlockInfoStructs pointer to an array of LTCore_HeapAllocatedBlockInfo structs that describe allocated heap blocks
 * @param[in] nNumBlockInfoStructs the number of LTCore_HeapAllocatedBlockInfo pointed to by pBlockInfoStructs
 * @param[in] pClientData the client data supplied to EnumerateHeapAllocatedBlockInfo
 * @return true to continue enumeration, false to abort
 *
 * @see EnumerateHeapAllocatedBlockInfo
 *
 * @ingroup ltcore_cb
 */

typedef LTTime (LTCore_SleepActionEventProc)(LTCore_SleepAction action, LTTime wakeupTimeOrSleepDuration, void *pClientData);
/**< LTCore event callback proc for notification of sleep actions
 *
 * This event callback is notified whenever a sleep action is taken.
 * The sleep actions are:
 *   kLTCore_SleepAction_GoingToSleep - called when the system is about to go to sleep
 *   kLTCore_SleepAction_SleepAborted - called when someone took a sleep disallowance grant in their SleepActionEventProc
 *   kLTCore_SleepAction_AwakenedFromSleep - called when the system has awakened from sleep
 *
 * @param action the sleep action that is taking place
 * @param wakeupTimeOrSleepDuration time parameter with the following meanings:<pre>
 *        ______                                   _______
 *        ACTION                                   MEANING
 *        kLTCore_SleepAction_GoingToSleep         the kernel time when the system will awaken from sleep due to the soonest
 *                                                 firing software wakeup timer or LTTime_Zero() if no software wakeup timers are set
 *        kLTCore_SleepAction_SleepAborted         n/a - always LTTime_Zero()
 *        kLTCore_SleepAction_AwakenedFromSleep    the time duration the system spent while asleep</pre>
 *
 * @param pClientData the client data to pass through to the event proc
 *
 * @return the following:<pre>
 *        ______                                   ______
 *        ACTION                                   RETURN
 *        kLTCore_SleepAction_GoingToSleep         a kernel time in the future when you require the system to awaken from sleep
 *                                                 or LTTime_Zero() if you have no such requirement
 *        kLTCore_SleepAction_SleepAborted         ignored - return LTTime_Zero()
 *        kLTCore_SleepAction_AwakenedFromSleep    ignored - return LTTime_Zero()</pre>
 *
 * @note When returning a kernel time when you require the system to awaken from sleep (kLTCore_SleepAction_GoingToSleep only),
 *       the system will wake up and immediately go back to sleep if your thread has nothing to do.  Take care not to unjudiciously ping-pong
 *       in and out of sleep by specifying a wakeup requirement without later having anything to do.  This will unnecessarily consume battery life.
 *
 */


typedef LTTime (LTCore_EnterSleepModeProc)(LTTime softwareSleepWakeupTime, void * pClientData);
/**< LTCore callback for entering low power sleep mode
 *
 * This callback is called by LTCore to enter low power sleep mode.  It is
 * called in the context of a special LTCore thread of a priority higher
 *  than any other thread in the system.  The role of the LTCore_EnterSleepModeProc is to enter sleep
 * and only return when sleep mode is exited.  The LTTime returned should reflect the duration of time
 * that spent in sleep mode.  If the duration of time spent in sleep mode is indeterminable
 * then you probably should not register an LTCore_EnterSleepModeProc proc (and thereby not support sleep mode).
 * If you do decide to support sleep mode in your product, and you cannot determine
 * the amount of time spent in sleep mode (via use of a hardware realtime clock for example), then your
 * LTCore_EnterSleepModeProc should return LTTime_Zero()
 *
 * @param softwareSleepWakeupTime the kernelTime that software requires the sleep wakeup to occur
 *        or LTTime_Zero() if software has no sleep wakeup requirement
 * @param pClientData the clientdata specified when registering the enter sleep proc
 * @return the duration of time spent in sleep mode.
 *
 * @see SetEnterSleepModeProc
 */

typedef bool (LTCore_SleepModeDisallowanceGrantEnumProc)(u32 nDisallowanceGrantID, void * pCallerAddress, void * pClientData);
/**< LTCore enumeration callback for examining currently issued sleep disallowance grants (for debugging)
 *
 * @param nDisallowanceGrantID the disallowance grant id
 * @param pCallerAddress the source address where the disallowance grant was requested
 * @return true to continue enumeration, false to abort
 *
 * @see EnumerateSleepModeDisallowanceGrants
 */

enum {

    kLTCore_NumberOfInterruptPriorityBits = 4,   /**< Number of interrupt priorities (must be a power of 2) */
    kLTCore_MaxSleepModeModeDisallowanceGrants = 32
};

/**
 * LTCore Interrupt Priorities.
 * @ingroup ltcore_enum
 */
typedef_LTENUM_SIZED(LTCore_InterruptPriority, u32) {
    kLTCore_InterruptPriorityLowest  =  0,                                               ///< Lowest interrupt priority
    kLTCore_InterruptPriorityHighest = (1 << kLTCore_NumberOfInterruptPriorityBits) - 1  ///< Highest interrupt priority
};

/**
 * @struct LTCore
 * @brief The main interface for LTCore methods.
 *
 * (Insert more here)
 *
 * @ingroup ltcore
 */

/********************************
********************************
LTCore Library Root Interface */
TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTCore, 1);

struct LTCoreApi {

/*  ___________________________________________  Every LTLibrary root interface inherits these LTInterface prototype functions.  They are listed
    LTInterface Inherited Prototype Functions /  here in comment form for reference; they are declared by the typedef_LTLIBRARY_ROOT_INTERFACE macro.

    const char *     (* GetInterfaceName)(void);     returns the name of the interface; in this case "LTCore"
    u32              (* GetInterfaceVersion)(void);  returns the version number of the interface; in this case 1
    LTLibrary  *     (* GetLibrary)(void);           returns the LTLibrary * the interface came from; in this case this LTLibrary
    void             (* Destroy)(LTHandle handle);   destroys a handle (handles are associated with interfaces), always works correctly on any handle though
    LTInterfaceExt * (* GetLTInterfaceExt)(void);    for future expansion; currently there is no LTInterfaceExt

    _________________________________________    Every LTLibrary root interface also inherits these LTLibrary prototype functions.
    LTLibrary Inherited Prototype Functions /

    const char *     (* GetLibraryBuildVersion)(void);                returns the build id generated when the library was built
    LTInterface *    (* GetInterface)(const char * pInterfaceName);   gets additional interfaces by name that a library exports
    const LTInterface ** (* GetInterfaces)(void);                     gets a read-only null-terminated array of all interfaces exported by the library
    u32              (* GetRunFunctionStacksizeRequirement)(void);    used internally by LTCore to query a library's stacksize requirement for its run function, if existant
    int              (* Run)(int argc, const char ** argv);           used internally by LTCore to "run" a library in a new thread created with aforementioned stack size                                        \
    LTLibraryExt *   (* GetLTLibraryExt)(void);                       for future expansion; currently there is no LTLibraryExt

    ______________________________
    _____________________________/                                    Take off every 'zig'
    LTCore Interface Functions */

    INHERIT_LIBRARY_BASE

/*  _________________
    System Info Functions */
    const char * (* GetSoftwareVersion)(void) LT_ISR_SAFE;
    bool         (* IsDeveloperBuild)(void)   LT_ISR_SAFE;
    LTTime       (* GetBuildTime)(void)       LT_ISR_SAFE;

/*  _________________
    Time Functions */
    LTTime  (* GetKernelTime)(void) LT_ISR_SAFE;
        /**< Returns time (uptime) since LT initialized in units of \p LTTime.
         *
         * GetKernelTime() returns time in units of \p LTTime with a maximum
         * precision of 1 nanosecond and an actual precision that is hardware
         * dependent, based on a monotonically increasing high-resolution counter when
         * available or the OS tick counter when not.  The actual precision at runtime
         * may obtained by calling GetKernelTimeResolution().  GetKernelTime()
         * may be used to implement relative time calculations such as for performance
         * measurements or calculating whether or timeouts have been reached.
         *
         * @return time in units of \p LTTime (s64 nanoseconds)
         * @see GetKernelTimeResolution(), LTTime
         */

    LTTime  (* GetKernelTimeResolution)(void) LT_ISR_SAFE;
        /**< Returns the resolution (precision) of GetKernelTime() in units of \p LTTime.
         *
         * @return resolution (LTTime, e.g. 100hz -> 10ms, 50Mhz -> 20ns, etc.)
         * @see GetKernelTime(), LTTime
         */

    int    (* FormatCanonicalTimeString)(LTTime time, char * pStringBuff, u32 nBuffSize, bool bIncludeBrackets) LT_ISR_SAFE;
        /**< Formats LTTime into the canonical format of "[seconds.microseconds]".
         *
         *   @param[in]  time The time to format.
         *   @param[out] pStringBuff The string buffer to fill.
         *   @param[in]  nBuffSize The size in bytes of the pStringBuff buffer.
         *   @param[in]  bIncludeBrackets whether or not to encapsulate the string with brackets
         *   @return the number of characters excluding the null terminator that was written to pStringBuff
         *   @note nBuffSize should be >= 24 bytes for proper results; shorter values may result in clipping of the string.
         */

    LTTime  (* GetClockTimeUTC)(void) LT_ISR_SAFE;
        /**< Gets UTC as time since 1/1/1970, or LTTime_Zero() if time not set.
         *
         *  GetClockTimeUTC() returns LTTime set with the number of nanoseconds elapsed since 1/1/1970
         *  or LTTime_Zero() if the Time UTC has not been set.
         *
         *  @return LTTime set with the number of nanoseconds since 1/1/1970 or LTTime_Zero() if Time UTC has not been set.
         *
         *  @note If GetClockTimeUTC returns LTTime_Zero(), then the UTC time has not been set.  If GetClockTimeUTC() returns
         *        any other value, that value may be considered the actual accurate.  This relies on only setting time UTC with the accurate
         *        time.  Also, whenever time UTC becomes inaccurate (e.g. if the system wakes up out of low power mode and the elapsed time
         *        is indeterminate, then SetClockTimeBaseUTC() should be called with a timebase of {0,0} to clear clock time UTC.
         *  @note Code that can work with an approximate time, e.g. for operating TLS sockets before TimeUTC is set, can first call
         *        GetClockTimeUTC() and if it returns LTTime_Zero, can then call GetApproximateClockTimeUTC()
         *
         *  @see SetClockTimeBaseUTC(), GetApproximateClockTimeUTC()
         *
         */

    void    (* SetClockTimeBaseUTC)(const LTTimeBase * pTimeBaseUTC)    LT_ISR_SAFE;
        /**< accurately sets clock time UTC without introducing software delay errors
         *
         *  SetClockTimeBaseUTC sets clock time UTC using a structure with two fields, primaryClockTime and secondaryClockTime.
         *  primaryClockTime should be set with the kernelTime at the present moment when accurate clockTime UTC is obtained from the
         *  Internet, a h/w real time clock, or other source.  The clock time UTC that is obtained should be stored in secondaryClockTime
         *  as UTC nanoseconds since 1/1/1970.
         *
         *  Once primaryClocktime and secondaryClockTime are set within a time base structure, that structure can withstand arbitrary
         *  delays in processing (e.g. context switches, atypical system load, atypical network traffic, etc).  without affecting the
         *  accuracy of the clock time utc set, because the clock time utc is accompanied by the reference kernel time whence it
         *  was obtained.
         *
         *  @param pTimeBaseUTC the time base to set clock time UTC with
         *  @see GetClockTimeUTC()
         *
         *  @note Only call SetClockTimeBaseUTC when you are absolutely sure that the clock time UTC you are setting is accurate.
         */

    void    (* GetClockTimeBaseUTC)(LTTimeBase * pTimeBaseUTCToSet) LT_ISR_SAFE;
        /**< Gets UTC time base;
         *
         *   GetClockTimeBaseUTC fills pTimeBaseUTCToSet such that pTimeBaseUTCToSet->secondaryClockTime represents the clock time UTC when
         *   kernel time was at the value stored in pTimeBaseUTCToSet->primaryClockTime
         *
         *   @param ptimeBaseUTTCToSet a pointer to a time base to set the time base UTC with
         */

    LTTime  (* GetClockTimeUTCKernelOffset)(void) LT_ISR_SAFE;
        /**< Gets the difference between UTC and kernelTime.
         *
         *  GetClockTimeUTCKernelOffset() gets the difference between UTC and kernelTime, equivalent to UTC at system boot.
         *
         *  @return the offset in nanoseconds between clock time UTC and kernel time, or 0 if clock time UTC is not set.
         */

    LTTime  (* GetApproximateClockTimeUTC)(void)    LT_ISR_SAFE;
        /**< returns an approximation of clock time UTC if accurate time UTC has not been set, LTTime_Zero otherwise
         *
         *  GetApproximateClockTimeUTC() returns an LTTime set with the <i>approximate</i> number of UTC nanoseconds elapsed since 1/1/1970.
         *  Approximate time UTC is only available when accurate clock time UTC is unavailable (as indicated by GetClockTimeUTC() returning LTTime_Zero).
         *  Approximate time UTC will default to the build time of the LTCore library, or to a closer approximation if one is set with
         *  SetApproximateClockTimeBaseUTC() by a client external to LTCore.  A closer approximation, for example, may be the last known accurate
         *  UTC time from a previous boot session, tracked by a library external to LTCore.
         *
         *  Most clients should call GetClockTimeUTC() and never call GetApproximateClockTimeUTC() since an approximation of time UTC will
         *  likely lead to logic errors in client code if they assume any accuracy.  Such clients, when presented with an LTTime_Zero
         *  return value from GetClockTimeUTC should consider that time UTC is unknown and/or unset and deal exclusively with knowing or not knowing time UTC.
         *  and not try to make sense of approximations.
         *
         *  To emphazie the point, GetApproximateClockTimeUTC() should only be used by subsystems that are specifically designed to handle approximate time.  For example,
         *  a TLS subsystem may be specially designed to allow the use of approximate time to validate date ranges of built-in certificates
         *  whose valid date ranges are known to include the firmware build time. By so doing, the use of those certificates is enabled prior to
         *  having time UTC set from the Internet, which could, ironically, require use of those certificates to do so.
         *
         *  @return an LTTime set with the an approximation of the number of nanoseconds since 1/1/1970 or LTTime_Zero() if approximate
         *          clock time UTC is not available.
         *
         *  @note A client should always first call GetClockTimeUTC() for accurate clock time UTC and only if not available, and
         *        prepared to deal with approximate time, should call GetApproximateClockTimeUTC.
         *
         *  @note the default (unset) approximate time is the build time UTC of the LTCore library.  If both SetClockTimeBaseUTC,
         *        and SetApproximateClockTimeBaseUTC have never been called, then GetApproximateClockTimeUTC will return the build
         *        time UTC of the LTCore library.
         *
         *  @see GetClockTimeUTC(), SetApproximateClockTimeBaseUTC()
         *
         */

    void    (* SetApproximateClockTimeBaseUTC)(const LTTimeBase * pTimeBaseUTC)    LT_ISR_SAFE;
        /**< sets approximate clock time UTC
         *
         *  SetApproximateClockTimeBaseUTC() sets the approximate clock time UTC which is used by subsystems appropriately designed
         *  to rationally use a non-accurate, but approximate clock time UTC, when accurate clock time UTC is not available.
         *
         *  SetApproximateClockTimeBaseUTC() should only be called by the subsystems (typically only 1 in any given product build)
         *  that want to set an approximate time base UTC that is a closer approximation than the default.
         *
         *  @see GetApproximateClockTimeUTC()
         *
         *  @note Only call SetClockTimeBaseUTC when you have a better UTC time approximation than the default approximation of the
         *        build time UTC of the LTCore library.
         */


/*  ____________________
    Library Functions */
    LTLibrary *         (* OpenLibrary)(const char * pLibraryName);
        /**< Loads and opens a library by name.
         *
         * @param[in] pLibraryName Name of the library as a null-terminated string.
         * @return a pointer to the instance of the library, or @p NULL if the library
         * is unavailable or could not be loaded.
         * @see CloseLibrary()
         */

    void                (* CloseLibrary)(LTLibrary * pLibrary);
        /**< Closes an open library.
         *
         * @param[in] pLibrary Pointer to the library to close (as provided in the return
         * value of OpenLibrary()).
         * @see OpenLibrary()
         */

    bool                (* SetLibraryHook)(LTCore_LibraryHookFunction *pHookFunction);
        /**< Set the library hook function.
         *
         *   It is an error to set a hook when there is already a hook set.
         *
         * @param[in] pHookFunction Pointer to the hook function (or NULL to clear a hook).
         * @return true if successful, false if an existing hook was set and a new hook was provided.
         */

    LTInterface *       (* GetLibraryInterface)(LTLibrary * pLibrary, const char * pInterfaceName);                     /**< gets a library exported named interface */
    bool                (* GetLibraryBuildVersionString)(const char * pLibraryName, char * pBuildVersionStringToSet, u32 nBuildVersionStringBuffSize);
        /**< Retrieves an open library's build version string.
         *
         *   This method will fill *pBuildVersionStringToSet with an open library's build version string.
         *   It does not operate on libraries that are not opened.
         *   @param[in]  pLibraryName The name of the open library.
         *   @param[out] pBuildVersionStringToSet The string buffer to receive the build version string.
         *   @param[in]  nBuildVersionStringBuffSize The size in bytes of the pBuildVersionStringToSet buffer.
         *   @return true if successful, false if the buffer was too small to hold the build version string.
         *   @note The buffer size should be at least 128 bytes to hold the full build version string.
         */

    const char *      (* GetLTCoreLibraryBuildVersion)(void);
        /**< Retrieves the LTCore library's build version string.
         *
         *   @return the LTCore library's build version string.
         *   @note This function primarily exists to support each LTLibrary's inherited GetLibraryBuildVersion() function on builds that are statically linked to reduce size
         */

    bool                (* IsLibraryOpen)(const char * pLibraryName);
        /**< returns whether or not a library is currently open
         *
         * @param[in]  pLibraryName The name of the library
         * @return the current number of times the library has been opened or 0 if the library is not open or no such library exists
         */
    bool                (* GetLibrarySnapshot)(const char * pLibraryName, LTCore_LibrarySnapshot * pSnapshotToFill);
        /**< Fills *pSnapshotToFill with a snapshot of the state of the open library named pLibraryName.
         *
         * @param[in]  pLibraryName The name of the open library.
         * @param[out] pSnapshotToSet An LTCore_LibrarySnapshot structure that has nStructureSize set to sizeof(LTCore_LibrarySnapshot).
         * @return true if the library named pLibraryName is currently open and pSnapshotToFill->nStructureSize was set to a known sizeof(LTCore_LibrarySnapshot), false otherwise.
         * @note IMPORTANT: Set <tt>pSnapshotToFill->nStructureSize</tt> to <tt>sizeof(*pSnapshotToFill)</tt> before calling GetSnapshot.
         * @note If <tt>pSnapshotToFill->nStructureSize</tt> is not set to a known <tt>sizeof(LTCore_LibrarySnapshot)</tt> then this function will return false.
         * @note If @p pLibraryName is not currently open, then this function will return false.
         * @note This function unconditionally zeroes all LTCore_LibrarySnapshot fields except pLibraryName and nStructureSize.
         */
    void                (* SnapshotOpenLibraries)(LTCore_LibrarySnapshotCallbackProc * pCallbackProc, void * pClientData);
        /**< Passes the snapshot of each currently opened library to pCallbackProc.
         * @param[in] pCallbackProc Callback that will receive each snapshot.
         * @see LTCore_LibrarySnapshot
         */
    bool                (* EnumerateInstalledLibraries)(LTCore_InstalledLibrariesEnumProc * pEnumProc, void * pClientData);
        /**< Calls pEnumProc with the name of each installed library.
         *
         * @param[in] pEnumProc The callback to receive each installed library name.
         * @param[in] pClientData The client's data that will be passed through to pEnumProc.
         * @return true if enumeration continued until the end of the installed library set, false if the enumeration was aborted.
         */

/*  ___________________
    Memory Functions */
    void *              (*   Alloc)(LT_SIZE nBytes LT_CALLSITE_FUNCTION_PARAMETER);
        /**< Allocates memory, with debug information (file and line number of call).
         * @param[in] nBytes Number of bytes to allocate.
         * @param[in] callsite Call site information (normally supplied by macro @p lt_callsite).
         * @return Pointer to allocated memory, or @p NULL if allocation failed.
         * @see ReAlloc(), Free()
         */

    void *              (* AllocFromRegion)(LTMemoryRegion region, LT_SIZE nBytes LT_CALLSITE_FUNCTION_PARAMETER);
        /**< Allocate memory from a BSP-registered heap region (e.g. DMA-only RAM).
         * @param region    LTMemoryRegion identifying the region; obtain via
         *                  GetNamedMemoryRegion(name).  The value 0 is a sentinel
         *                  meaning "no specific region" — in that case the call
         *                  behaves like Alloc() and walks the unrestricted free list.
         *                  Non-zero values are 1-based indices into
         *                  LTCoreBSP_LTHeapConfig.pRegions[]; the LT runtime translates
         *                  region N to pRegions[N - 1].
         * @param nBytes    Number of bytes to allocate.
         * @return Pointer to allocated memory, or NULL if region is invalid,
         *         the region is exhausted, or nBytes is zero.
         * Memory is freed via Free() — the region is determined from the in-band
         * block header, not from a region argument at free time.
         */

    void *              (* ReAlloc)(void * pMem, LT_SIZE nBytes LT_CALLSITE_FUNCTION_PARAMETER);
        /**< Reallocates a block of memory, with debug information (file and line number of call).
         * NOTE: If pMem is NULL then ReAlloc operates as Alloc(nBytes ...).
         * NOTE: If nBytes is zero then ReAlloc operates as Free(pMem ...).
         * @param[in] pMem Pointer to existing block of allocated memory.
         * @param[in] nBytes New size of memory block, in bytes.
         * @param[in] callsite Call site information (normally supplied by macro @p lt_callsite).
         * @return Pointer to reallocated memory block, or @p NULL if reallocation
         * failed.
         * @see Alloc(), Free()
         */

    void                (*    Free)(void * pMem LT_CALLSITE_FUNCTION_PARAMETER);
        /**< Reclaims a block of allocated memory, with debug information (file and line number of call).
         * @param[in] pMem Pointer to memory block to free. If pMem is NULL, no operation is performed.
         * @param[in] callsite Call site information (normally supplied by macro @p lt_callsite).
         * @see Alloc(), ReAlloc()
         */

    LT_SIZE             (* GetAllocSize)(void * pMem);
        /**< Get the actual allocation size available to the user for an allocated memory block.
         * @note The returned value is greater than or equal to the size originally passed to Alloc or ReAlloc.
         * @param[in] pMem Pointer to memory block to obtain allocated size for.
         * @return size of allocated block in bytes, zero for an invalid block.
         */

    LT_SIZE             (* GetTotalSystemRAM)(void);                /**< Returns the size of the heap in bytes. */
    LT_SIZE             (* GetAvailableSystemRAM)(void);            /**< Returns the number of free bytes in the heap. */
    LT_SIZE             (* GetSystemRAMLowWatermark)(void);         /**< Returns the least ever number of free bytes in the heap. */
    LT_SIZE             (* GetLargestAvailableBlockInRAM)(void);    /**< Returns the largest available block in the heap. */
    LT_SIZE             (* GetCurrentRAMAllocationCount)(void);     /**< Gets the current amount of bytes allocated by LT. */
    LT_SIZE             (* GetMaxRAMAllocationCount)(void);         /**< Gets the max amount of bytes ever allocated by LT. */

    void                (* SetHeapTag)(char tag);

    bool                (* EnumerateHeapAllocatedBlockInfo)(LTThread hAllocatingThreadFilter, char tagFilter, LTCore_HeapAllocatedBlockInfoEnumCB *pCallback, void *pClientData);
        /**< enumerates through all heap allocated blocks, providing allocation info for each block
         *
         * %EnumerateHeapAllocatedBlockInfo() allows one to enumerate all through all allocated memory, getting called back synchronously
         *  with sets of HeapAllocatedBlockInfo structs (const pointer to array of structs and count) continuously until all matching heap blocks
         *  have been enumerated or until the client aborts enumeration.
         *
         * @param hAllocatingThreadFilter if non-zero, enumerated heap blocks are filtered to those allocated by the thread with handle hAllocatingThreadFilter, otherwise blocks from all threads are enumerated
         * @param tagFilter if non-zero, filters enumerated heap blocks to those allocated with the tag in tagFilter, otherwise blocks with all tags are enumerated
         * @param pCallback the callback procedure to call back with sets of heap allocation info structs
         * @param pClientData client data passed back to the callback procedure
         * @param return true if enumeration continued to completion, false if enumeration aborted
         *
         * @see SetHeapTag
         *
         * @note This function proceeds until either
         *       (a) the callback returns false, indicating the client wants to abort enumeration, or
         *       (b) all matching heap blocks  are enumerated.
         *
         * @note This function allows allocations to proceed during the enumeration and ensures that the allocation and freeing of blocks do not
         *       interfere with the enumeration, and to ensure this limits the enumeration to one client a ta time.
         *       If a second client calls EnumerateHeapAllocatedBlockInfo while another client is in process of enumerating, this function
         *       will return false immediately.  The second client may retry at a later point in time. This limitation is not viewed as problematic
         *       since this function exists primarily for the purpose of tracking down memory leaks during development and debugging.
         *
         * @note This function only succeeds if LTCORE_TRACE_HEAP_ALLOCATIONS is set to 1 in LTCoreDebugHelpers.h.  If it is not set, this
         *       function will return false immediately.
         * @note In release mode builds with LTCORE_TRACE_HEAP_ALLOCATIONS set to 1, LTCallsite information is only included
         *       in the HeapBlockInfo structs supplied to the callback if ENABLE_LT_CALLSITE_IN_RELEASE_MODE is set to 1 in LTTypes.h.
         *
         * @note ENABLE_LT_CALLSITE_IN_RELEASE_MODE increases stack requirements for lt_malloc, lt_realloc, and lt_free.  If your system
         *       has tightly tuned threadstacks and you see crashes with LTCORE_TRACE_HEAP_ALLOCATIONS and
         *       ENABLE_LT_CALLSITE_IN_RELEASE_MODE set to 1, then you may want to set LTTHREAD_STACK_FUDGE_BYTES to a non-zero value (say 128)
         *       in LTCoreDebugHelpers.h.
         */

    u64                 (* SnapshotMemstat)(void);
        /**< snapshots memstat info into a single 64 bit quantity
         *
         * Call %SnapshotMemstat to obtain a packed representation of a snapshot of current memory allocated, max memory allocated, and total heap memory.
         * Each number is encoded in a whole and fractional part where whole is in the range of 0-1023 and fractional is in the range
         * of 0-99 (percent).  The units used are best fit and are given in kb, mb, gb, or tb.
         * actually kib, mib, gib, or tib, but whose counting.  Oh, I am.
         *
         * The 64 bits are encoded as followed:
         * Bits   0-7: maximum lt allocation count, fractional part  [0..99] (.00% - .99%)
         * Bits  8-15: current lt allocation count, fractional part  [0..99] (.00% - .99%)
         * Bits 16-23: total heap memory, fractional part            [0..99] (.00% - .99%)
         * Bits 24-25: maximum lt allocation count units:            00=kb, 01=mb, 10=gb, 11=tb
         * Bits 26-27: current lt allocation count units:            00=kb, 01=mb, 10=gb, 11=tb
         * Bits 28-29: total heap memory units:                      00=kb, 01=mb, 10=gb, 11=tb
         * Bits 30-31: reserved
         * Bits 32-41: maximum lt allocation count, whole part       [0..1023]
         * Bits 42-51: current lt allocation count, whole part       [0..1023]
         * Bits 52-61: total heap memory, whole part                 [0..1023]
         * Bits 62-63: reserved
         */

    int             (* FormatCanonicalMemstatString)(u64 memstat, char * pStringBuff, u32 nBuffSize, bool bIncludeBrackets);
        /**< formats a memstat snapshot into the canonical format of [134.24k/256.00k used, 185.65k hi] with or without encapuslating brackets
         *
         *   @param[in]  memstat The memstat snapshot to format.
         *   @param[out] pStringBuff The string buffer to fill.
         *   @param[in]  nBuffSize The size in bytes of the pStringBuff buffer.
         *   @param[in]  bIncludeBrackets whether or not to encapsulate the string with brackets.
         *   @return the number of characters excluding the null terminator that was written to pStringBuff
         *   @note nBuffSize should be >= 42 bytes for proper results; shorter values may result in clipping of the string.
         */

    LTMemoryRegion (* GetNamedMemoryRegion)(const char *name);
        /**< gets an LTMemoryRegion by name
         *
         *   This function gets an LTMemoryRegion by name that is suitable for
         *   passing into lt_malloc_from_region.  If the region doesn't exist
         *   then the LTMemoryRegion returned will cause lt_malloc_from_region
         *   to behave as if lt_malloc was called.
         *
         *   @param name the name of the memory region to get
         *   @return the LTMemoryRegion if found, otherwise (LTMemoryRegion)0;
         *
         *   @note This function reads memory region values specified in LTDeviceConfig.json in the form:<pre>
         *         {
         *           "memory": {
         *                       "regions": [ { "name": "audio", "region": 1 },
         *                                    { "name": "video", "region": 2 }
         *                                  ]
         *                     }
         *         }
         *         </pre>
         *         The region numbers must be valid indices into reserved memory regions defined
         *         in the LTHeapConfig struct in the platform variant's LTCoreBSP implementation.
         */

/*  _____________
    Facilities */
             /*/ Resource instances of OS facilities are represented as typedefs of LTHandles. Each LTHandle      */
            /*/  has an attached interface for operating it, obtainable with GetHandleInterface(handle).         */
           /*/   Any interface can destroy any handle, even if the interface doesn't correspond to the handle.  */
          /*/  ________________________________________________________________________________________________*/
/*  __________/
    LTEvent */
    LTEvent (* CreateEvent)(const LTArgsDescriptor * pEventArgsDescriptor, LTEvent_DispatchProc * pEventDispatchProc, LTEvent_DispatchCompleteProc * pEventDispatchCompleteProc, LTEvent_NotifyImmediateEventStateProc * pNotifyImmediateEventStateProc, void * pNotifyImmediateEventStateClientData);
    /**< Creates an LTEvent.
     * An LTEvent is an asynchronous notification resource that has the following distinctive features:
     * 1. Events are delivered to event receivers in the context of the event receivers' own thread.
     * 2. Event specific data is delivered directly into the receivers' event procs as typed function parameters.
     * @param[in] pEventArgsDescriptor A description of the arguments of the event callback proc that receivers register.
     * @param[in] pEventDispatchProc The procedure supplied by the event creator that invokes receivers' event callback procs with event args.
     * @param[in] pEventDispatchCompleteProc An optional procedure to invoke asynchronously when all receivers have processed
     *            event notification so that the notifying thread can free or reclaim any data passed in args.  If notifying thread
     *            when the event was notified.  Pass @p NULL to specify no completion proc.
     * @param[in] pNotifyImmediateEventStateProc The procedure supplied by the event creator to immediately signal to new event receivers of the current event status.
     * @param[in] pNotifyImmediateEventStateClientData Callback data passed to the pNotifyImmediateEventStateProc callback.
     * @todo What is that "If notifying thread" fragment supposed to mean?
     */

/*  __________
    LTThread */
    LTThread            (* CreateThread)(const char * pName);
        /**< Spawns a new LTThread.
         *
         * @return a pointer to the LTThread, or @p NULL if the thread could not be
         * allocated and started.
         */

    LTOThread *         (* GetCurrentThreadObject)(void);
        /**< gets the current thread as an LTOThread object
         *
         * @return an LTOThread LTObject pointer referring to the current thread or NULL if
         *         not currently executing in LT Thread context (e.g. in ISR)
         * @note Do not cache this thread object.  It is only valid for the lifetime of the
         *       execution of the current thread
         */

/*  ______________________________________
    Object create and destroy functions */
    LTObject *        (* MacroCreateObject)(const char * pObjectName, const char * pSpecialization, u32 reserved, LTLibrary_MacroCreateObjectParms *createParms LT_CALLSITE_FUNCTION_PARAMETER);
        /**< Creates an object parameterized by lt_createobject
         *
         * @note Not intended for general use; use lt_createobject instead */

    LTObject *        (* MacroCreateDeviceObject)(const char * pDeviceObjectName, const char * pSpecialization, const char * pDeviceUnitName, LTLibrary_MacroCreateObjectParms *createParms LT_CALLSITE_FUNCTION_PARAMETER);
        /**< Creates a device object parameterized by lt_createdeviceobject
         *
         * @note Not intended for general use; use lt_createdeviceobject instead */

    LTObject *        (* MacroCreateDriverObject)(LTObject * pDeviceObjectInstance, LTLibrary_MacroCreateObjectParms *createParms LT_CALLSITE_FUNCTION_PARAMETER);
        /**< Creates a driver object parameterized by lt_createdriverobject_fordevice
         *
         * @note Not intended for general use; use lt_createdriverobject_fordevice instead */

    LTObject *        (* MacroCreateDriverObjectForUnitTest)(const char * pDriverObjectAPIName, const char * pDeviceUnitName, LTLibrary_MacroCreateObjectParms *createParms LT_CALLSITE_FUNCTION_PARAMETER);
        /**< Creates a driver object parameterized by lt_createdriverobject_fordevice
         *
         * @note Not intended for general use; use lt_createdriverobject_fordevice instead */

    void              (* DestroyObject)(LTObject *object);
        /**< Destroys an object
         *
         * %DestroyObject decrements the reference count of an object.  When the reference count reaches 0,
         * the object is destroyed.
         * The helper macro lt_destroyobject may be used in lieu of calling this function directly, if desired.
         *
         * @param[in] object the object to destroy
         * @see lt_createobject, lt_destroyobject
         * @note DestroyObject is provided as a helper function for the lt_destroyobject macro.  The macro
         * is provided for the convenience of pairing calls to lt_createobject and lt_destroyobject when
         * reference counting is not explicitly used.  calling lt_destroyobject or DestroyObject is equivalent
         * to callign object->API->RemoveRef(object).  The object is not actually destroyed until its internal
         * reference count reaches 0.
         */

    void              (*AddObjectRef)(LTObject *object);
        /**< Increments the reference count of an object
         *
         * %AddObjectRef increments the reference count of an object.
         *
         * @param[in] object the object on which to increment the reference count
         * @note calling AddObjectRef(object) is equivalent to calling object->API->AddRef(object)
         * @see RemoveObjectRef
         */

    void              (*RemoveObjectRef)(LTObject *object);
        /**< Decrements an object's reference count
         *
         * %RemoveObjectRef decrements the reference count of an object.  When the reference count reaches 0,
         * the object is destroyed.
         *
         * @param[in] object the object on which to remove the reference count
         * @note calling RemoveObjectRef(object) is equivalent to calling object->API->RemoveRef(object)
         * @see AddObjectRef
         */

    bool              (*GetDeviceAndUnitIndicesFromDeviceOrDriverObject)(LTObject * pDeviceOrDriverObject, u32 * pDeviceIndexToSet, u32 * pUnitIndexToSet);
        /**< Get the device index and unit index from a device or driver object for use with the LTDeviceKonfig object
         *
         * %GetDeviceAndUnitIndicesFromDeviceOrDriverObject will supply the index of the device entry and unit entry for
         * a device object or driver object.  These indices may be used in LTDeviceKonfig api functions for fast access
         * to the device unit configuration data.
         *
         * @param[in] pDeviceOrDriverObject the device or driver object from which to obtain the device and unit indices
         * @param[in/out] pDeviceIndexToSet a pointer to a u32 to receive the device index
         * @param[in/out] pUnitIndexToSet a pointer to a u32 to receive the unit index
         * @return true pDeviceOrDriverObject is a device or driver object that was created with lt_createdeviceobject or lt_createdriverobject_fordevice, false if it is not
         */

    bool              (*MockObject)(const char *objectName, const char *specialization, const char *mockSpecialization);
        /**< Set a mock for an object with a temporary specialization for the purpose of testing.
         * Once a mock is applied to a given object, subsequent calls to lt_createobject() and lt_createobject_named()
         * will utilize the mock specialization rather than the original.
         *
         * @note Mocks should NOT be used during normal operation, they are intended solely for testing purposes.
         * @note In any unit test run sequence, all calls to MockObject() and UnmockObject() must be made from the
         *       same thread. This thread is fixed as the one that makes the first call to MockObject() and remains
         *       the owning thread until all mock objects are cleared with UnmockObject(NULL, NULL). Once this
         *       clearing occurs, a different thread may take over the responsibility of establishing mocks.
         *
         * @param[in] objectName name of object to mock.
         * @param[in] specialization name of specialization to mock.
         * @param[in] mockSpecialization name of mock specialization to use in place of the original specialization.
         * @return true if object was successfully mocked, false on error.
         * @see UnmockObject */

    bool              (*UnmockObject)(const char *objectName, const char *specialization);
        /**< Revert one (or all) object mocks.
         *
         * @note if objectName or specialization are NULL this will unmock ALL objects and completely remove the
         *       side effects of mocking. This MUST be done after testing is concluded.
         *
         * @param[in] objectName name of object to unmock. Set this to NULL to remove all mocks.
         * @param[in] specialization name of specialization to unmock. Set this to NULL to remove all mocks.
         * @return    true if object(s) were successfully unmocked, false on error.
         * @see MockObject */

/*  ______________________________.  .  .  .  .  .  .  support for library authors to provide unique handle types and for querying handle properties
    Handle Management Functions */
    LTHandle           (* CreateHandle)(LTInterface * pHandleInterface, LT_SIZE nSizeInBytes);
        /**< Creates a handle with private storage in the amount of nSizeInBytes
         * and assigns pHandleInterface as its controlling interface.
         * @param[in] nSizeInBytes The number of bytes that will be allocated for handle private data.
         * @param[in] pHandleInterface The interface associated with the handle.
         *            Minimally the interface name identifies the handle type and
         *            the interface contains the (optional) OnDestroyHandle function that is called to perform
         *            any private data destruction/cleanup when DestroyHandle is called.
         *            If no destroy function is necessary, an empty interface may be used, e.g.: <pre>
         *              typedef_LTLIBRARY_INTERFACE(IMyHandleType, 1) LTLIBRARY_EMPTY_INTERFACE;
         *              define_LTLIBRARY_DEFINITION(IMyHandleType)    LTLIBRARY_EMPTY_DEFINITION; </pre>
         * @return the newly created handle on success or 0 on invalid arguments failure
         *         (@p NULL pHandleInterface, 0 nSizeInBytes, or nSizeInBytes too large for memory allocation).
         * @note CreateHandle() is provided for implementers of handle types.  CreateHandle() creates and returns raw handles.
         *       The client is responsible for initializing their handle private data.
         * @see GetHandlePrivateData()
         */
    void                (* DestroyHandle)(LTHandle handle);
        /**< Calls the handle interface's OnDestroyHandle() proc to allow for handle private data uninitialization,
         * and then frees the handle and its private data memory.
         * @param[in] handle The handle to destroy.
         * @note Any handle may be destroyed by calling LTCore->DestroyHandle(handle) or
         *       the handle interface's Destroy() function, e.g. iMyHandle->Destroy(myHandle).
         *       Any interface can destroy any handle and the OnDestroyHandle() call will be properly routed,
         *       e.g. <tt>iEvent->Destroy(hThread)</tt>, while seemingly nonsensical, will correctly destroy the hThread.
         *       In effect, every interface has Destroy() as a synonym for LTCore->DestroyHandle().
         */
    bool                (* IsHandleValid)(LTHandle handle);
        /**< Determines if a handle is valid (has been created and not yet destroyed).
         * @param[in] handle The handle being queried.
         * @return true if the handle has been created and not yet destroyed, false otherwise.
         */
    void *              (* ReserveHandlePrivateData)(LTHandle handle);
        /**< Returns a pointer to the phandles private data, reserving the handle from being destroyed.
         * @param[in] handle the handle to reserve the private data from
         * @return a pointer to the handle private data, or @p NULL if handle is invalid.
         * @note While private data is reserved, the reservation prevents the handle from being destroyed.  Any
         *       any call to DestroyHandle will be deferred until all private data reservations are released.
         * @note For every call to ReserveHandlePrivateData, a corresponding call to ReleaseHandlePrivateData must be made.
         * @see ReleaseHandlePrivateData
         */
    void                (* ReleaseHandlePrivateData)(LTHandle handle, void *privateData);
        /**< Releases previously reserved handle privateData reservation.
         * @param[in] handle The handle to release the previously reserved privateData to.
         * @param[in] privateData the private data previously reserved from the handle.
         * @see ReserveHandlePrivateData
         */

    void *              (* GetHandlePrivateData)(LTHandle handle);
        /**< Deprecated function to get the handle's private data.
         *
         * @param[in] handle the handle to get the private data from
         * @return the handles private data or NULL if the handle is invalid
         *
         * @note THIS FUNCTION IS NOT SAFE.  CONVERT TO ReserveHandlePrivateData() and ReleaseHandlePrivateData() pairings.
         */

    LTInterface *       (* GetHandleInterface)(LTHandle handle);
        /**< Gets the handle's associated interface.
         * @param[in] handle The handle being queried.
         * @return the handle's associated interface, or @p NULL if no associated interface exists.
         */
    LTInterface *       (* GetNameCheckedHandleInterface)(LTHandle handle, const char * pInterfaceName);
        /**< Gets the handle's associated interface if its name matches pInterfaceName.
         *
         * @param[in] handle The handle being queried.
         * @param[in] pInterfaceName The name of the interface to return.
         * @return the handle's associated interface if the name of the associated interfaces matches pInterfaceName, @p NULL otherwise.
         * @note This function is provided for the convenience macro @p lt_gethandleinterface.
         *       Without the macro: <pre>
         *           ILTEvent * iLTEvent = (ILTEvent **)LT_GetCore()->GetHandleInterface(event);
         *               / * CAUTION: this will blindly cast the returned interface to ILTEvent *even if event is not a event handle * / </pre>
         *       With the macro: <pre>
         *           ILTEvent * iLTEvent = lt_gethandleinterface(ILTEvent, event);
         *               / * only returns the interface if event is a event handle * / </pre>
         */
    const char *        (* GetHandleInterfaceName)(LTHandle handle);
        /**< Gets the name of the handle's associated interface (analogous to handle type).
         * @param[in] handle The handle being queried.
         * @return the name of the handle's associated interface, or @p NULL if the handle is invalid.
         */
    LTLibrary *         (* GetHandleLibrary)(LTHandle handle);
        /**< Gets the library that the handle comes from.
         * @param[in] handle The handle being queried.
         * @return the handle's library.
         */
    u32                 (* GetHandleReservationCount)(LTHandle handle);
        /**< returns current reservation count of the handle's private data
         *
         *  Call &GetHandleReservationCount to determine the number of times ReserveHandlePrivateData()
         *  has been called on a handle without a matching call to ReleaseHandlePrivateData().  This function
         *  is provided as a debugging aid
         *
         * @param[in] handle The handle being queried.
         * @return the reservation count of the handle's private data
         */

    const char *        (* GetHandleStateString)(LTHandle handle);
        /**< returns current state of a handle
         *
         *  Call &GetHandleStateString to retrieve a printable string of the handle's current state.
         *  The string is one of: "healthy", "DestroyPending", "DestroyScheduled", "InDestroy", or "invalid"
         *  The strings mean:
         *  healthy - normal handle state
         *  DestroyPending - a Destroy() call has been made to destroy the handle when the handle's private data was reserved
         *  DestroyScheduled - a previously DestroyPending handle is scheduled for destruction because all private data reservations have been released
         *  InDestroy - the handle is being destroyed
         *  invalid - the handle passed in was invalid
         *
         *  @note The string returned is a static readonly string.  Do not attempt to modify or free.
         *
         *  @param[in] handle The handle being queried.
         *  @return the string representing the handles current state
         */

    u32 (* GetHandlesByInterface)(LTHandle *handlesArrayToFill, u32 nArrayCount, LTInterface *pInterface);
        /**< Fills user supplied array with known handles matching specified interface.
         * This method fills a user supplied array with handles matching the specified interface.
         * @param[in] handlesArrayToFill. A pointer to a user allocated array where the handles will be placed or NULL if only a count is desired.
         * @param[in] nArrayCount The number of elements handlesArrayToFill is allocated to hold.
         * @param[in]  pInterface The interface to match or NULL to match all handles.
         * @return the number handles that matches pInterface
         * @note Call once with NULL array to determine alloc size, then alloc, then call again as follows:
         * <pre> \
         * LTCore *pCore = LT_GetCore();
         * const ILTEvent * pEventInterface = lt_getlibraryinterface(ILTEvent, pCore);
         * u32 nCount = pCore->GetHandlesByInterface(NULL, 0, pEventInterface);
         * LTHandle *myArray = nCount ? lt_malloc(sizeof(LTHandle) * nCount) : NULL;
         * if (myArray) {
         *     u32 newCount = pCore->GetHandlesByInterface(myArray, nCount, pEventInterface);
         *     / * newCount returned is the actual number of handles that exist currently;
         *         if newCount < nCount then newCount handles put in array;
         *         if newCount >= nCount, then nCount handles put in array
         *       * /
         *     if (newCount < nCount) nCount = newCount; / * adjust nCount to match num put in array * /
         *     for (newCount = 0; newCount < nCount; newCount++) lt_consoleprint("Handle %d = 0x%x\n", (int)newCount, (int)myArray[newCount]);
         *     lt_free(myArray);
         * }
         * </pre>
         *
         */

    u32 (* GetHandlesByInterfaceName)(LTHandle *handlesArrayToFill, u32 nArrayCount, const char *pInterfaceName);
        /**< Fills user supplied array with known handles matching specified interface name.
         * This method fills a user supplied array with handles whose interface matches the specified interface name.
         * @param[in] handlesArrayToFill. A pointer to a user allocated array where the handles will be placed or NULL if only a count is desired.
         * @param[in] nArrayCount The number of elements handlesArrayToFill is allocated to hold.
         * @param[in] pInterfaceName The name of the interface to match or NULL to match all handles.
         * @return the number handles whose interface name matches pInterfaceName
         * @note Call once with NULL array to determine alloc size, then alloc, then call again as follows:
         * <pre> \
         * const char *pInterfaceNameLTEvent *= "ILTEvent";
         * u32 nCount = pCore->GetHandlesByInterfaceName(NULL, 0, pInterfaceNameLTEvent);
         * LTHandle *myArray = nCount ? lt_malloc(sizeof(LTHandle) * nCount) : NULL;
         * if (myArray) {
         *     u32 newCount = pCore->GetHandlesByInterface(myArray, nCount, pInterfaceNameLTEvent);
         *     / * newCount returned is the actual number of handles that exist currently;
         *         if newCount < nCount then newCount handles put in array;
         *         if newCount >= nCount, then nCount handles put in array
         *       * /
         *     if (newCount < nCount) nCount = newCount; / * adjust nCount to match num put in array * /
         *     for (newCount = 0; newCount < nCount; newCount++) lt_consoleprint("Handle %d = 0x%x\n", (int)newCount, (int)myArray[newCount]);
         *     lt_free(myArray);
         * }
         * </pre>
         *
         */

    u32 (* GetHandleCount)(void);
        /**< Get the number of handles in handle pool */

    u32 (* GetTotalHandleBytesOverhead)(void);
        /**< returns the total amount of memory used by the handle table and handle records */

/*  ______
    LTArgs - method for capturing [subset of types from] varargs from va_list into heap memory */
    LTArgs *            (*CreateArgs)(u32 nNumArgs);
        /**< Creates an empty args structure that can hold nNumArgs. */
    LTArgs *            (*CreateArgsFromDescriptor)(LTArgsDescriptor * pDescriptor);
        /**< Creates an empty args structure with a copy of pDescriptor. */
    LTArgs *            (*CreateArgsFrom_lt_va_list)(LTArgsDescriptor * pDescriptor, lt_va_list vaList);
        /**< Creates an args structure using @p pDescriptor to pull and set args from vaList.  Does not call @p lt_va_end when done. */
    void                (*UpdateArgsFrom_lt_va_list)(LTArgs *pArgs, lt_va_list vaList);
        /**< Updates an args structure using with args from vaList.  pArgs must have been previously created with the correct descriptor.  Does not call @p lt_va_end when done. */
    void                (*TrimArgs)(LTArgs *pArgs);
        /**< Frees all kLTArgType_charstar memory. */
    void                (*DestroyArgs)(LTArgs * pArgs);
        /**< Destroys a previously created LTArgs structure, freeing not only the LTArgs structure but also all
           kLTArgType_charstar memory. */

/*_______________
  Resource Tree */
    bool (*ReadResourceValue)(const LTResourceTree *resourceTree, u32 offset, const char *resourceKey, LTResourceValue *resourceValue);
        /**< Reads a resource value from a resource tree
         *
         * Call %ReadResourceValue() to read values from the resource trees of libraries.  Resource Trees are compact binary representations
         * of subsetteed json that are automatically generated and compiled into a library's .rodata section at build time when a library has the file
         * "resources/ResourceTree.json" in its source folder.  Library resource trees may be accessed at runtime by calling the function
         * GetResourceTree(), an autogenerated function that returns a pointer to the libary's resource tree or NULL if a library doesn't have a resource tree.
         *
         * %ReadResourceValue() provides the capability to read values from library ResourceTrees.
         *
         * @param  resourceTree the resource tree to read from.   Call a library's GetResourceTree() function to obtain
         *         its resource tree.
         * @param  offset the offset location of the tree object node in which to scope the search
         *         Valid values are:
         *             - 0 to start searching from the beginning of the resource tree
         *             - valid object offsets obtained from FindResourceOffset()
         *             - LTResourceValue.offset obtained by a previous successful call to ReadResourceValue()
         * @param  resourceKey the key (name or logical relationship) of the resource variable to read. Valid values are:
         *             - NULL to return the value at the given offset.
         *             - kLTResourceKey_FirstChild to return the first child value of the object value at the given offset.
         *             - kLTResourceKey_NextSibling to return the next sibling value of the object value at the given offset.
         *             - a '/' separated hierarchical path to the value to search for. Example: "a/b/c" returns the value named "c"
         *               contained within the object value named "b" which is itself contained with the object value named "a" located
         *               within the container object (or root) starting at the given offset.
         * @param  resourceValue a pointer to an LTResourceValue struct to have filled in with the read variable, if found.
         *         If NULL is passed in for resourceValue then this function just tests for the existence of any value matching key
         * @return true if resource Key is found, false otherwise
         * @note   when the type found is string or binary, the pointer values in the resourceValue struct point to
         *         readonly data directly embedded in the resource tree.  No memory management is required; do not attempt to
         *         modify or free those values and do not attempt to use them after the library containing the resource tree is closed.
         * @note   Pass in 0 for the offset to search the entire tree for the key.  Use offset values returned from FindResourceOffset()
         *         to narrow the scope of the search to the subtree contained in the object node at that offset.
         */

     u32 (*FindResourceOffset)(const LTResourceTree *resourceTree, u32 offset, const char *resourceKey);
        /**< Finds the offset of the value in the resource tree with specified key
         *
         * Call %FindResourceOffset() to obtain an offset into the tree where the value with key resourceKey is located.
         * If the key refers to a value with type kLTResourceValueType_Object, then that offset may be passed into ReadResourceValue
         * to narrow the scope of the key to the subtree that starts at that offset.
         *
         * @param resourceTree the resourceTree to read from
         * @param offset a tree offset that can be used to specify a subtree for searching or 0 search the whole tree
         * @param resourceKey the resource key of the value to find the offset of
         * @return the absolute offset from the start of resourceTree where the internal tree node with specified
         *         resourceKey lives or 0 if the specified resourceKey wasn't found
         */
    u32 (*CountResourceChildren)(const LTResourceTree *resourceTree, u32 offset, const char *resourceKey);
        /**< Counts the number of children of the resource value in the resource tree with specified key
         *
         * Call %CountResourceChildren() to obtain the number of children of the resource value with key resourceKey.
         *
         * @param resourceTree the resourceTree to read from
         * @param offset a tree offset that can be used to specify a subtree for searching or 0 search the whole tree
         * @param resourceKey the resource key of the value to find the offset of
         * @return the number of children of the resource value with specified resourceKey.
         */
    const char* (*ResourceTypeToString)(LTResourceValueType type);
        /**< Converts a resource value type to a string
         *
         * Call %ResourceTypeToString() to convert a resource value type to a string.
         *
         * @param type the resource value type to convert
         * @return a string representation of the resource value type
         */
    u32 (*ResourceValueToString)(const LTResourceValue *resourceValue, char *buffer, u32 bufferSize);
        /**< Gets string representation of resource value
         *
         * Call %ResourceValueToString() to convert a resource value to a string.
         *
         * @param resourceValue the resource value to convert
         * @param buffer a buffer to hold the string representation of the resource value
         * @param bufferSize the size of the buffer
         * @return the number of characters (without null term) added to buffer, 0 for arrays and objects
         */

/*  _______________
    ISR Services */
    bool    (*InsideInterruptContext)(void) LT_ISR_SAFE;
        /**< Indicates whether the function is called from within an interrupt service routine.
         *
         * Call %InsideInterruptContext() to determine whether or not the processor
         * is currently executing in an interrupt (or processor exception) context
         *
         * @return true if the proessor is currently running in an interrupt context
         */

    bool    (*InterruptsAreDisabled)(void) LT_ISR_SAFE;
        /**< Indicates whether or not interrupts are currently disabled
         *
         * Call %InsideInterruptContext() to determine whether or not the processor
         * is currently executing in an interrupt or processor exception context
         *
         */

    LT_SIZE (*Disable)(void) LT_ISR_SAFE;
        /**< Disable interrupts.  Disable() may be called from within
         * a thread context or an ISR context.  If called from within an ISR context and the system
         * supports interrupt priorities, Disable() will prevent the interrupt handler from being
         * preempted by higher priority interrupts.  On systems without interrupt priorities, calling
         * Disable() from an ISR is a no-op, except a corresponding call to Enable() is still required,
         * as described below.
         * When called from a thread context, all interrupts at all priorities are disabled and task
         * switching is disabled.
         *
         * CAVEAT 1: When LT is a hosted guest on a host operating system such as Linux, Windows, iOS, Android, MacOS, etc.,
         * Disable() is simulated and task switching to other LT threads is disabled; host os threads may still run.
         * CAVEAT 2: When interrupts are disabled within a thread context, LTCore facilities are unavailable, including LT_ISR_SAFE facilities.
         * CAVEAT 3: Whether or not interrupts are explicitly disabled from within an ISR context, LT_ISR_SAFE functions may always be called from an ISR context.
         *
         * Each call to Disable() returns an interrupt disable mask that must be passed into the corresponding call to Enable(),
         * and calls to Disable() nest; to re-enable interrupts after one or more calls to Disable(), a corresponding
         * call to Enable() must be made for each call to Disable(), using the proper corresponding mask when calling Enable().
         * When the disable nest count goes from 1 to zero interrupts are actually enabled.
         *
         * CAUTION: No scheduling features of LT are permitted to be called while interrupts are disabled or while executing
         *          in an interrupt context - this means no thread scheduling or mutex functions, nor any features that use them.
         *          Exception: ILTThread->QueueTaskProc is LT_ISR_SAFE and may be called from within an ISR at any time,
         *          but also note that it may *not*    ever be called from a thread context when interrupts are disabled.
         *
         * NOTE: Enable() and Disable() are provided primarily for writers of interrupt handlers and for threads that
         *       directly service the interrupt handlers, typically both contained in an LTDriver library.
         *
         * CAUTION: Do not use Enable() and Disable() for critical sections in regular threads.  Use LTMutex instead.
         */

    void    (* Enable)(LT_SIZE nDisableMask) LT_ISR_SAFE;
        /**< Enable interrupts.
         *
         * @param[in] nDisableMask The disable mask returned by the most recent call to Disable.
         * @see Disable()
         */

    void    (*SetInterruptVector)(u32 nInterrupt, LTCore_InterruptHandler * pInterruptHandler, LTCore_InterruptPriority priority);
        /**< Set interrupt vector @p nInterrupt to the @p InterruptHandler handler and initial priority (@p priority).  Note that the
         * underlying implementation is architecture dependent and therefore will likely support fewer or more than the number of LTCore
         * interrupt priorities. It is the job of the underlying implementation to map LTCore priority numbers to actual interrupt priorities.
         * Note also that on some systems the priority argument may be ignored altogether.
         */

    void    (*SetInterruptPriority)(u32 nInterrupt, LTCore_InterruptPriority priority);
        /**< Change interrupt vector @p nInterrupt to priority (@p priority).
         * Note that some architectures do not support changing interrupt priority dynamically,
         * and in these situations this call will be ignored.
         * @see SetInterruptVector
         */

/*  ______________________
    command line access */
    int           (* GetArgc)(void);
        /**< Returns the count of command line arguments passed into LT_Run().
         *
         */

    const char ** (* GetArgv)(void);
        /**< Returns the array of command line argument strings passed into LT_Run().
         * The first element is the name of the current platform.
         * The second element is the name of the program as invoked.
         */

/* _______________________
   Low Power Sleep Mode */
    void (* SetEnterSleepModeProc)(LTCore_EnterSleepModeProc * pEnterSleepModeProc, void * pClientData);
        /**< sets the procedure that will be called when sleep mode is to be entered
         *
         * Call %SetEnterSleepModeProc to set the procedure that will be called to enter sleep mode.
         * When the system is at idle for the duration specified by %SetEnterSleepModeIdleDelay() then
         * pEnterSleepModeProc will be called.
         *
         * @param pEnterSleepModeProc the procedure to call to enter sleep mode
         * @param pClientData client data passed back to pEnterSleepModeProc
         * @see SetEnterSleepModeIdleDelay
         *
         * @note Once LTCore has determined that sleep mode shall be entered because the sleep idle delay
         *       has elapsed and no outstanding disallowance grants are issued, then pEnterSleepModeProc
         *       will be called.  In order to ensure that the pEnterSleepModeProc executes before any threads
         *       can become ready and run before the pEnterSleepModeProc is called, pEnterSleepModeProc is called
         *       in a thread of a special reserved priority higher than any other user or system thread.  The pEnterSleepModeProc
         *       should effect only what is necessary to enter sleep mode.
         */

    void (* SetEnterSleepModeIdleDelayAndMinimumSleepDuration)(LTTime idleDelay, LTTime minimumSleepDuration);
        /**< sets the amount of idle time that must elapse entering sleep mode and minimumSleepDuration
         *
         * @param idleDelay the time duration in idle after which the system will enter low power sleep mode
         *        or LTTime_Zero() to disable low power sleep mode
         * @param minimumSleepDuration the minimum time duration the system can spend in sleep mode
         *        or LTTime_Zero() if there is no minimum.  LTCore will not invoke the enter sleep mode proc
         *        if a sleep wakeup timer would cause the system to wakeup before the minimumSleepDuration expires
         *
         * @note When all theads are wait-blocked, mutex-blocked, or sleeping, i.e. there are no threads ready-to-run,
         *       the system enters an idle state. If the system remains in idle state for the duration set by
         *       %SetEnterSleepModeIdleDelay, then the enter sleep mode procedure set with %SetEnterSleepModeModeProc
         *       will be called, provided no sleep disallowances have been issued.
         *
         * @note If any threads in the system continuously have thread timers that expire, perform thread sleeps expire, or have task procs queued
         *       at an interval shorter than the enter sleep idle delay specified, the system will never able to enter sleep
         *
         * @see SetEnterSleepModeProc
         * @see DisallowSleepMode
         * @see ReallowSleepMode
         * @see SetWakeupTimer
         * @
         */

    u32 (* DisallowSleepMode)(void);
        /**< Issues a temporary disallowance of sleep mode, preventing sleep mode from being entered
         *
         * Call %DisallowSleepMode() to receive a temporary grant of disallowance of sleep mode that will prevent sleep mode
         * from being entered.  There are 32 disallowance grants issued on a first come-first serve basis.  The
         * intention is that clients who are performing a power critical operation and wish to prevent sleep
         * mode from being entered during the critical operation should call %DisallowSleepMode and then once the
         * operation is complete, return the grant by calling %ReallowSleepMode().
         *
         * @param pCallerAddress The source address where the call to DisallowSleepMode was made (lt_returnaddress())
         * @return the disallowance grant id that should passed back to %ReallowSleepMode or 0 if no disallowance grants
         *         are currently available
         *
         * @note pCallerAddress is stored internally with each disallowance grant for debugging purposes.
         *       For convenience, the macro lt_disallowsleep() is provided for automated specification of the pCallerAddress parameter.
         * @see ReallowSleepMode
         */

    void (* ReallowSleepMode)(u32 disallowanceGrant);
        /**< Reallows a previous disallowance of sleep mode
         *
         * Call %ReallowSleepMode() to return a previously issued grant of sleep disallowance, thus reallowing the disallowance issued by that grant.
         *
         * @param disallowanceGrant The previously issued disallowance grant to reallow
         *
         * @see DisallowSleepMode
         */

    bool (* EnumerateSleepModeDisallowanceGrants)(LTCore_SleepModeDisallowanceGrantEnumProc * pEnumProc, void * pClientData);
        /**< enumerates the issued sleep disallowance grants
         *
         * @param pEnumProc the sleep grant disallowance enumeration proc to call for each issued disallowance grant
         * @param pClientData the client data to pass back to the pEnumProc
         * @return true if enumeration continued through completion, false if enumeration was aborted
         */

    void (* OnSleepAction)(LTCore_SleepActionEventProc * pEventProc, void * pClientData);
        /**< registers for notification of sleep actions
         *
         *  Call OnSleepAction to register a sleep action event proc to get notified of sleep actions.
         *
         * @param pEventProc the sleep action event proc that will be notfied of sleep actions
         * @param pClientData client data passed back to the sleep action event proc
         *
         * @note the set of allowed actions is limited.  Please see LTCore_SleepActionEventProc for details.
         * @see LTCore_SleepActionEventProc, NoSleepAction
         *
         *
         */

     void (* NoSleepAction)(LTCore_SleepActionEventProc * pEventProc);
        /**< unregisters from notification of sleep actions
         *
         *  Call NoSleepAction to unregister a previously registered sleep action event proc.
         *
         * @param pEventProc the sleep action event proc to unregister
         *
         * @see OnSleepAction
         *
         * @note NOTE NOTE NOTE : THIS FUNCTION IS CURRENTLY DISABLED.
         *
         */

/*  ________
    LTLOG */
    void    (* Log )(const char * pSectionName, const char * pTag, u32 nLogFlags, const char * pFormat, ... ) LT_ISR_SAFE LT_PRINTF_FORMAT_FUNCTION(4);
        /**< Writes a log entry to the system log; used by LTLOG macro */
    void    (* LogV)(const char * pSectionName, const char * pTag, u32 nLogFlags, const char * pFormat, lt_va_list args ) LT_ISR_SAFE;
        /**< va_list version of Log; used by LTLOGV macro  */
    void    (* Trace)(LTTraceStream *stream, LTTracePayloadType type, ...);
    /**< Write low-overhead trace data */
    void    (* TraceAddStreams)(LTTraceStream **streams);
    /**< Call during library open to register all trace streams declared by that library */
    void    (* TraceRemoveStreams)(LTTraceStream **streams);
    /**< Call during library close to remove previously added trace streams */
    bool    (* TraceSetStreamEnabled)(u32 streamId, bool enabled);
        /**< Set the enable flag of a trace stream
         *
         * @streamName Name of the stream that will be set
         * @enabled Value of the enabled flag to set
         * @returns true if the stream was found and its enable flag was set, false otherwise
         *
         * It's possible to enable/disable all streams by calling this function with id values incrementing from 0 until it returns false.
         */
    void    (* TraceKnownStreams)(void);
        /**< Write out descriptions of all added streams
         *
         * This will generally be called by a trace client when it receives a new connection
         * in order to get a manifest of streams.
         */
    void    (* SetLogHookFunction)(LTCore_LogHookFunction * pLogHookFunction);
        /**< Set a log hook function that logs will be dispatched to
         *
         * @param pLogHookFunction a pointer to a log hook function or NULL to clear the log hook function
         * @param pTraceHookFunction a pointer to a trace hook function or NULL to clear the trace hook function
         *
         * Only a single log hook can be set at at time.  The purpose of the log hook function is to queue logs for deferred processing by a "log processing" thread.  The log hook function
         * will be called by all threads that use any LTLOG and LTASSERT macros and functions, except for LTLOG_STOMP macros and functions.  The log hook function must therefore:
         * <ol><li>Use as little stack space as possible.</li><li>Be interrupt safe *and* thread safe</li><li>Execute as quickly as possible</li><li>Not use LTLOG nor LTASSERT macros or functions</li></ol>
         *
         * @note %SetLogHookFunction(NULL) may be called to clear the log hook function from any thread. When setting a non-NULL log hook function, however, %SetLogHookFunction must be
         *       called from the LTThread_InitProc of the thread that is created to process the logs queued by the log hook function.  LTCore requires this in order
         *       to disregard all logs initiated from that processing thread to prevent infinite run-away log generation.
         *
         */

    void    (* SetTraceHookFunction)(LTCore_TraceHookFunction pTraceHookFunction);

/*  ________________________________________
    console output and debugging funtions */
    void    (* ConsolePrint)(const char * pFormatString, ...)  LT_ISR_SAFE LT_PRINTF_FORMAT_FUNCTION(1); /**< Serialized output to log buffer, truncated at 720 chars (244 from ISR); uses 128 bytes stack space. */
    void    (* ConsolePrintV)(const char * pFormatString, lt_va_list args) LT_ISR_SAFE;                  /**< Same as ConsolePrint but with @p va_list. */
    void    (* ConsolePutChars)(const char * pSrc, u32 nChars) LT_ISR_SAFE;                              /**< Puts @p nChars chars; serialized via log buffer. */
    void    (* ConsolePutString)(const char * pString) LT_ISR_SAFE;                                      /**< Puts a string; serialized via log buffer. */
    void    (* ConsoleStomp)(const char * pFormatString, ...) LT_ISR_SAFE  LT_PRINTF_FORMAT_FUNCTION(1); /**< Direct putchar to console; no serialization; truncated at 192 chars (192 from ISR); uses 256 bytes stack space. */
    void    (* ConsoleStompV)(const char * pFormatString, lt_va_list args) LT_ISR_SAFE;                  /**< Same as ConsoleStomp but with @p va_list. */
    void    (* ConsoleStompChar)(char ch) LT_ISR_SAFE;                                                   /**< Direct putchar to console; no serialization. */
    void    (* ConsoleStompChars)(const char * pChars, u32 nChars) LT_ISR_SAFE;                          /**< Direct putchars to console; no serialization. */
    void    (* ConsoleStompString)(const char * pString) LT_ISR_SAFE;                                    /**< Direct putstring to console; no serialization. */
    void    (* FlushConsoleOutput)(void);                                                                /**< Flushes buffered console output; not ISR safe. */

    void    (* SetConsoleCharactersReceivedProc)(LTCore_ConsoleCharactersReceivedProc * pCharReceivedProc, LTCore_ConsoleBreakReceivedProc * pBreakReceivedProc, void * pClientData);
        /**< Sets the callback proc that receives console input characters.
         *
         * Sets the callback procedure that will receive characters input (typed) and break (normally Ctrl-C) into the console.
         * The first client who calls this function wins.  To forcefully reset the callbacks, pass in @p NULL for pCharReceivedProc, pBreakReceivedProc,
         * and pClientData; after this another callback can be set.  The callback is called in the context of the calling thread.
         *
         * @param[in] pCharReceivedProc The callback procedure to receive console input characters, or @p NULL to clear the set procedure.
         * @param[in] pBreakReceivedProc The callback procedure to receive the break character, or @p NULL to clear the set procedure (or to ignore break).
         * @param[in] pClientData The client data passed into pCharReceivedProc and pBreakReceivedProc.
         */

    void    (* EnableSerialConsole)(bool bEnable);
        /**< Enables or disables the serial console
         *
         * Enables serial console input and output through the BSP.  When disabled, the serial-console UART is shut down or
         * otherwise disabled.  Used to disable serial-console input and output in secured units that do not have console
         * access granted by LTAT or any other security-related means.
         *
         * Does nothing if the BSP does not provide EnableSerialConsole() through its API.
         *
         * @param[in] bEnable true to enable the serial console, false to disable it.
         */

    bool    (* AssertFailed)(const char * pFile, int nLine, const char * pTest) LT_ISR_SAFE;
        /**< Gives the platform an opportunity to perform platform- or environment-specific assert notification.
         *
         * @param[in] pFile The source file containing the assertion.
         * @param[in] nLine The line number of he assertion.
         * @param[in] pTest DOCUMENTATION_NEEDED.
         * @returns DOCUMENTATION_NEEDED.
         *
         * @internal
         *
         * Used by LT_ASSERT macro, implemented in PlatformBSP.
         *
         * For example, on Windows with Visual Studio 2017, when LT_ASSERT fires in debug mode,
         * the standard MSVC runtime Abort/Retry/Ignore dialog comes up and
         * pressing abort will land you in the debugger with your program halted on the line of code with the LT_ASSERT
         * with the file open and cursor positioned thereupon, full stack trace,
         * for all threads, etc.  If the platform bsp implements AssertFailed and returns true, then LT_ASSERT will call
         * LT_GetCore()->DebugBreak() which is what actually invokes the debugger in the just given example.
         */

    void    (* DebugBreak)(void) LT_ISR_SAFE;   /**< Generates a breakpoint to call the debugger, if detected, otherwise force exception with data dump to console */

    bool    (* HandleInDestroy)(LTHandle handle); /**< Convenience function for interface definition macros. */

    LTStdlib * (* GetLTStdlib)(void);  /**< Returns the LTStdlib interface; convenience function for @p lt_stdlib macros. */


    /*  ______________
    os shutdown */
    void    (* TerminateLT)(int nExitCode);
        /**< Initiates graceful shutdown of LT.
         * This method is non-blocking and initiates a graceful shutdown of LT by calling Terminate() on all running threads.
         * Once TerminateLT is called, no threads will be able to queue thread functions and no further events will be dispatched.
         * Thread cleanup and state preservation must be handled in thread client data release functions and in thread exit functions.
         *
         * @param[in] nExitCode The exit code for LT_Run() to return.
         * @see ILTThread
         */
    bool    (* IsLTShuttingDown)(void);
        /**< indicates whether or not LT is in process of shutting down
         *
         * When TerminateLT() is called, LT terminates all non-system threads and waits for their termination and then proceeds with shutdown of LTCore
         * and graceful exit.  As threads in the system shut down, any remaining queued task procs in thread queues will be aborted and any queued
         * client data release procs will be called, followed by the thread's ExitProc().  Clients can call %IsLTShuttinDown to determine
         * if their thread is being terminated intentionally because LT is shutting down or not.  This main use case is found in the LTSystemHealth
         * library, which will reboot the system in its HealthMonitor thread's ThreadExit proc, unless IsLTShuttingDown() returns true,
         * in which case LTSystemHealth will refrain from rebooting the system to allow the graceful shutdown of LT to proceed.
         *
         * @return whether or not LT is currently shutting down
         */
    /*  ______________
    crash dump callbacks */
    void    (* RegisterCrashdumpWriteCallback)(LTCore_CrashdumpWriteCallback * pCallback);
        /**< Registers crash dump write callback
         * This method is used by Crashdump manager to hook it up to the FaultHandler.
         *
         * @param[in] pCallback callback function pointer.
         */
};

/*_____________________
  LTConsoleConnector */
typedef void (LTConsoleConnector_PutCharProc)(const char * pChars, u32 numChars, void * pClientData) LT_ISR_SAFE;
    /**< The type of the 'putchar' procedure you supply to LTCore when connecting your auxiliary console
     * @note The procedure you supply must be both thread-safe and ISR-safe.
     */
typedef_LTObject(LTConsoleConnector, 1) {
    bool (* ConnectConsole)(LTConsoleConnector * pConsoleConnector, LTConsoleConnector_PutCharProc *pPutCharProc, void *pClientData);
        /**< connect an auxiliary console with the LT console
         *
         * call ConnectConsole() to install an auxiliary console, connecting it with LTConsole output
         * and enabling provision of LTConsole input.  This LT default UART system
         * console to be replicated on another console style device, such over a network socket or
         * an LTEvent or to an adjunct device, e.g. a USB CDC device.
         *
         * @param pConsoleConnector this LTConsoleConnector object
         * @param pPutCharProc your callback procedure that will be called for you to deliver console output to your auxiliary console implementation
         * @param pClientData client data passed back to your callback procedure
         * @return true if console connection is available, false otherwise
         *
         * @see DisconnectConsole()
         *
         * @note your pPutCharProc can be called back at any time from any thread or ISR.  It is up to *YOU* to
         *       make your pPutCharProc both THREAD_SAFE and ISR_SAFE.
         * @note you must not do anything in your pPutCharProc that would cause any additional console output -
         *       that means it cannot consoleprint, consolestomp, log, or assert. */

    void (* DisconnectConsole)(LTConsoleConnector * pConsoleConnector);
        /**< disconnects your previously connected auxiliary console
         */
    u32  (* SubmitConsoleInput)(LTConsoleConnector * pConsoleConnector, const char *pChars, u32 numChars) LT_ISR_SAFE;
} LTOBJECT_API;
/**< connect an auxiliary console as an adjunct to the default LT system console
 *
 *   LTConsoleConnector allows an auxiliary implementation of console i/o to be connected to the LT system console.
 *   It is used as follows: <pre>
 *
 *     LTConsoleConnector *consoleConnector = lt_createobject(LTConsoleConnector);
 *     if (consoleConnector) {
 *         if (consoleConnector->API->ConnectConsole(consoleConnector, &MyPutcharProc, pMyClientData)) {
 *             consoleConnector->API->SubmitConsoleInput(consoleConnector, pInputCharsReceivedOnMyConsole, nNumCharsFromMyConsole);
 *             consoleConnector->API->DisconnectConsole(consoleConnector);
 *         }
 *         lt_destroyobject(consoleConnector);
 *     }
 *
 *     / *
 *          NOTE: LTCore, to output characters to your auxiliary console, will call the LTConsoleConnector_PutCharProc
                  callback you supplied to ConnectConsole(). LTCore can call your callback at any time from any thread
 *                or any ISR.  It is UP TO YOU to ensure  your LTConsoleConnector_PutCharProc is both ISR SAFE and THREAD SAFE.
 *                In order to submit input characters from your auxiliary console into LTCore, you call SubmitConsoleInput()
 *                with the characters.  You may call SubmitConsoleInput() at any time from any thread or ISR.
 *
 * CRITICAL NOTE: Your LTConsoleConnector_PutCharProc must NOT do anything that could possibly generate console output.  Any
 *                procedures you rely on in your PutCharProc likewise are prohibited from causing console output.  This includes
 *                LTLOG, LT_ASSERT, lt_consoleprint, lt_consolestomp, LT_GetCore()->AnythingThatHasAnAssertOrLOGInIt()
 *                Every line of code that is executed from the invocation of your PutCharProc until it returns must be examined
 *                and validated by you to have zero asserts or logs, or prints, even if they are #ifdef'd out. * / </pre>
 *
 */

/* __________________
  / LTCore Typedefs /
 /_________________/
/____________________
 Logging Subsystem */

/**____________________________
             LTCore_LogFlags */
/** Flags used for logging.
 * @ingroup ltcore_enum
 */
typedef_LTENUM_SIZED(LTCore_LogFlags, u32) {
    kLTCore_LogFlags_LogTypeRaw             = 0,            ///< Raw log message without preamble; used for ConsolePrint
    kLTCore_LogFlags_LogTypeVerbose         = 1,            ///< Verbose log message
    kLTCore_LogFlags_LogTypeDebugLog        = 2,            ///< Debug log message
    kLTCore_LogFlags_LogTypeLog             = 3,            ///< Standard log message
    kLTCore_LogFlags_LogTypeYellowAlert     = 4,            ///< Warning log message
    kLTCore_LogFlags_LogTypeRedAlert        = 5,            ///< Error log message
    kLTCore_LogFlags_LogTypeAssert          = 6,            ///< Assertion log message

    kLTCore_LogFlags_LogTypeMask            = 0x7,          ///< Bitmask for the log type field

    kLTCore_LogFlags_LogToConsole           = (1 << 4),     ///< Send log message to console
    kLTCore_LogFlags_LogToServer            = (1 << 5),     ///< Send log message to server
    kLTCore_LogFlags_LogFromISR             = (1 << 6),     ///< indicates log originated from an ISR
    kLTCore_LogFlags_ConsoleStomp           = (1 << 7),     ///< output immediately, stomping on buffered output in progress
    kLTCore_LogFlags_NullArgs               = (1 << 9),     ///< copy format string directly into log buffer, bypassing snprintf, ignoring varargs
    kLTCore_LogFlags_Flush                  = (1 << 10),    ///< flush any deferred log processing
    kLTCore_LogFlags_DumpThreadName         = (1 << 11),    ///< print the thread name of the log initiating thread
    kLTCore_LogFlags_DumpMemstats           = (1 << 12),    ///< print the memstats as they are when the log is initiated
    kLTCore_LogFlags_Reserved3              = (1 << 13),    ///< reserved for internal/future use
    kLTCore_LogFlags_Reserved2              = (1 << 14),    ///< reserved for internal/future use
    kLTCore_LogFlags_Reserved1              = (1 << 15),    ///< reserved for internal/future use

};

/**_____________________________________
               LTCore_LibrarySnapshot */
typedef struct LTCore_LibrarySnapshot {
    char                name[kLTLibrary_MaxNameBufferSize];                   /**< Name of the library for which this snapshot pertains */                                                /* 40 bytes */
    char                rootInterfaceName[kLTLibrary_MaxNameBufferSize];      /**< Name of the library's root interface */                                                                /* 40 bytes */
    u32                 nRootInterfaceVersion;                                /**< Version of the library's root interface */                                                             /*  4 bytes */
    LTInterfaceType     rootInterfaceType;                                    /**< Type of the library's root interface */                                                                /*  4 bytes */
    u16                 nStructureSize;                                       /**< Size of this struct - set this to sizeof(LTCore_LibrarySnapshot) before calling GetLibrarySnapshot */  /*  2 bytes */
    u16                 nOpenCount;                                           /**< Number of times OpenLibrary() has been called for this library */                                      /*  2 bytes */
} LTCore_LibrarySnapshot;
/**< Information about a single loaded library.
 *   @see LTCore::GetLibrarySnapshot, LTCore::SnapshotOpenLibraries.
 *   @ingroup ltcore_struct
 */
LT_STATIC_ASSERT_SIZE_32_64(LTCore_LibrarySnapshot, 92, 92)

typedef struct LTCore_HeapAllocatedBlockInfo {
    void *              pAddr;      /*  4 or  8 */  /**< Pointer to the allocated buffer */                             /* 4/8 bytes */
    LTThread            hThread;    /*  4 or  4 */  /**< Handle to the thread which allocated this buffer */            /* 4/8 bytes */
    u32                 nBytes;     /*  4 or  4 */  /**< Size of the allocated buffer */                                /*   4 bytes */
    LTCallSite          callsite;   /* 12 or 24 */  /**< Call site of allocation */                                    /* 12/20 bytes */
} LTCore_HeapAllocatedBlockInfo;
/**<
 *   Information about a single heap allocation.
 *   @see LTCore::EnumerateHeapAllocatedBlockInfo.
 *   @ingroup ltcore_struct
 */
LT_STATIC_ASSERT_SIZE_32_64(LTCore_HeapAllocatedBlockInfo, 24, 40)

/**___________________________________________
                     LTCore_LibraryHookSite */
typedef_LTENUM_SIZED(LTCore_LibraryHookSite, u32) {
    kLTCore_LibraryHookSite_SubstituteOpen = 0, /**< Hook is being invoked before LTLibraryManager_OpenLibrary()  */
    kLTCore_LibraryHookSite_AfterOpen,          /**< Hook is being invoked after LTLibraryManager_OpenLibrary()   */
    kLTCore_LibraryHookSite_BeforeClose,        /**< Hook is being invoked before LTLibraryManager_CloseLibrary() */
 };


/**_______________________________________
                     LTCore_SleepAction */
typedef_LTENUM_SIZED(LTCore_SleepAction, u32) {
    kLTCore_SleepAction_GoingToSleep = 0,   /**< called when the system is about to go to sleep */
    kLTCore_SleepAction_SleepAborted,       /**< called when someone took a sleep disallowance grant in their SleepActionEventProc */
    kLTCore_SleepAction_AwakenedFromSleep   /**< called when the system has awakened from sleep */
 };


/**<
 *   LTCore Interrupt Priorities.
 *   @ingroup ltcore_enum
 */

#include <lt/LT.h>

/**************************************
 * LTCore library convenience macros *
 ************************************/
 #define lt_openlibrary(libraryName)                        ((libraryName *)LT_GetCore()->OpenLibrary(#libraryName))
 #define lt_closelibrary(pLibrary)                          LT_GetCore()->CloseLibrary((LTLibrary *)(pLibrary))
 #define lt_getlibraryinterface(interfaceName, library)     ((interfaceName *)LT_GetCore()->GetLibraryInterface((LTLibrary *)(library), #interfaceName))
 #define lt_gethandleinterface(interfaceName, handle)       ((interfaceName *)LT_GetCore()->GetNameCheckedHandleInterface((handle), #interfaceName))
 #define lt_ishandleinterface(interfaceName, handle)        ((0 == lt_strcmp(#interfaceName, LT_GetCore()->GetHandleInterfaceName((handle)))) ? true : false)
   /**< Returns true if @p interfaceName matches the name of the handle's attached interface; false if names don't match or handle is invalid.
     * Example: <tt>bool bHandleIsEvent = lt_ishandleinterface(ILTEvent, hHandle);</tt>
     */
 #define lt_destroyhandle(handle)                           LT_GetCore()->DestroyHandle((handle))
 #define lt_reservehandleprivatedata(handle)                LT_GetCore()->ReserveHandlePrivateData((handle))
 #define lt_releasehandleprivatedata(handle, privateData)   LT_GetCore()->ReleaseHandlePrivateData((handle), (privateData))

/****************************************
 * LTCore sleep mode convenience macros *
 ****************************************/
 #define lt_disallowsleepmode()                               LT_GetCore()->DisallowSleepMode()
 #define lt_reallowsleepmode(disallowanceGrantID)             LT_GetCore()->ReallowSleepMode((disallowanceGrantID))

/************************************************************
 * LTObject lifecycle                                       *
 *                                                          *
 * lt_createobject(objectType)                              *
 * lt_createobject(objectType, specializationType)          *
 * lt_createobject_named(objectName, specializationName)    *
 * lt_destroyobject(object)                                 *
 * ltobject_addref(object)                                  *
 * ltobject_removeref(object)                               *
 *                                                          *
 ************************************************************/
#define lt_createobject(objectType, ...)        LT_CREATEOBJECT(objectType, LTTYPES_IS_VARIADIC_LIST_EMPTY(__VA_ARGS__), __VA_ARGS__)
    /**< creates an object
     *
     *   @param objectType the type of object to create
     *   @param ...        optional (non-default) specialization type
     *   @return the created object or NULL if the object creation failed
     *
     *   The typical use of create object is with one parameter, the object type.
     *   Example: <pre>LTMutex *mutex = lt_createobject(LTMutex);</pre>
     *
     *   An optional second argument may specify a [non-default] specialized
     *   implementation of the objectType's api.
     *   Example: <pre>LTFile *file = lt_createobject(LTFile, LTMemoryFile);</pre>
     *
     *   No other arguments are supported.
     */

#define lt_createobject_named(objectName, specializationName)   LT_GetCore()->MacroCreateObject(objectName, specializationName, 0, GET_LTOBJECT_MACROCREATEOBJECT_PARMS() comma_lt_callsite())
    /**< creates an object with type and specialization by name
     *
     *   lt_createobject_named allows one to programmatically determine the object specialization of an object for some
     *   generic object api.  For example, a system may have an LTObject API called MyPlugin with multiple plugin implementations
     *   that all conform to, implement (are specializations) of the MyPlugin object.  Each of these specializations is given a unique name
     *   (by the define_LTObjectApi macro) and these specializations may be instantiated programmatically with lt_creatobject_named.
     *
     *   @param objectName the string name of the base object
     *   @param specializationName the string name of the specialization that implements objectName's object api
     *   @return the created specialization instance or NULL if object creation failed
     *   @note a generic LTObject* is returned and must be casted to the proper API pointer type specified by objectName.
     *
     *   Example: <pre>     *
     *     const char * pluginToCreate = ReadPluginNameFromSettings();
     *     MyPlugin *plugin = (MyPlugin *)lt_createobject_named("MyPlugin", pluginToCreate);
     *   </pre>
     *
     */

#define lt_destroyobject(object)                LT_GetCore()->DestroyObject((LTObject *)object)
    /**< destroys an object
     *
     *   @param object - the object to destroy
     *   @note Objects are reference counted and to be precise, lt_destroyobject decrements the reference count.
     *         When the count reaches 0, the object is destroyed.  The typical object lifecycle is to simply
     *         create and destroy an object with lt_createobject and lt_destroyobject.
     *
     *   Example:<pre>
     *   LTMutex *mutex = lt_createobject(LTMutex);
     *   lt_destroyobject(mutex);
     *   </pre>
     */

#define ltobject_addref(object)                                 LT_GetCore()->AddObjectRef((LTObject*)(object))
    /**< increments object reference count
     *
     *   ltobject_addref increments an object's reference count.  An object is created with reference count one.
     *   An object will not be destroyed until its reference count is decremented to zero.
     *
     *   @param object - the object on which to increment the reference count
     *
     */

#define ltobject_removeref(object)                              LT_GetCore()->RemoveObjectRef((LTObject*)(object))
    /**< decrements an object's reference count
     *
     *   ltobject_removeref decrements an object's reference count.  When an object's reference count goes to zero,
     *   it will be destroyed.
     *
     *   @param object - the object on which to increment the reference count
     *   @note lt_destroyobject is absolutely equivalent to ltobject_removeref
     *
     */

/**********************************************************************
 * Creating LTDevice and LTDriver objects                             *
 *                                                                    *
 * lt_createdeviceobject(deviceType)                                  *
 * lt_createdeviceobject(deviceType, unitName)                        *
 * lt_createdriverobject_fordevice(driverType, deviceObjectInstance)  *
 * lt_createdriverobject_forunittest(driverType, unitName)            *
 *                                                                    *
 **********************************************************************/
#define lt_createdeviceobject(deviceType, ...)  LT_CREATEDEVICEOBJECT(deviceType, LTTYPES_IS_VARIADIC_LIST_EMPTY(__VA_ARGS__), __VA_ARGS__)
    /**< creates a device object that operates its default or a specific device unit
      *
      *  @param deviceType the type of device object to create
      *  @param ...        optional specific device unit to operate
      *
      *  Examples:<pre>
      *
      *  LTDeviceKeypad * defaultKeypad = lt_createdeviceobject(LTDeviceKeypad);
      *      / * creates a keypad device object that uses the default keypad device unit
      *          (first LTDeviceKeypad unit listed in LTDeviceConfig.json) * /
      *
      *  LTDeviceKeypad * keypadOne = lt_createdeviceobject(LTDeviceKeypad, KEYPAD1);
      *      / * creates a keypad device object that specifically uses the KEYPAD1 device unit * /
      */

#define lt_createdriverobject_fordevice(driverType, deviceObjectInstance) ((driverType *)LT_GetCore()->MacroCreateDriverObject((LTObject *)deviceObjectInstance, GET_LTOBJECT_MACROCREATEOBJECT_PARMS() comma_lt_callsite()))
    /**< create proper driver object for device object's device unit; for use by device object impls only (typically in device object constructors)
      *
      *  lt_createdriverobject_fordevice() creates the driver used for driving the device unit operated by a device object.
      *
      *  @param driverType the type of the driver object to create
      *  @param deviceObjectInstance the device object instance the driver is to be used by
      *  @return the driver object for driving the device unit being operated, or NULL if driver object creation failed
      *
      *  Example:<pre>
      *  / * client code * /
      *  LTDeviceKeypad *keypad = lt_createdeviceobject(LTDeviceKeypad, "KEYPAD1");
      *
      *  / * In LTDeviceKeypad.c: * /
      *  static bool LTDeviceKeypadImpl_ConstructObject(LTDeviceKeypadImpl *keypad) {
      *      keypad->driver = lt_createdriverobject_fordevice(LTDriverKeypad, keypad);
      *      return keypad->driver ? true : false;
      *  }
      *  </pre>
      *  In this example, when lt_createdeviceobject(LTDeviceKeypad, "KEYPAD1") is called, the LTDeviceKeypadImpl
      *  constructor calls lt_createdriverobject_fordevice() which creates the driver identified in LTDeviceConfig.json for KEYPAD1.
      *
      *  If lt_createdeviceobject(LTDeviceKeypad, "KEYPAD2") were called, the same LTDeviceKeypadImpl constructor
      *  calling lt_createdriverobject_fordevice() would get the driver for KEYPAD2 created.
      */

#define lt_createdriverobject_forunittest(driverType, deviceUnit)     ((driverType *)LT_GetCore()->MacroCreateDriverObjectForUnitTest(#driverType, #deviceUnit, GET_LTOBJECT_MACROCREATEOBJECT_PARMS() comma_lt_callsite()))
    /**< creates a driver object standalone from any device object; intended only for unit tests of driver objects
      *
      *  lt_createdriverobject_fordevice() creates a driver object instance for unit testing purposes.
      *
      *  @param driverType the type of the driver object to create
      *  @param the unitName the driver object drives
      *  @return the driver object for driving the device unit specified, or NULL if driver object creation failed
      *  @note this is for unit testing only.  Results are undefined in a driver object is created at the same time by
      *        a device object and instantiated directly with lt_createdriverobject_forunittest()
      *
      *  Example:<pre>LTDriverKeypad *kepadDriver = lt_createdriverobject_forunittest(LTDriverKeypad, "KEYPAD1");</pre>
      */

/*************************************************
 * lt_createobject helpers, do not call directly *
 *************************************************/
    /* helper macros for lt_createobject */
    #define lt_createobject_typed(objectType, specializationType)     ((objectType *)(lt_createobject_named(#objectType, #specializationType)))
    #define lt_createobject_typed_with_specialization(objectType, specializationType)     lt_createobject_typed(objectType, specializationType)
    #define LT_CREATEOBJECT_TEST_EMPTY_1(objectType, ...)             lt_createobject_typed(objectType, objectType##Impl)
    #define LT_CREATEOBJECT_TEST_EMPTY_0(objectType, ...)             lt_createobject_typed_with_specialization(objectType, LTTYPES_FORCE_VARIADIC_EXPANSION(LTTYPES_EXTRACT_FIRST_ARG(__VA_ARGS__)))
    #define LT_CREATEOBJECT_TEST_EMPTY(objectType, empty, ...)        LTTYPES_FORCE_VARIADIC_EXPANSION(LT_CREATEOBJECT_TEST_EMPTY_##empty(objectType, __VA_ARGS__))
    #define LT_CREATEOBJECT(objectType, empty, ...)                   LT_CREATEOBJECT_TEST_EMPTY(objectType, empty, __VA_ARGS__)
    /* helper macros for lt_createdeviceobject */
    #define lt_createdeviceobject_typed_defaultUnit(deviceType, specializationType)             ((deviceType *)(lt_createdeviceobject_named(#deviceType, #specializationType, NULL)))
    #define lt_createdeviceobject_typed_deviceUnit(deviceType, specializationType, deviceUnit)  ((deviceType *)(lt_createdeviceobject_named(#deviceType, #specializationType, #deviceUnit)))
    #define lt_createdeviceobject_typed_specifiedUnit(deviceType, specializationType, deviceUnit)  lt_createdeviceobject_typed_deviceUnit(deviceType, specializationType, deviceUnit)
    #define lt_createdeviceobject_named(deviceName, specializationName, deviceUnitName)   LT_GetCore()->MacroCreateDeviceObject(deviceName, specializationName, deviceUnitName, GET_LTOBJECT_MACROCREATEOBJECT_PARMS() comma_lt_callsite())
    #define LT_CREATEDEVICEOBJECT_TEST_EMPTY_1(deviceType, ...)      lt_createdeviceobject_typed_defaultUnit(deviceType, deviceType##Impl)
    #define LT_CREATEDEVICEOBJECT_TEST_EMPTY_0(deviceType, ...)      lt_createdeviceobject_typed_specifiedUnit(deviceType, deviceType##Impl, LTTYPES_FORCE_VARIADIC_EXPANSION(LTTYPES_EXTRACT_FIRST_ARG(__VA_ARGS__)))
    #define LT_CREATEDEVICEOBJECT_TEST_EMPTY(deviceType, empty, ...) LTTYPES_FORCE_VARIADIC_EXPANSION(LT_CREATEDEVICEOBJECT_TEST_EMPTY_##empty(deviceType, __VA_ARGS__))
    #define LT_CREATEDEVICEOBJECT(deviceType, empty, ...)            LT_CREATEDEVICEOBJECT_TEST_EMPTY(deviceType, empty, __VA_ARGS__)

/********************************************
 ********************************************
 * LTCore library DEBUG and LOGGING macros *
 ******************************************
 * LT_ASSERT  */
#if 0
    /* DRW 07-Feb-23: we're enabling asserts in release mode now, "for great justice" */
    #ifdef LT_DEBUG
      #define LT_ASSERT(x)    LT_ISR_SAFE   (void)((!!(x)) || (false == LT_GetCore()->AssertFailed(__FILE__, __LINE__, "" #x "")) || (LT_GetCore()->DebugBreak(), 0))
    #else
      #define LT_ASSERT(x)    LT_ISR_SAFE
    #endif
#else
      #define LT_ASSERT(x)    LT_ISR_SAFE   (void)((!!(x)) || (false == LT_GetCore()->AssertFailed(__FILE__, __LINE__, "" #x "")) || (LT_GetCore()->DebugBreak(), 0))
#endif

/*************************************
 * LTLOG LOGGING AND PRINTING MACROS *
 *************************(***********/
#define lt_consoleprint LT_GetCore()->ConsolePrint
#define lt_consoleputstring LT_GetCore()->ConsolePutString
#define lt_consolestomp LT_GetCore()->ConsoleStomp

#define DEFINE_LTLOG_SECTION(pSection)          static const char *           s_pLTLOG_Section = pSection;
#define LTLOG(pTag, pFormat, ...)               LT_ISR_SAFE LT_GetCore()->Log(s_pLTLOG_Section,  pTag, kLTCore_LogFlags_LogTypeLog      | (kLTCore_LogFlags_LogToConsole), pFormat, ##__VA_ARGS__)
#define LTLOG_SERVER_ONLY(pTag, pFormat, ...)   LT_ISR_SAFE LT_GetCore()->Log(s_pLTLOG_Section,  pTag, kLTCore_LogFlags_LogTypeLog      | (kLTCore_LogFlags_LogToServer), pFormat, ##__VA_ARGS__)
#define LTLOG_SERVER(pTag, pFormat, ...)        LT_ISR_SAFE LT_GetCore()->Log(s_pLTLOG_Section,  pTag, kLTCore_LogFlags_LogTypeLog      | (kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_LogToServer), pFormat, ##__VA_ARGS__)
#define LTLOG_YELLOWALERT(pTag, pFormat, ...)   LT_ISR_SAFE LT_GetCore()->Log(s_pLTLOG_Section,  pTag, kLTCore_LogFlags_LogTypeYellowAlert | (kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_LogToServer), pFormat, ##__VA_ARGS__ )
#define LTLOG_REDALERT(pTag, pFormat, ...)      LT_ISR_SAFE LT_GetCore()->Log(s_pLTLOG_Section,  pTag, kLTCore_LogFlags_LogTypeRedAlert | (kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_LogToServer), pFormat, ##__VA_ARGS__ )
#define LTLOG_REDALERT_AND_REBOOT(pTag, pFormat, ...)       LT_ISR_SAFE (void)((LT_GetCore()->Log(s_pLTLOG_Section,  pTag, kLTCore_LogFlags_LogTypeRedAlert | (kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_LogToServer), pFormat, ##__VA_ARGS__ ), 0) || (((void)LT_GetCore()->AssertFailed(__FILE__, __LINE__, "redalert")), 0) || (LT_GetCore()->DebugBreak(), 0))
#define LTLOG_STOMP(pTag, pFormat, ...)         LT_ISR_SAFE LT_GetCore()->Log(s_pLTLOG_Section,  pTag, kLTCore_LogFlags_LogTypeLog      | (kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_ConsoleStomp), pFormat, ##__VA_ARGS__)
#define LTLOG_CALLER()                                      LT_GetCore()->Log(__FUNCTION__, "line:" LT_STRINGIFY(__LINE__) "_called_from", kLTCore_LogFlags_LogTypeLog   | (kLTCore_LogFlags_LogToConsole), "0x%llx", LT_Ps64((s64)(lt_returnaddress())))
#define LTLOG_STOMP_CALLER()                                LT_GetCore()->Log(__FUNCTION__, "line:" LT_STRINGIFY(__LINE__) "_called_from", kLTCore_LogFlags_LogTypeLog   | (kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_ConsoleStomp), "0x%llx", LT_Ps64((s64)(lt_returnaddress())))
#define LTLOGV(pTag, pFormat, args)             LT_ISR_SAFE LT_GetCore()->LogV(s_pLTLOG_Section, pTag, kLTCore_LogFlags_LogTypeLog      | (kLTCore_LogFlags_LogToConsole), pFormat, args)
#define LTLOGV_SERVER(pTag, pFormat, args)      LT_ISR_SAFE LT_GetCore()->LogV(s_pLTLOG_Section, pTag, kLTCore_LogFlags_LogTypeLog      | (kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_LogToServer), pFormat, args)
#define LTLOGV_YELLOWALERT(pTag, pFormat, args) LT_ISR_SAFE LT_GetCore()->LogV(s_pLTLOG_Section, pTag, kLTCore_LogFlags_LogTypeYellowAlert | (kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_LogToServer), pFormat, args)
#define LTLOGV_REDALERT(pTag, pFormat, args)    LT_ISR_SAFE LT_GetCore()->LogV(s_pLTLOG_Section, pTag, kLTCore_LogFlags_LogTypeRedAlert | (kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_LogToServer), pFormat, args)
#define LTLOGV_STOMP(pTag, pFormat, args)       LT_ISR_SAFE LT_GetCore()->LogV(s_pLTLOG_Section, pTag, kLTCore_LogFlags_LogTypeLog      | (kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_ConsoleStomp), pFormat, args)
#define LTLOG_LOGNULL                           LT_ISR_SAFE 1 ?      LT_UNUSED(s_pLTLOG_Section) : LTLog_LogNull
        LT_INLINE                               LT_ISR_SAFE void     LTLog_LogNull(const char * pTag, const char * pFormat, ...) { LT_UNUSED(pTag); LT_UNUSED(pFormat); }

#ifdef  LT_DEBUG
    #define LTLOG_FENTER()                      LT_ISR_SAFE LTLOG("fenter", "%s", __FUNCTION__)
    #define LTLOG_FEXIT()                       LT_ISR_SAFE LTLOG("fexit",  "%s", __FUNCTION__)
    #define LTLOG_DEBUG(pTag, pFormat, ...)     LT_ISR_SAFE LT_GetCore()->Log(s_pLTLOG_Section,  pTag, kLTCore_LogFlags_LogTypeDebugLog | (kLTCore_LogFlags_LogToConsole), pFormat, ##__VA_ARGS__)
    #define LTLOGV_DEBUG(pTag, pFormat, args)   LT_ISR_SAFE LT_GetCore()->LogV(s_pLTLOG_Section, pTag, kLTCore_LogFlags_LogTypeDebugLog | (kLTCore_LogFlags_LogToConsole), pFormat, args)
    #define LTLOG_VERBOSE(pTag, pFormat, ...)   LT_ISR_SAFE LT_GetCore()->Log(s_pLTLOG_Section,  pTag, kLTCore_LogFlags_LogTypeVerbose  | (kLTCore_LogFlags_LogToConsole), pFormat, ##__VA_ARGS__)
    #define LTLOGV_VERBOSE(pTag, pFormat, args) LT_ISR_SAFE LT_GetCore()->LogV(s_pLTLOG_Section, pTag, kLTCore_LogFlags_LogTypeVerbose  | (kLTCore_LogFlags_LogToConsole), pFormat, args)
#else
    #define LTLOG_FENTER()
    #define LTLOG_FEXIT()
    #define LTLOG_DEBUG(pTag, pFormat, ...)     LT_ISR_SAFE LTLOG_LOGNULL
    #define LTLOGV_DEBUG(pTag, pFormat, args)   LT_ISR_SAFE LTLOG_LOGNULL
    #define LTLOG_VERBOSE(pTag, pFormat, ...)   LT_ISR_SAFE LTLOG_LOGNULL
    #define LTLOGV_VERBOSE(pTag, pFormat, args) LT_ISR_SAFE LTLOG_LOGNULL
#endif

#ifdef LT_NULLIFY_ALL_NON_SERVER_LOGS
    #undef  LTLOG
    #undef  LTLOG_STOMP
    #undef  LTLOGV
    #undef  LTLOGV_STOMP
    #undef  LTLOG_CALLER
    #undef  LTLOG_STOMP_CALLER
    #define LTLOG               LT_ISR_SAFE LTLOG_LOGNULL
    #define LTLOG_STOMP         LT_ISR_SAFE LTLOG_LOGNULL
    #define LTLOGV              LT_ISR_SAFE LTLOG_LOGNULL
    #define LTLOGV_STOMP        LT_ISR_SAFE LTLOG_LOGNULL
    #define LTLOG_CALLER        LT_ISR_SAFE LTLOG_LOGNULL
    #define LTLOG_STOMP_CALLER  LT_ISR_SAFE LTLOG_LOGNULL
#endif

#ifdef LT_NULLIFY_ALL_ASSERTS
    #undef  LT_ASSERT
    #define LT_ASSERT(x)    LT_ISR_SAFE
#endif

LT_EXTERN_C_END

#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTCORE_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Jan-19   augustus    created
 *  22-May-19   augustus    added message queues and signals
 *  17-Jun-19   constantine completed first-round of doxygenation.
 *  18-Jun-19   augustus    added LTMonitor
 *  13-Aug-19   constantine Move doxygenation to a separate file.
 *  18-Aug-19   augustus    replaced class LTMessageQueue with PostThreadMessage(void)
 *  24-Aug-19   augustus    added InsideInterruptContext(void), Disable(), Enable()
 *  11-Oct-19   augustus    added GetDeviceTagName(void)
 *  24-Nov-19   augustus    added math functions and GenRandomBytes(void)
 *  01-Dec-19   augustus    added private string functions for LTString
 *  01-Dec-19   augustus    added vsnprintf and StringVFormatString
 *  07-Jan-20   augustus    added TerminateLT(void)
 *  08-Jan-20   augustus    added logging functions and GetTimeGMTOffset(void)
 *  10-Jan-20   augustus    PostThreadMessage(void) now returns false if thread no longer running
 *  14-Mar-20   augustus    isr safe nested disable/enable
 *  21-Mar-20   augustus    marked ConsolePrint functions with LT_PRINTF_FORMAT_MEMBER_FUNCTION for error checking
 *  06-Apr-20   augustus    LTCore::Logger::LogConsole(void) sends LogFlags 0 to bypass observer notification
 *  28-May-20   augustus    added LTArray create/destroy functions
 *  22-Jun-20   augustus    added spinlock functions
 *  22-Jul-20   augustus    added LTHandle functions
 *  01-Aug-20   augustus    re-optimizated library interface declaration; finished LTHandle
 *  12-Aug-20   augustus    converted all objects to handles; moved stdlib functions into LTStdlib.h
 *  19-Aug-20   augustus    added GetNameCheckedHandleInterface, and interface convenience macros
 *  30-Aug-20   augustus    got rid of LTMonitor and LTSignal
 *  19-Sep-20   augustus    added LTArgs functions
 *  30-Sep-20   caligula    updated LTString create functions
 *  11-Oct-20   augustus    CreateThread() only takes pName
 *  16-Oct-20   augustus    added local time, time zone, and calendar time support
 *  20-Nov-20   augustus    added lt_ishandleinterface and GetHandleInterfaceName
 *  26-Nov-20   augustus    adjusted nomenclature for SetConsoleCharacterReceivedProc
 *  05-Dec-20   augustus    added GetSystemRAMLowWatermark
 *  17-Dec-20   augustus    added ConsolePrintV
 *  23-Dec-20   augustus    added Mutex interface functions here in prep for getting rid of ILTMutex
 *  23-Jan-21   augustus    added SnapshotOpenLibraries and GetLibrarySnapshot
 *  25-Jan-21   augustus    added EnumerateInstalledLibraries
 *  14-Feb-21   augustus    added ConsoleStomp and ConsoleStompV
 *  05-Jun-21   augustus    moved TimeZone, ClockLocal, and ClockTime/CalendarTime conversion functions to LTSystemTimeZone library
 *  06-Jun-21   augustus    enabled DEBUG and RELEASE logging macros; eradicated ILTLogger and LTLogger
 *  22-Jul-21   augustus    got rid of spinlocks
 *  08-Sep-21   augustus    full complement of ConsoleStomp functions
 *  11-Dec-21   tiberius    renamed string and pointer arrays, removed U32 arrays
 *  23-Dec-21   augustus    added GetLibraryBuildVersionString
 *  18-Jan-22   tiberius    added LTList
 *  18-Feb-22   augustus    added pReturnAddr to LTCore_HeapAllocSnapshot
 *  28-Feb-22   augustus    added FormatCanonicalTimeString
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 *  17-May-22   augustus    modified UTC functions to use LTTimeBase
 *  07-Feb-23   augustus    enable asserts in release mode as well as debug mode
 *  31-Jul-23   augustus    added lt_createobject, lt_destroyobject
 *  04-Aug-23   augustus    added lt_consoleprint
 *  27-Oct-23   augustus    added IsLTShuttingDown
 *  26-Jan-24   nerva       added CountResourceChildren, ResourceTypeToString, ResourceValueToString
 *  01-May-24   augustus    added LTCore_RawConsole object
 *  22-May-24   augustus    added bIncludeBrackets to FormatCanonicalTimeString
 *  26-Jun-24   augustus    added low power standby mode functions
 *  13-Nov-24   augustus    added LT_NULLIFY_ALL_NON_SERVER_LOGS and LT_NULLIFY_ALL_ASSERTS
 *  10-Apr-25   augustus    renamed standby mode to sleep mode
 *  10-Apr-25   augustus    added OnSleepAction, NoSleepAction
 *  05-Apr-26   augustus    added GetLTCoreLibraryBuildVersion
 *  27-Apr-26   augustus    added GetNamedMemoryRegion
 */
