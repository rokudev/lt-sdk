/******************************************************************************
 * <lt/core/bsp/LTCoreBSP.h>       BSP interface for LTCore hardware enablement
 *
 * This file defines the LTCoreBSP interface which specifies the implementation
 * requirements for bring-up of LTCore on new hardware platforms.  The interface
 * is only used by the LTCore private implementation; no direct access is provided.
 * As such, this interface is only of interest to BSP implementers.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_BSP_LTCOREBSP_H
#define ROKU_LT_INCLUDE_LT_CORE_BSP_LTCOREBSP_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

/*_______________________
 /  LTCoreBSP typedefs */

/** LTCoreBSP - the BSP interface LTCore uses to access BSP functionality (declared fully below) */
typedef struct LTCoreBSP  LTCoreBSP;

/** LTHostAPI - the hosted interface LTCore uses to access host OS functionality (declared in LTHosted.h) */
typedef struct LTHostAPI  LTHostAPI;

typedef void (LTCoreBSP_LTCoreLogFunction)(const char * pSectionName, const char * pTag, u32 nLogFlags, const char * pFormat, ... ) LT_ISR_SAFE;
typedef void (LTCoreBSP_InterruptHandler)(void);
typedef u32   LTCoreBSP_InterruptPriority;

/*_____________________
 /  LT Configuration */

typedef u32 (LTCoreBSP_RISCV_InterruptDispatcher)(u32 mcause) LT_ISR_SAFE;
   /**< RISC-V Interrupt dispatch function */

typedef u64 (LTCoreBSP_RISCV_GetCycleCount)(void) LT_ISR_SAFE;
   /**< RISC-V Get high resolution timer count */

typedef u8 LTCoreBSP_RISCV_VectorMode;
enum LTCoreBSP_RISCV_VectorMode {
    kLTCoreBSP_RISCV_VectorMode_CLICNotPresent  = 0,  /**< Core Local Interrupt Controller (CLIC) not present */
    kLTCoreBSP_RISCV_VectorMode_CLICPresent     = 2   /**< CLIC present */
};

/** RISC-V architecture configuration */
typedef struct {
    LTCoreBSP_RISCV_InterruptDispatcher * pDispatcher;    /**< Interrupt dispatch function */
    LTCoreBSP_RISCV_GetCycleCount       * pGetCycleCount; /**< Get cycle count function */
    void                                * pStackTop;      /**< Initial address of system stack (grows down) */
    u32                                   nClockSpeedHz;  /**< Reference Clock speed (Hz) */
    LTCoreBSP_RISCV_VectorMode            vectorMode;     /**< Interrupt vector mode */
} LTCoreBSP_RISCV_SystemConfig;

typedef struct {
    u32    nClockSpeedHz;       /**< CPU Clock speed (Hz) */
} LTCoreBSP_Xtensa_SystemConfig;

typedef struct {
    u32    nClockSpeedHz;        /**< Reference Clock speed (Hz), either CPU or reference clock */
    void * pSecurityContainer;   /**< Address of security container, set to NULL if not present */
    bool   bUseExternalClockRef; /**< True when using external clock reference, false for CPU */
} LTCoreBSP_ArmCortexM_SystemConfig;

typedef u32 (LTCoreBSP_ArmV5_InterruptDispatcher)(void) LT_ISR_SAFE;
   /**< ArmV5 Interrupt dispatch function */

typedef u64 (LTCoreBSP_ArmV5_GetCycleCount)(void) LT_ISR_SAFE;
   /**< ArmV5 Get high resolution timer count */

typedef struct {
    LTCoreBSP_ArmV5_InterruptDispatcher * pDispatcher;    /**< Interrupt dispatch function */
    LTCoreBSP_ArmV5_GetCycleCount       * pGetCycleCount; /**< Get cycle count function */
    u32                                   nClockSpeedHz;  /**< Reference Clock speed (Hz) */
} LTCoreBSP_ArmV5_SystemConfig;

/** LTCoreBSP_HeapRegion -- Heap region definition for non-hosted BSPs.
 *
 *  Regions are registered in array order; LTKHeapAddRegionEx returns the new
 *  region’s internal slot (0-based, matching pRegions[] position).  Public callers
 *  identify regions via the LTMemoryRegion type, which is *1-based*: LTMemoryRegion N
 *  refers to pRegions[N - 1].  The value 0 is reserved as a sentinel meaning "no
 *  specific region" and routes lt_malloc_from_region back to lt_malloc.  Names are
 *  bound to LTMemoryRegion values via the /memory/regions array in LTDeviceConfig.json
 *  (see LTCore->GetNamedMemoryRegion). */
typedef struct LTCoreBSP_HeapRegion {
    u8   * pRegionBuffer;      /**< Address of heap region */
    u32    nSizeInBytes;       /**< Size of heap region in bytes */
    bool   bExclusive;         /**< If true, the region is skipped by default lt_malloc()/LTKAlloc()
                                *   and reachable only via lt_malloc_from_region(region, ...).  Use for
                                *   DMA-only RAM that must not absorb general-purpose traffic.  Defaults
                                *   to false in zero-initialised struct literals. */
} LTCoreBSP_HeapRegion;
/** LTCoreBSP_LTHeapConfig -- LT heap configuration for non-hosted BSPs */
typedef struct LTCoreBSP_LTHeapConfig {
    u8                       nRegions;    /**< Number of non-contiguous heap regions */
    LTCoreBSP_HeapRegion   * pRegions;    /**< Pointer to array of heap region definitions */
} LTCoreBSP_LTHeapConfig;

/*___________________________________________________________________________________________
  Callback function types for LTCoreBSP functions that have a callback function parameter */
typedef bool (LTCoreBSP_LibraryEnumProc)(const char * pLTLibraryName, void * pClientData); /**< Enumeration callback type for LTCoreBSP_LibraryEnumerate */

/** LTCoreBSP_LTCoreCallbacks - callback functions for LTCoreBSP to call back into LTCore */
typedef struct LTCoreBSP_LTCoreCallbacks {

    /* Interrupt support */
    bool (*InsideInterruptContext)(void) LT_ISR_SAFE;
        /**< determines if executing in ISR context, same function as InsideInterruptContext() in LT Core. */

    bool (*InterruptsAreDisabled)(void) LT_ISR_SAFE;
        /**< determines if interrupts are disabled, same function as InterruptsAreDisabled() in LT Core. */

    LT_SIZE (*Disable)(void) LT_ISR_SAFE;
        /**< disables interrupts, same function as Disable() in LT Core. */

    void (*Enable)(LT_SIZE nDisableMask) LT_ISR_SAFE;
        /**< enables interrupts, same function as Enable() in LT Core. */

    void (*SetInterruptVector)(u32 nInterrupt, LTCoreBSP_InterruptHandler * pInterruptHandler, LTCoreBSP_InterruptPriority priority);
        /**< Register interrupt vector, same function as SetInterruptVector() in LT Core. */

    void (*SetInterruptPriority)(u32 nInterrupt, LTCoreBSP_InterruptPriority priority);
        /**< Change interrupt vector @p nInterrupt to priority (@p priority).
         * Note that some architectures do not support changing interrupt priority dynamically,
         * and in these situations this call will be ignored.
         * @see SetInterruptVector
         */

    void (*ProcessISRConsoleInputChars)(const char * pChars, u32 nChars) LT_ISR_SAFE;
        /**< give LTCore console input characters to process (as received from ISR)
         *
         * Whenever the ISR fires for console character(s) received, it should call
         * ProcessISRConsoleInputChars as many times as necessary during that
         * interrupt cycle to notify LTCore of the characters received.  When it is finished notifying
         * all characters, it should call one more time with NULL and 0 for pChars and nChars to
         * signal that it is complete.
         *
         * With a UART that only receives one character per interrupt cycle, the ISR would look something like this:<pre>
         * static void MYSerialConsoleReceiveISR(void) {
         *     char c = INPUT_CHAR_REGISTER & 0xFF; / * in this example the char is retrieved from the low 8 bits of INPUT_CHAR_REGISTER * /
         *     s_pLTCoreCallbacks->ProcessISRConsoleInputChars(&c, 1);
         *     s_pLTCoreCallbacks->ProcessISRConsoleInputChars(NULL, 0);
         * }
         * </pre>
         * @note When the BSP is initialized, the ISR receive interrupt should be disabled and only enabled
         *       when LTCoreBSP->EnableIncomingSerialConsoleInputCharacterInterrupt(true) is called.  If the
         *       interrupt cannot be disabled individually, then the ISR should use its own private flag to
         *       prevent invocation of ProcessIncomingSerialConsoleInputCharactersFromISR until
         *       LTCoreBSP->EnableIncomingSerialConsoleInputCharacterInterrupt(true) is called.
         */

    LTCoreBSP_LTCoreLogFunction * LTCoreLogFunction;
     /**< function that enables the logging macros; do not call directly */

    void (*TerminateLT)(int nExitCode);
     /**< initiates graceful shutdown of LT from BSP, e.g. from within atexit() registered exit function
      *
      *   @param nExitCode the exit code that will be returned from LT_Run().  When in doubt pass in 0
      */

    bool (*IsDeveloperBuild)(void);
     /**< returns true if the firmware is a developer build, false if a production build */

    #ifndef LT_NO_DYNAMIC_LOADER
        void (* ReportLibraryLoaderFunctionFailure)(const char * pFunctionName, const char * pSalientArgument, const char * pError);
        /**< call this function to report a native function call failure during library load
         *
         *  @note this function should be called immediately prior to returning with an error code from LTCoreBSP_LoadLibrary or from LTCoreBSP_LookupLibrarySymbol.<br>
         *  Example invocation: <pre>
         *      pCoreCallbacks->ReportFunctionFailure("dlopen", "libLTMesh.so", "File not found.");
         *  LTCore will format and print this to the console as:
         *      ltcorebsp: dlopen(libLTMesh.so) failed.  File not found.
         *  </pre>
         */
    #endif
} LTCoreBSP_LTCoreCallbacks;

/*___________________________________________________________________________
 / LTCore Board Support Package - Functions for LT bringup on new hardware */
struct LTCoreBSP { /**< interface into the BSP */

  /*___________________________________
    high frequency counter functions */
    s64 (* GetHighFrequencyCounterNanoseconds)(void) LT_ISR_SAFE;
        /**< return elapsed time in nanoseconds since system boot */

    s64 (* GetHighFrequencyCounterNanosecondResolution)(void) LT_ISR_SAFE;
        /**< return the precision of the high frequency counter in nanoseconds */

  /*_________________
    Serial console */
    void (* EnableSerialConsole)(bool bEnable);
        /**< enables or disables the serial console
         *
         * @param bEnable true to enable the serial console, false to disable it
         */

    void (* PutCharsToConsole)(const char * pChars, u32 numChars) LT_ISR_SAFE;
        /**< put characters to the serial console uart as fast as possible
         *
         * PutCharsToConsole() is called by LTCore to instruct the BSP to output characters
         * to the serial console.  It should return when all characters have been submitted to
         * the hardware.  If the hardware has a fifo character output buffer, this function
         * should repeatedly fill and wait, fill and  wait (as necessary), and fill
         * and return without waiting for the fifo to drain on the final batch of characters.
         * In other words, the function should return as soon as all of the characters have
         * been submitted.
         *
         * @param pChars a pointer to the characters to put to the console
         * @param nunmChars the number of characters to put to the console
         *
         * @note This function must operate both when interrupts are enabled and when they are disabled.
         *       Therefore, if a fifo output buffer is employed, it must be operated in polled (busy wait)
         *       mode when interrupts are disabled.  Even with interrupts enabled, there is no advantage
         *       to operating such a fifo transmit buffer in interrupt driven fifo-ready mode because LT already
         *       buffers the output and calls PutCharsToConsole from a low priority thread.
         *
         */

  /*______________________
    logging redirection */
    void     (* RedirectLogV)(const char * pSectionName, const char * pTag, u32 nLogFlags, const char * pFormat, lt_va_list args) LT_ISR_SAFE;
        /**< Normally NULL, if non-NULL log messages will be rdirected to this function
         *
         * This function member should normally be set to NULL.
         * If this function is non-NULL *and* LT is started in host mode (argv[0] to LTRun() is "lthost"),
         * LTCore will redirect all log messages here to this function in the bsp instead of instantiating and using
         * the default LT logging subsystem.
         *
         * On RokuOS the implementation of this function translates the logFlags to RokuOS log flags and the invokes the
         * RokuOS equivalent of this function, passing it the translated log flags and the rest of the arguments to this function.
         * All other platform BSPs should set this variable to NULL.
         *
         * @note BSP_LTLOG macros are provided below to aid in platform bringup and debugging.  Any logs
         *       made with these BSP_LTLOG macros will also get redirected to this function.
         */

    /* debugging */
    bool     (* DebugAssertFailed)(const char * pFile, int nLine, const char * pTest) LT_ISR_SAFE;
    /**< if this function returns true then the LT_ASSERT() macro will invoke LTCoreBSP_DebugBreak() */

    /* LT Configuration
     *   For Hosted BSPs these fields should be omitted, i.e.: NULL.  Note that data pointers are forbidden in LT library interfaces,
     *   however they are permitted in LTCoreBSP since it is an internal LTCore interface. */
    const void                      * pLTSystemConfig; /**< pointer to LT system configuration */
    const LTCoreBSP_LTHeapConfig    * pLTHeapConfig;   /**< pointer to LT heap configuration */
    const LTHostAPI                 * hostAPI;         /**< pointer to LT Host API (if any, use NULL for Native LT operation) */

    void (* PostInitialize)(void);
        /**< optional post-initialization hook called by LTCore after the kernel, heap, and
         *   threading are fully initialized and running in an LTThread context, but before
         *   any libraries are opened.  Set to NULL if not needed. */
};

    /** Additional BSP functions outside the API struct which also must be written by BSP Implementers: */
    const LTCoreBSP * LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks);
        /**< perform bsp initialization
         *
         * LTCoreBSP_Initialize will be called by LTCore.  LTCoreBSP_Initialize should cache the pCallbacks pointer and provide all required BSP initialization.
         * Upon returning, all facilities of the BSP should be operational.
         * @param pCallbacks a pointer to the LTCore_BSPCallbacks structure provided by LTCore to LTCoreBSP
         * @return a pointer to the LTCoreBSP interface struct on first invocation, NULL on all subsequent invocations until LTCoreBSP_Finalize is called
         * @note this function should only return the BSP interface struct on first call and then return NULL on subsequent invications until LTCoreBSP_Finalize
         *       is called with the same interface struct pointer "returned" to the LTCoreBSP.  Thereafter, then next call to LTCoreBSP_Initialize should initialize
         *       and return a valid LTCoreBSP interface struct pointer.
         */

    void LTCoreBSP_Finalize(const LTCoreBSP * pBSP);
        /**< called by LTCore for LTCoreBSP to fully uninitialize.  No BSP facilities will be called after this function has been invoked by LTCore
         *
         * @param pBSP the LTCoreBSP interface pointer returned from LTCoreBSP_Initialize. The BSP should only Finalize once after Initialize and only if pBSP is the same pointer as
         *        returned from LTCoreBSP_Initialize
         */

    /* all functions below this line are only called in between calls to LTCoreBSP_Initialize and LTCoreBSP_Finalize.
       Any and all LTCoreBSP static linkage is completely exclusive with LTCore.  */
    bool     LTCoreBSP_InsideInterruptContext(void) LT_ISR_SAFE;       /**< returns true if currently running in an interrupt handler, false otherwise */
    bool     LTCoreBSP_InterruptsAreDisabled(void) LT_ISR_SAFE;        /**< returns true if interrupts are currently disabled */
    LT_SIZE  LTCoreBSP_DisableInterrupts(void) LT_ISR_SAFE;            /**< disable interrupts, returning the current disable mask prior to disabling interrupts */
    void     LTCoreBSP_EnableInterrupts(LT_SIZE nMask) LT_ISR_SAFE;    /**< enable interrupts with the mask passed in */
    void     LTCoreBSP_DebugBreak(void);                               /**< enter debugger if available, otherwise force exception with data dump to console */

/*_______________________________________
 / Utility macros for BSP Implementers */
#define LTCoreBSP_NanosecondsToMicroseconds(nanos)         ((s64)(((s64)(nanos))  / ((s64)(LT_CONSTS64(1000)))))
#define LTCoreBSP_NanosecondsToMilliseconds(nanos)         ((s64)(((s64)(nanos))  / ((s64)(LT_CONSTS64(1000000)))))
#define LTCoreBSP_NanosecondsToSeconds(nanos)              ((s64)(((s64)(nanos))  / ((s64)(LT_CONSTS64(1000000000)))))
#define LTCoreBSP_SecondsToNanoseconds(secs)               ((s64)(((s64)(secs))   * ((s64)(LT_CONSTS64(1000000000)))))
#define LTCoreBSP_MillisecondsToNanoseconds(millis)        ((s64)(((s64)(millis)) * ((s64)(LT_CONSTS64(1000000)))))
#define LTCoreBSP_MicrosecondsToNanoseconds(micros)        ((s64)(((s64)(micros)) * ((s64)(LT_CONSTS64(1000)))))
#define LTCoreBSP_NanosecondsValueInfinite                 LT_S64_MAX
#define LTCoreBSP_NanosecondsIsInfinite(nanos)             ((LTCoreBSP_NanosecondsValueInfinite == ((s64)(nanos))) ? true : false)

/*_________________________
 / Log defs for BSP Only  /
/ The following logging macros may be used in the BSP implementation
  for debug purposes.  When BSP debugging is complete, #define NULLIFY_BSP_LTLOGS
  in the bsp c file(s) that use logging macros before they #include <lt/core/bsp/LTCoreBSP.h>
  and they all will be elided.

  IMPORTANT: The BSP should not send any characters to the serial port, except when
  instructed by LTCore.  i.e. Do not implement and/or call your own printf or underlying
  vendor SDK printfs in the BSP.

  Macros that log in both debug and release modes
    BSP_LTLOG(tag, format, ...)                - use to perform a regular log
    BSP_LTLOG_YELLOWALERT(tag, format, ...)    - use to log anomalous situations that are recoverable
    BSP_LTLOG_REDALERT(tag, format, ...)       - use to log anomalous situations that are fatal

  Macros that log when LT_BUILD_MODE=debug only
    BSP_LTLOG_DEBUG(tag, format, ...)          - use to perform a regular log in debug mode only
    BSP_LTLOG_FENTER()                         - put at the beginning of a function to log function entry
    BSP_LTLOG_FEXIT()                          - put before return points of a function to log function exit

    To use the logging macros in your bsp file(s) put:
       DEFINE_BSP_LTLOG_SECTION("bsp_section_name")
    where bsp_section_name is the section that will be prepended to log tags
    REMEMBER: When finished debugging make sure to
      #define NULLIFY_BSP_LTLOGS                       / * before including <lt/core/bsp/LTCoreBSP.h> * /
      #include <lt/core/bsp/LTCoreBSP.h>
    in all bsp .c file(s) so that *all* BSP LOGS are elided from the build.
*/

#ifndef DOXY_SKIP
typedef  u32 LTCoreBSP_LogFlags;
#endif
/* Note: Do NOT modify LTCoreBSP_LogFlags; they are a subset of LTCore_LogFlags and must match */
typedef enum LTCoreBSP_LogFlags {
    kLTCoreBSP_LogFlags_LogTypeRaw             = 0,        ///< Raw log message without preamble; used for ConsolePrint
    kLTCoreBSP_LogFlags_LogTypeVerbose         = 1,        ///< Verbose log message
    kLTCoreBSP_LogFlags_LogTypeDebugLog        = 2,        ///< Debug log message
    kLTCoreBSP_LogFlags_LogTypeLog             = 3,        ///< Standard log message
    kLTCoreBSP_LogFlags_LogTypeYellowAlert     = 4,        ///< Warning log message
    kLTCoreBSP_LogFlags_LogTypeRedAlert        = 5,        ///< Error log message
    kLTCoreBSP_LogFlags_LogTypeAssert          = 6,        ///< Assertion log message

    kLTCoreBSP_LogFlags_LogTypeMask            = 0x7,      ///< Bitmask for the log type field

    kLTCoreBSP_LogFlags_LogToConsole           = (1 << 4), ///< Send log message to console
    kLTCoreBSP_LogFlags_LogToServer            = (1 << 5), ///< Send log message to server
    kLTCoreBSP_LogFlags_LogFromISR             = (1 << 6), ///< indicates log originated from an ISR
    kLTCoreBSP_LogFlags_ConsoleStomp           = (1 << 7), ///< output immediately, stomping on buffered output in progress
    kLTCoreBSP_LogFlags_NullArgs               = (1 << 9), ///< copy format string directly into log buffer, bypassing snprintf, ignoring varargs
    kLTCoreBSP_LogFlags_Flush                  = (1 << 10), ///< flush any deferred log processing

    kLTCoreBSP_LogFlags_LogEntry64             = (1 << 16),
    kLTCoreBSP_LogFlags_LogEntry128            = (1 << 17)
} LTCoreBSP__LogFlags;

#define DEFINE_BSP_LTLOG_SECTION(section)              static const char *                     s_LTCoreLogSection  = section; \
                                                       static LTCoreBSP_LTCoreLogFunction *    s_LTCoreLogFunction = NULL;
    /* To use the BSP_LTLOG macros in your BSP .c file(s), put
          DEFINE_BSP_LTLOG_SECTION(section_name)
       at file scope after including <lt/core/bsp/LTCoreBSP.h>
       in each of the BSP .c file(s) you wish to use the macros in.

       BSP_LTLOG macros are only for use in debugging the BSP.

       IMPORTANT: When finished debugging, put #define NULLIFY_BSP_LTLOGS
       in each BSP.c file at file scope *before* including <lt/core/bsp/LTCoreBSP.h>
       This will elide (get rid of) all BSP_LTLOG macro generated code while allowing the
       BSP_LTLOG macros to remain in place.
    */

#define BSP_LTLOG_INITIALIZE(ltCoreLogFunction)        s_LTCoreLogFunction = ltCoreLogFunction; LT_UNUSED(s_LTCoreLogSection);
    /* Use BSP_LTLOG_INITALIZE in each of your bsp .c file(s) that use the BSP_LTLOG macros:
       In all .c files that don't contain the function LTCoreBSP_Initialize(), create a
       special logging initialization function, e.g.
          void My_Secondary_C_File_Logging_Initializer(LTCoreBSP_LTCoreLogFunction * ltCoreLogFunction) {
              BSP_LTLOG_INITIALIZE(ltCoreLogFunction);
          }
       In the function  LTCoreBSP_Initialize() use BSP_LTLOG_INITIALIZE directly and call the other
       log intializer functions created in other BSP .c. files, if any:
          const LTCoreBSP * LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {
              BSP_LTLOG_INITIALIZE(pCallbacks->LTCoreLogFunction);                     / * usually only this is required * /
              My_Secondary_C_File_Logging_Initializer(pCallbacks->LTCoreLogFunction);  / * only if another BSP .c file exists where you created such a function * /
              My_Tertiary_C_File_Logging_Initializer(pCallbacks->LTCoreLogFunction);   / * only if another BSP .c file exists where you created such a function * /
              My_Quaternary_C_File_Logging_Initializer(pCallbacks->LTCoreLogFunction); / * only if another BSP .c file exists where you created such a function * /
              ...
              return &s_myBSP;
          }

       BSP_LTLOG macros are enabled (and nothing will log until) after LTCoreBSP_Initialize() returns.

       REMEMBER:
        When finished debugging, #define NULLIFY_BSP_LTLOGS at file scope *before* including <lt/core/bsp/LTCoreBSP.h>
     */

#define BSP_LTLOG_LOGNULL                              LT_ISR_SAFE 1 ?  LT_UNUSED(s_LTCoreLogSection), LT_UNUSED(s_LTCoreLogFunction) : BSPLog_LogNull
        LT_INLINE                                      LT_ISR_SAFE void BSPLog_LogNull(const char * pTag, const char * pFormat, ...) { LT_UNUSED(pTag); LT_UNUSED(pFormat); }
#if defined(ROKU_LT_SOURCE_LT_CORE_LTKERNEL_H)
    #define DEFAULT_BSP_LTK_LOG_SECTION                "ltk"
#else
    #define DEFAULT_BSP_LTK_LOG_SECTION                "bsp"
#endif
#define BSP_LTLOG_SECTION                              (s_LTCoreLogSection && *s_LTCoreLogSection) ? s_LTCoreLogSection : DEFAULT_BSP_LTK_LOG_SECTION

#if (defined(NULLIFY_BSP_LTLOGS) || defined(NULLIFY_LTK_LTLOGS))
    /* if you have log macros in your BSP source file(s) and you want to turn them all off,
       define NULLIFY_BSP_LTLOGS before including LTCoreBSP.h in your BSP source file(s) and
       all logs will be elided. */
    #define BSP_LTLOG(pTag, pFormat, ...)                LT_ISR_SAFE BSP_LTLOG_LOGNULL
    #define BSP_LTLOG_YELLOWALERT(pTag, pFormat, ...)    LT_ISR_SAFE BSP_LTLOG_LOGNULL
    #define BSP_LTLOG_REDALERT(pTag, pFormat, ...)       LT_ISR_SAFE BSP_LTLOG_LOGNULL
    #define BSP_LTLOG_DEBUG(pTag, pFormat, ...)          LT_ISR_SAFE BSP_LTLOG_LOGNULL
    #define BSP_LTLOG_STOMP(pTag, pFormat, ...)          LT_ISR_SAFE BSP_LTLOG_LOGNULL
    #define BSP_LTLOG_STOMP_REDALERT(pTag, pFormat, ...) LT_ISR_SAFE BSP_LTLOG_LOGNULL
    #define BSP_LTLOG_FENTER()
    #define BSP_LTLOG_FEXIT()
#else
    #define BSP_LTLOG(pTag, pFormat, ...)                LT_ISR_SAFE if (s_LTCoreLogFunction) s_LTCoreLogFunction(BSP_LTLOG_SECTION, pTag, kLTCoreBSP_LogFlags_LogTypeLog         | (kLTCoreBSP_LogFlags_LogToConsole), pFormat, ##__VA_ARGS__)
    #define BSP_LTLOG_YELLOWALERT(pTag, pFormat, ...)    LT_ISR_SAFE if (s_LTCoreLogFunction) s_LTCoreLogFunction(BSP_LTLOG_SECTION, pTag, kLTCoreBSP_LogFlags_LogTypeYellowAlert | (kLTCoreBSP_LogFlags_LogToConsole | kLTCoreBSP_LogFlags_LogToServer), pFormat, ##__VA_ARGS__ )
    #define BSP_LTLOG_REDALERT(pTag, pFormat, ...)       LT_ISR_SAFE if (s_LTCoreLogFunction) s_LTCoreLogFunction(BSP_LTLOG_SECTION, pTag, kLTCoreBSP_LogFlags_LogTypeRedAlert    | (kLTCoreBSP_LogFlags_LogToConsole | kLTCoreBSP_LogFlags_LogToServer), pFormat, ##__VA_ARGS__ )
    #define BSP_LTLOG_STOMP(pTag, pFormat, ...)          LT_ISR_SAFE if (s_LTCoreLogFunction) s_LTCoreLogFunction(BSP_LTLOG_SECTION, pTag, kLTCoreBSP_LogFlags_LogTypeLog         | (kLTCoreBSP_LogFlags_LogToConsole | kLTCoreBSP_LogFlags_ConsoleStomp), pFormat, ##__VA_ARGS__)
    #define BSP_LTLOG_STOMP_REDALERT(pTag, pFormat, ...) LT_ISR_SAFE if (s_LTCoreLogFunction) s_LTCoreLogFunction(BSP_LTLOG_SECTION, pTag, kLTCoreBSP_LogFlags_LogTypeRedAlert    | (kLTCoreBSP_LogFlags_LogToConsole | kLTCoreBSP_LogFlags_ConsoleStomp), pFormat, ##__VA_ARGS__)

    #define BSP_LT_CONSOLEPRINT(pFormat, ...)          LT_ISR_SAFE if (s_LTCoreLogFunction) s_LTCoreLogFunction(NULL, NULL, kLTCore_LogFlags_LogTypeRaw | kLTCore_LogFlags_LogToConsole, pFormat, ##__VA_ARGS__)
    #define BSP_LT_CONSOLESTOMP(pFormat, ...)          LT_ISR_SAFE if (s_LTCoreLogFunction) s_LTCoreLogFunction(NULL, NULL, kLTCore_LogFlags_LogTypeRaw | kLTCore_LogFlags_LogToConsole | kLTCore_LogFlags_ConsoleStomp, pFormat, ##__VA_ARGS__)

    #ifdef  LT_DEBUG
        #define BSP_LTLOG_FENTER()                     LT_ISR_SAFE BSP_LTLOG("fenter", "%s", __FUNCTION__)
        #define BSP_LTLOG_FEXIT()                      LT_ISR_SAFE BSP_LTLOG("fexit",  "%s", __FUNCTION__)
        #define BSP_LTLOG_DEBUG(pTag, pFormat, ...)    LT_ISR_SAFE if (s_LTCoreLogFunction) s_LTCoreLogFunction(BSP_LTLOG_SECTION,  pTag, kLTCoreBSP_LogFlags_LogTypeDebugLog | (kLTCoreBSP_LogFlags_LogToConsole), pFormat, ##__VA_ARGS__)
    #else
        #define BSP_LTLOG_FENTER()
        #define BSP_LTLOG_FEXIT()
        #define BSP_LTLOG_DEBUG(pTag, pFormat, ...)    LT_ISR_SAFE BSP_LTLOG_LOGNULL
    #endif
#endif /* #ifdef NULLIFY_BSP_LTLOGS */

#if defined(ROKU_LT_SOURCE_LT_CORE_LTKERNEL_H)
    #define DEFINE_LTK_LTLOG_SECTION    DEFINE_BSP_LTLOG_SECTION
    #define LTK_LTLOG_INITIALIZE        BSP_LTLOG_INITIALIZE

    #define LTK_LTLOG                   BSP_LTLOG
    #define LTK_LTLOG_YELLOWALERT       BSP_LTLOG_YELLOWALERT
    #define LTK_LTLOG_REDALERT          BSP_LTLOG_REDALERT
    #define LTK_LTLOG_DEBUG             BSP_LTLOG_DEBUG
    #define LTK_LTLOG_STOMP             BSP_LTLOG_STOMP
    #define LTK_LTLOG_STOMP_REDALERT    BSP_LTLOG_STOMP_REDALERT
    #define LTK_LTLOG_FENTER            BSP_LTLOG_FENTER
    #define LTK_LTLOG_FEXIT             BSP_LTLOG_FEXIT
    #define LTK_LT_CONSOLEPRINT         BSP_LT_CONSOLEPRINT
    #define LTK_LT_CONSOLESTOMP         BSP_LT_CONSOLESTOMP
#endif

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_BSP_LTCOREBSP_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  21-Jan-21   augustus    re-created from the previous static interface
 *  22-Feb-21   augustus    added ThreadGetStackUsage; made all stack sizes use u32
 *  15-Jun-21   tiberius    remove handle concept from BSPs, pass in void * instead
 *  22-Jun-21   tiberius    thread priority cleanup
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 *  11-Nov-22   augustus    added BSP_LTLOG capability; removed ConsolePrint/ConsoleStomp functions
 */
