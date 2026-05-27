/******************************************************************************
 * lt/source/lt/device/slider/LTDeviceSliderImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTArray.h>
#include <lt/core/LTTime.h>

#include <lt/device/slider/LTDeviceSlider.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("slider.device")

static ILTThread           * s_iThread = NULL;

/* Access to the LTDriverSlider library: */
static LTDriverLibrary     * s_pDriver = NULL;
static ILTDriverSlider     * s_iDriver = NULL;

/* States used in SliderStateMachine */
typedef enum {
    INITIAL,
    FIRST_TOUCH,
    SWIPE,
    LONG_PRESS
} LTSliderState;

/* constants used for SliderStateMachine */
enum {
    kSlider_Interrupt_State_Change,
    kSlider_Timer_State_Change
};

/* The structure that collects the samples, where the bit set to '1' in
 * 'sample' at time 'timestamp' means that the key corresponding to that bit
 * was pressed.
 */
typedef struct {
    LTTime tmTimestamp;
    u32    nSample;
} Sample;

/******************************************************************************
 * Slider detection, direction and intensity types and structures
 */

/* Standard sequence consists of samples furnished by interrupts where each
 * interrupt signals a transition from one set of currently pressed keys to
 * another. The end of the sequence is determined when an interrupt comes where
 * all keys are released. From that sequence of samples we need to detect the
 * direction and the speed of the finger slide.
 */

/* The special case is the one where the sequence lasts for longer than
 * SAMPLE_MAX samples. In that case we force the detection algorithm to start.
 * As we have 8 keys for the only slider currently used, more than 15
 * interrupts can only mean that some keys are pressed multiple times, and the
 * slide movement went in both direction in the sequence. When that happens,
 * we can't make a decision anyway, so we stop gathering samples and report.
 * So, this value is somewhat arbitrary right now and could change as we deal
 * with other slider parts.
 */
#define SAMPLE_MAX 16

typedef struct {
    LTDeviceUnit             hSlider;     /* driver instance handle */
    s32                      lowerBound;
    s32                      upperBound;
    s32                      value;
    u32                      nMaxStep;     /* used for swipes and +/- */
    u32                      keyCount;
    LTMutex                * mutex;      /* parts of this struct require critical sections */
    LTThread                 hCBThread;   /* client's thread for callbacks */
    LTDeviceSliderCallback * pCallback;
    void                   * pClientData;

    LTSliderState            state;
    Sample                   samples[SAMPLE_MAX];
    u32                      sampleTop;

    /* bit0 - enabled(1)/disabled(0)
     * bit1 - periodic(1)/interrupt-driven(0)
     */
    u8                       flags;
} SliderDevice_Instance;

/* This array holds the device handles returned to the clients. An index into
 * the array is the same device number that the client supplied when requesting
 * a handle, and it's also the same device number supplied to the driver when a
 * driver-supplied handle is requested by the device library.
 * The purpose of the array is to detect if a handle is already given for a
 * specific device number. When a client asks for a handle for a device number
 * for which a handle is already given, we return the existing handle.
 * The assumption is that there is only one client (controller), so it makes
 * more sense to tell controller "already owned, here is the handle" rather
 * than "already owned, look up in your own structures for the handle".
 *
 * The array is used only by the application thread, and it is not protected by a critical section in this file.
 */
static LTArray  * s_pDeviceHandles    = NULL;

/* Driver interrupt posts on this thread, and all sample processing happens
 * there before a proc is posted on the client's thread.
 */
static LTThread   s_hEventThread      = 0;

static u32        s_nNumDeviceUnits   = 0;

/******************************************************************************
 * Flags operations
 */
enum {kEnableBit = 0x01, kPeriodicBit = 0x02};

#define ENABLE(pInst)          ((pInst)->flags |=  kEnableBit)
#define DISABLE(pInst)         ((pInst)->flags &= ~kEnableBit)
#define IS_ENABLED(pInst)      ((pInst)->flags &   kEnableBit)
#define SET_PERIODIC(pInst)    ((pInst)->flags |=  kPeriodicBit)
#define SET_INTDRIVEN(pInst)   ((pInst)->flags &= ~kPeriodicBit)
#define IS_PERIODIC(pInst)     ((pInst)->flags &   kPeriodicBit)

/* If there is a long button press possibly followed by a swipe, we want to
 * generate an event for it. A long press at the end of a swipe is treated
 * differently. It is just a break between swipes to allow for the gesture of
 * swiping up and down while observing the changes without releasing a finger.
 * This is the time limit in milliseconds after which we detect a long press.
 */
#define PRESS_LIMIT 500

/******************************************************************************
 * Macros for an easy switch from ConsolePrint to LTLOG and back
 */
#define EMPTYSPACE
#define COMMA ,
/* COUNT_FORWARD returns (12-count(__VA_ARGS__))-th member of the arguments
 * array passed to it. __VA_ARGS__ can't be empty.
 */
#define COUNTER(...) LTTYPES_COUNT_FORWARD(__VA_ARGS__, COMMA, COMMA, COMMA, COMMA, COMMA, COMMA, COMMA, COMMA, COMMA, EMPTYSPACE)

#define REST(a, ...) __VA_ARGS__
#define ADD_NL(...) LTTYPES_EXTRACT_FIRST_ARG(__VA_ARGS__) "\n"
#define ARGS_WITH_NL(...) ADD_NL(__VA_ARGS__) COUNTER(__VA_ARGS__) REST(__VA_ARGS__)

/* For ConsolePrint, we ignore the first argument */
//#define _PRINT(msg1, ...) _PRINT0(__VA_ARGS__)
//#define _PRINT0(...) LT_GetCore()->ConsolePrint(__VA_ARGS__)

/* For LTLOG, it's a straightforward replacement */
//#define _PRINT(...) LTLOG_DEBUG(__VA_ARGS__)

/* Turning off all messages */
#define _PRINT(...)

/******************************************************************************
 *  Help for printing a byte in binary with the LSB first
 */
#define BINARY_FORMAT "%c%c%c%c%c%c%c%c\n"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x01 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x80 ? '1' : '0')

/******************************************************************************
 *  Helper functions for mapping between device unit numbers, corresponding
 *  handles and pointers to SliderDevice_Instances. Not all of them are used
 *  currently.
 */
static SliderDevice_Instance * GetInstanceFromHandle(LTDeviceUnit handle) {
    if (handle) {
        SliderDevice_Instance * pInstance = (SliderDevice_Instance *)(LT_GetCore()->GetHandlePrivateData(handle));
        return pInstance;
    }
    else {
        return NULL;
    }
}

static u32 GetIndexFromHandle(LTDeviceUnit handle) {
    if (handle) {
        LTDeviceUnit tempHandle = 0;
        for (u32 i = 0; i < s_nNumDeviceUnits; i++) {
            s_pDeviceHandles->API->Get(s_pDeviceHandles, i, &tempHandle);
            if (tempHandle == handle) return i;
        }
    }
    return LT_U32_MAX;
}

#if 0
// DRW : GetHandleFromIndex() unreferenced - error on MacOS
static inline LTDeviceUnit GetHandleFromIndex(u32 index) {
    LTDeviceUnit handle = 0;
    if (index < s_nNumDeviceUnits) {
        s_iArray->GetAt(s_pDeviceHandles, index, &handle);
        return handle;
    }
    else {
        return 0;
    }
}
#endif

static LTDeviceUnit GetHandleFromInstance(SliderDevice_Instance * pI) {
    for (u32 i = 0; i < s_nNumDeviceUnits; i++) {
        LTDeviceUnit handle = 0;
        s_pDeviceHandles->API->Get(s_pDeviceHandles, i, &handle);
        if (handle) {
            SliderDevice_Instance * pInstance = (SliderDevice_Instance *)(LT_GetCore()->GetHandlePrivateData(handle));
            if (pI == pInstance) {
                return handle;
            }
        }
    }
    return 0;
}

#if 0
// DRW : GetInstanceFromIndex() unreferenced - error on MacOS
static inline SliderDevice_Instance * GetInstanceFromIndex(u32 index) {
    LTDeviceUnit handle = 0;
    if (index < s_nNumDeviceUnits) {
        s_iArray->GetAt(s_pDeviceHandles, index, &handle);
    }
    if (handle) {
        SliderDevice_Instance * pInstance =
            (SliderDevice_Instance *)(LT_GetCore()->GetHandlePrivateData(handle));
        return pInstance;
    }
    return NULL;
}
#endif
/*****************************************************************************/

static void IntCallback(void * pData);
static void ClientTimerCallback(void * pData);
static void SampleTimerCallback(void * pData);

/* Helper functions that used to be in the public API but they are taken out for
 * now. We may put them back if there is a need for a public function that
 * temporarily stops slider interrupt from being reported.
 */
static s32  Disable(LTDeviceUnit hDevice);
static s32  Enable(LTDeviceUnit hDevice);

/******************************************************************************
 *  Public APIs
 */

/* Sets the upper and the lower bound, and the initial value that must fall
 * within that range.
 * Can be called at any time after the device is allocated, i.e. its handle
 * is not 0. It doesn't enable interrupts or sets callbacks.
 */
static bool LTSliderDeviceUnit_Initialize(LTDeviceUnit hDevice, s32 nMin,
                                          s32 nMax, s32 nInit, u32 nMaxStep) {
    SliderDevice_Instance * pInstance = GetInstanceFromHandle(hDevice);
    if (pInstance) {
        if ((nMin > LT_S32_MIN) && (nMin < nMax) && (nInit <= nMax) && (nInit >= nMin)) {
            pInstance->lowerBound = nMin;
            pInstance->upperBound = nMax;
            pInstance->mutex->API->Lock(pInstance->mutex);
            pInstance->value = nInit;
            pInstance->mutex->API->Unlock(pInstance->mutex);
            /* We already checked that nMax>nMin and nMin>LT_S32_MAX, so we can be
             * sure that (nMax-nMin) by the definition of LT_S32_MAX and
             * LT_S32_MIN can't be larger than LT_U32_MAX, so (nMax-nMin) always
             * fits into nMaxStep.
             */
            if (nMaxStep == 0 || nMaxStep > (u32)(nMax - nMin)) {
                pInstance->nMaxStep = (nMax - nMin) / 4;
            }
            else {
                pInstance->nMaxStep = nMaxStep;
            }
            _PRINT("device.init", "bounds: [%ld, %ld] initial value %ld\n", nMin, nMax, nInit);
            return true;
        }
    }
    return false;
}

/* Sets a new 'value', but only if it's within the range [lowerBound, upperBound]. The purpose of Get and Set functions is to be used by the clients who don't use callbacks.
 * This is the only function that changes 'value' and runs on the client's thread. It needs a mutex or an API that disables driver interrupts.
 */
static bool LTSliderDeviceUnit_SetValue(LTDeviceUnit hDevice, s32 newValue) {
    SliderDevice_Instance * pInstance = GetInstanceFromHandle(hDevice);
    if (pInstance) {
        if ((newValue <= pInstance->upperBound) && (newValue >= pInstance->lowerBound)) {
            pInstance->mutex->API->Lock(pInstance->mutex);
            pInstance->value = newValue;
            pInstance->mutex->API->Unlock(pInstance->mutex);
            return true;
        }
    }
    return false;
}

/* Gets the current 'value'.
 */
static s32 LTSliderDeviceUnit_GetValue(LTDeviceUnit hDevice) {
    SliderDevice_Instance * pInstance = GetInstanceFromHandle(hDevice);
    if (pInstance) {
        pInstance->mutex->API->Lock(pInstance->mutex);
        s32 retval = pInstance->value;
        pInstance->mutex->API->Unlock(pInstance->mutex);
        return retval;
    }
    return LT_S32_MIN;
}

/* Sets the client's callback, which can be either periodic (t!=0) or
 * interrupt-driven (t==0). If there is another periodic or an interrupt-driven
 * callback already set, it will be replaced by the new one.
 * If the callback is periodic, driver interrupts will be coming and changing
 * 'value', but the changes are reported only periodically.
 */
static bool LTSliderDeviceUnit_RegisterForUpdates(LTDeviceUnit hDevice, LTTime tmUpdatePeriod, LTDeviceSliderCallback * pCallback, void * pClientData) {

    SliderDevice_Instance * pInstance = GetInstanceFromHandle(hDevice);
    if (pInstance) {
        LTThread hThread = s_iThread->GetCurrentThread();

        pInstance->pCallback = pCallback;
        pInstance->pClientData = pClientData;
        pInstance->hCBThread = hThread;

        if (LTTime_IsZero(tmUpdatePeriod)) {
            SET_INTDRIVEN(pInstance);
        }
        else {
            s_iThread->SetTimer(hThread, tmUpdatePeriod, (LTThread_TimerProc *)&ClientTimerCallback, NULL, LTHANDLE_TO_VOIDPTR(hDevice));
            SET_PERIODIC(pInstance);
        }
        (void)Enable(hDevice);
        return true;
    }
    return false;
}

/* Deregisters a callback. We don't check if the current thread is the same as
 * the thread which registered the callback. The assumption is that a
 * multithreaded app can unregister from any thread for as long as it holds a
 * valid handle.
 */
static void LTSliderDeviceUnit_UnregisterFromUpdates(LTDeviceUnit hDevice,
                                                     LTDeviceSliderCallback * pCallback) {
    LT_UNUSED(pCallback);
    SliderDevice_Instance * pInstance = GetInstanceFromHandle(hDevice);
    if (pInstance) {
        LT_ASSERT(pCallback == pInstance->pCallback);
        pInstance->hCBThread = 0;
        pInstance->pCallback = NULL;
        pInstance->pClientData = NULL;
        (void)Disable(hDevice);
        s_iThread->KillTimer(pInstance->hCBThread,
                             (LTThread_TimerProc *)&ClientTimerCallback,
                             LTHANDLE_TO_VOIDPTR(hDevice));
    }
}

/******************************************************************************/

/* For now, the functions Disable and Enable are not supposed to be called by
 * clients. Call to RegisterForUpdates and UnregisterFromUpdates will call
 * Disable/Enable to set the flags. These functions are still kept separate in
 * case we decide later to have them exposed.
 */

/* Unregisters the driver's interrupts, which will cause 'value' to be
 * frozen. If there are any client callbacks set, they will not be invoked.
 * Returns the last 'value' before the driver's interrupts are disabled.
 */
static s32 Disable(LTDeviceUnit hDevice) {
    SliderDevice_Instance * pInstance = GetInstanceFromHandle(hDevice);
    if (pInstance) {
        s_iDriver->SetISRDispatchProc(pInstance->hSlider, NULL, NULL);
        /* The flag set by DISABLE macro will prevent calls to the client's
         * callback even if that callback is not NULL.
         */
        DISABLE(pInstance);
        return (pInstance->value);
    }
    return LT_S32_MIN;
}

/* Registers for the driver's interrupts, which will cause 'value' to be
 * updated. Returns 'value' that was frozen at the time where Disable was
 * called.
 */
static s32 Enable(LTDeviceUnit hDevice) {
    SliderDevice_Instance * pInstance = GetInstanceFromHandle(hDevice);
    if (pInstance) {
        s32 previouslyFrozenValue = pInstance->value;
        s_iDriver->SetISRDispatchProc(pInstance->hSlider, &IntCallback, pInstance);
        ENABLE(pInstance);
        return (previouslyFrozenValue);
    }
    return LT_S32_MIN;
}

static void DispatchClientCB(void * pClientData);

static void HandleSwipe(SliderDevice_Instance * pInstance, s32 pctChange) {
    if (pctChange == LT_S32_MAX) return;
    pInstance->mutex->API->Lock(pInstance->mutex);
    float scaledChange = ((float)pInstance->nMaxStep * pctChange) / 100;
    _PRINT("handle.swipe.pct", "pct change %ld\n", LT_Ps32(pctChange));
    _PRINT("handle.swipe.scaled", "scaled change %lf\n", scaledChange);
    if (pInstance->value + scaledChange >= pInstance->upperBound) {
        pInstance->value = pInstance->upperBound;
    }
    else if (pInstance->value + scaledChange <= pInstance->lowerBound) {
        pInstance->value = pInstance->lowerBound;
    }
    else {
        pInstance->value = (s32)(pInstance->value + scaledChange);
    }

    /* A new event is processed, a client with event-driven callbacks now
     * receives an update.
     */
    if (!IS_PERIODIC(pInstance)) {
        _PRINT("handle.swipe.queue", "queues ClientCB for %lx\n", LT_Pu32(pInstance));
        s_iThread->QueueTaskProc(pInstance->hCBThread,
                                 (LTThread_TaskProc *)&DispatchClientCB, NULL,
                                 pInstance);
    }
    pInstance->mutex->API->Unlock(pInstance->mutex);
}

/* Returns the absolute value of the pressed keys when the specified range
 * [lowerBound, upperBound] is represented by all available slider's keys,
 * where the lowest key represents 'lowerBound', the highest key represents
 * 'upperBound', and the rest of the keys are evenly spread over the range. If
 * two or more keys are pressed, the average is used. The driver may decide to
 * filter the result.
 *
 * Returns LT_S32_MIN if there is an error.
 */
s32 GetEquivalentValue(SliderDevice_Instance * pInstance, u32 bitMask) {
    u32 highestAllowedBit = 1 << (pInstance->keyCount - 1);
    if (~(highestAllowedBit | (highestAllowedBit - 1)) & bitMask) {
        /* Reported bitMask is a longer than the number of keys on the unit */
        return LT_S32_MIN;
    }

    if (bitMask == 1) return pInstance->lowerBound;
    if (bitMask == highestAllowedBit) return pInstance->upperBound;

    float step = (float)(pInstance->upperBound - pInstance->lowerBound)
                 / (pInstance->keyCount - 1);

    /* This is the mask that's being moved over the bitMask, and for each bit
     * found in bitMask, we add a certain value to the calculation of the
     * equivalent value. We start from the second from the lsb, because lsb
     * is a special case managed before the while loop.
     */
    u32 mask = 2U;
    float totalAddition = 0;
    /* The count of bits that contributed to the equivalent value, so we can
     * divide the total sum over that number.
     */
    u32 nSetBitCounter = 0;
    float incr = step;

    /* We start from the lowest bit, and also include the highest bit for the
     * cases when they are used together with others. They contribute by
     * averaging down (for the lsb) and up (for the msb). So we can press two
     * highest and get an equivalent value lower than the maximum, which we
     * would get if only the highest key was the only one pressed.
     */
    if (bitMask & 1U) {
        totalAddition = step / 2;
        nSetBitCounter = 1;
    }
    while (mask != 1U << (pInstance->keyCount - 1)) {
        if (mask & bitMask) {
            totalAddition += incr;
            nSetBitCounter++;
        }
        mask <<= 1;
        incr += step;
    }
    if (bitMask & (1 << (pInstance->keyCount - 1))) {
        totalAddition += (float)(pInstance->upperBound - pInstance->lowerBound - step / 2);
        nSetBitCounter++;
    }
    _PRINT("equivalent.addition", "total addition is %f\n", totalAddition);
    _PRINT("equivalent.bitcounter", "bit counter is %ld\n", nSetBitCounter);
    float newValue = (float)pInstance->lowerBound + totalAddition / nSetBitCounter;
    return (s32)newValue;
}

/* In case of a long press, the state is set to the absolute value that
 * corresponds to the position on the slider when overlaid over the range
 * defined by the upper and the lower bound.
 */
static void HandlePress(SliderDevice_Instance * pInstance, u32 bitMask) {
    pInstance->mutex->API->Lock(pInstance->mutex);
    s32 newValue = GetEquivalentValue(pInstance, bitMask);
    if (newValue != LT_S32_MIN) {
        pInstance->value = newValue;
    }
    else {
        LTLOG_YELLOWALERT("handle.press", "bitMask invalid %lx\n", LT_Pu32(bitMask));
    }
    /* A new event is processed, a client with event-driven callbacks now
     * receives an update.
     */
    if (!IS_PERIODIC(pInstance)) {
        _PRINT("handle.press.queue", "queues ClientCB for %lx\n", LT_Pu32(pInstance));
        s_iThread->QueueTaskProc(pInstance->hCBThread,
                                 (LTThread_TaskProc *)&DispatchClientCB, NULL,
                                 pInstance);
    }
    pInstance->mutex->API->Unlock(pInstance->mutex);
}

/* The touch event is treated as a '+' or a '-' minus key. The value is
 * increased/decreased by the set step, depending if the touch is above or
 * below the current value when the slider is overlaid over the range defined
 * by the upper and the lower bound.
 */
static void HandleTouch(SliderDevice_Instance * pInstance, u32 bitMask) {
    pInstance->mutex->API->Lock(pInstance->mutex);
    s32 equivalentValue = GetEquivalentValue(pInstance, bitMask);
    if (equivalentValue == LT_S32_MIN) {
        LTLOG_YELLOWALERT("handle.touch.mask", "bitMask invalid %lx\n", LT_Pu32(bitMask));
        return;
    }
    _PRINT("handle.touch.equivalent", "equivalent value of the pressed button %ld\n",
           LT_Ps32(equivalentValue));
    _PRINT("handle.touch.maxstep", "step %ld\n", LT_Pu32(pInstance->nMaxStep));
    s32 oldValueForDebug = pInstance->value;
    LT_UNUSED(oldValueForDebug);
    if (equivalentValue > pInstance->value) {
        pInstance->value += pInstance->nMaxStep;
        if (pInstance->value > equivalentValue) {
            /* We don't want to overshoot the value where the user is pressing.
             * Similar to how clicking on a vertical scrollbar moves the thumb
             * up or down but never overshoots the location of the pointer.
             * Also, this takes care of the lower and upper bound because
             * GetEquivalentValue will not return a value out of the bounds.
             */
            pInstance->value = equivalentValue;
        }
        _PRINT("handle.touch.change.up", "touch going up for %ld to %ld\n",
               LT_Ps32(pInstance->value - oldValueForDebug),
               LT_Ps32(pInstance->value));
    }
    else if (equivalentValue < pInstance->value) {
        pInstance->value -= pInstance->nMaxStep;
        if (pInstance->value < equivalentValue) {
            pInstance->value = equivalentValue;
        }
        _PRINT("handle.touch.change.down", "touch going down for %ld to %ld\n",
               LT_Ps32(oldValueForDebug - pInstance->value),
               LT_Ps32(pInstance->value));
    }
    /* A new event is processed, a client with event-driven callbacks now
     * receives an update.
     */
    if (!IS_PERIODIC(pInstance)) {
        _PRINT("handle.touch.queue", "queues ClientCB for %lx\n", LT_Pu32(pInstance));
        s_iThread->QueueTaskProc(pInstance->hCBThread,
                                 (LTThread_TaskProc *)&DispatchClientCB, NULL,
                                 pInstance);
    }
    pInstance->mutex->API->Unlock(pInstance->mutex);
}

/* The function returns -1 if the move is downward considering key IDs (KEY7,
 * KEY6, etc). It returns 1 when the move is upward, 0 when the values are the
 * same or one of the values is 0, so we can't tell anything about direction.
 * If keys are pressed so that there are both upward and downward key presses
 * and releases, the function generates 0xF, which is a flag that the whole
 * sequence should be disregarded.
 */
static s8 GetDirection(u32 oldSample, u32 newSample, u32 nKeys) {
    _PRINT("get.direction", "    comparing old 0x%lx and new 0x%lx\n",
           LT_Pu32(oldSample), LT_Pu32(newSample));
    s8 direction[2] = {0};
    /* if one of them is 0, or they don't differ, we return 0, which means
     * the direction can't be determined but it's not inconsistent.
     */
    if (oldSample * newSample == 0 || (oldSample ^ newSample) == 0) return 0;

    u32 common = oldSample | newSample;
    u32 mask = 1U << (nKeys - 1);
    while ((common & mask) == 0U) mask = mask >> 1U;
    if ((oldSample & mask) > (newSample & mask)) direction[1] = -1;
    else if ((oldSample & mask) < (newSample & mask)) direction[1] = 1;
    else direction[1] = 0; /* both have 1 for this bit */

    mask = 1U;
    while ((common & mask) == 0U) mask = mask << 1U;
    if ((oldSample & mask) > (newSample & mask)) direction[0] = 1;
    else if ((oldSample & mask) < (newSample & mask)) direction[0] = -1;
    else direction[0] = 0; /* both have 1 for this bit */

    if (direction[1] * direction[0] == -1) return 0xF;  /* inconsistency */
    if (direction[1] * direction[0] == 1) return direction[0];
    return (direction[1] + direction[0]); /* one must be 0 */
}

#define USE_GENREPORT_ORIGINAL 0
// DRW : GenReport() unreferenced - error on MacOS; use USE_GENREPORT_ORIGINAL to conditionally compile onlu 1 version

#if USE_GENREPORT_ORIGINAL
#define GenReportActive GenReport

/* This function goes through samples, detects the direction of the transition,
 * and determines if all transitions are consistent. If they are, a number
 * between -100 and 100 is returned. The returned value depends on direction,
 * the speed and the length of the finger slide.
 */
static inline s32 GenReport(SliderDevice_Instance * pInstance) {
    /* For each key, we keep a timestamp of the last time it was pressed.
     * If it's 0, it wasn't pressed yet, or it was already both pressed and
     * released. We catch this second case with noRepeats.
     */
    /* This has to be array of s64 of getKeys elements */
    LTArray * pTimes = LTArray_CreateStructArray(sizeof(LTTime));
    LTTime zeroTimestamp = LTTime_Zero();
    u32 nKeys = pInstance->keyCount;
    for (u32 i = 0; i < nKeys; i++) {
        pTimes->API->Append(pTimes, &zeroTimestamp);
    }

    u32 nSampleCount = 0;
    u32 currentState = 0;  /* sample being processed reflects a transition FROM
                            * this state.
                            */
    u32 mask;
    bool repeatFlag = false;
    u32  repeatMask = 0;

    _PRINT("gen.report.header", "\nMovement Detection\n");
    s8 direction = 0;
    s8 nd;
    while (nSampleCount <= pInstance->sampleTop) {
        /* We also use currentState as a bitmask of the changes that occurred
         * in this transition. After one pass of the loop, we'll save the
         * current sample in it for the next pass.
         */
        nd = GetDirection(currentState, pInstance->samples[nSampleCount].nSample, nKeys);
        _PRINT("gen.report", "    %ld. GetDirection returned %d\n", LT_Pu32(nSampleCount), nd);
        if (direction != 0xF) {
            /* this is either the first direction decision or we detected an
             * inconsistency
             */
            if (direction == 0 || nd == 0xF) direction = nd;
            else if (nd * direction == -1) {
                /* inconsistent movement, set direction to 0xF and don't
                 * change it anymore.
                 */
                direction = 0xF;
            }
        }

        /* Now, find the timestamp when a key was pressed or released.
         * Only bits that changed between samples are needed.
         */
        currentState = currentState ^ pInstance->samples[nSampleCount].nSample;
        mask = 1U;
        for (u32 bitIndex = 0; bitIndex < nKeys; bitIndex++) {
            if (currentState & mask) {
                /* The condition is true for 0->1 and 1->0.
                 * If pTimes for a bit was 0, and the bit was just set to 1,
                 * we record the timestamp.
                 */
                LTTime lastTimestampForKey;
                pTimes->API->Get(pTimes, bitIndex, &lastTimestampForKey);
                if (LTTime_IsZero(lastTimestampForKey)) {
                    /* change from 0->1 */
                    if (repeatMask & mask) {
                        /* We went 0->1 before, so this is an ambiuguous swipe */
                        repeatFlag = true;
                        break;
                    }
                    else {
                        repeatMask |= mask;
                    }
                    pTimes->API->Set(pTimes, bitIndex, &(pInstance->samples[nSampleCount].tmTimestamp));
                }
                else { /* bit switched to 0. report the interval,
                        * but adjust each time for the first timestamp because
                        * only relative times are important
                        */
                    _PRINT("gen.report.press", "Button %ld pressed from %lld for %lld\n",
                            LT_Pu32(bitIndex),
                            LT_Ps64(LTTime_GetMilliseconds(lastTimestampForKey) - LTTime_GetMilliseconds(pInstance->samples[0].tmTimestamp)),
                            LT_Ps64(LTTime_GetMilliseconds(pInstance->samples[nSampleCount].tmTimestamp) -
                            LTTime_GetMilliseconds(lastTimestampForKey)));

                    pTimes->API->Set(pTimes, bitIndex, &zeroTimestamp);
                }
            }
            mask = mask << 1;
        }
        currentState = pInstance->samples[nSampleCount++].nSample;
    }

    nSampleCount = pInstance->sampleTop - 1;
    lt_destroyobject(pTimes);
    _PRINT("gen.report.footer", "\n");

    /* A key was pressed and released multiple times. That indicates opposite
     * directions.
     */
    /* The logic for direction is to take the sample 0, and the sample
     * nSampleTop-1, and find the larger. If the same set of keys is pressed in
     * both samples, there is no definite direction.
     */
    if (repeatFlag || direction == 0 || direction == 0xF) {
        return LT_S32_MAX; // no definite direction
    }

    /* Repurpose mask for the mask of all bits set in the first and final
     * sample. We need it to detect the distance between the first and the last
     * key pressed.
     * __CLZ and __builtin_clz could work on Arm or with GCC if I could do
     * #ifdef.
     */
    mask = pInstance->samples[0].nSample | pInstance-> samples[nSampleCount].nSample;
    u8 highestBit = nKeys - 1;
    u8 highestBitMask = 1 << highestBit;
    while ((mask & highestBitMask) == 0U) {
        highestBit--;
        //mask = mask << 1;
        highestBitMask >>= 1U;
    }
    //mask = mask >> (nKeys - 1 - highestBit);
    u8 lowestBit = 0U;
    u8 lowestBitMask = 1U;
    while ((mask & 1U) == 0U) {
        lowestBit++;
        lowestBitMask <<= 1U;
        //mask = mask >> 1;
    }

    /* The direction is already detected, we need intensity as the speed. */
    _PRINT("gen.report.status", "direction %d time %lld distance %d\n", direction,
           LT_Ps64(LTTime_GetMilliseconds(pInstance->samples[nSampleCount + 1].tmTimestamp) - LTTime_GetMilliseconds(pInstance->samples[0].tmTimestamp)),
           highestBit - lowestBit + 1);
    _PRINT("gen.report.return", "change reported %ld\n",
           LT_Ps32((highestBit - lowestBit + 1) * 100 * direction / (s32)nKeys));

    return((highestBit - lowestBit + 1) * 100 * direction / (s32)nKeys);
}

#else

#define GenReportActive GenReport2

/* An alternative version of getReport. This version doesn't strictly look for
 * inconsistencies. It simply looks at the highest and the lowest bit, and if
 * there is a consistent direction from their press and release event, the
 * swipe is accepted and measured.
 * We still go through the process of finding intermediate directions and
 * printing press and release events for debugging purposes, but we can remove
 * without losing any functionality.
 */
static s32 GenReport2(SliderDevice_Instance * pInstance) {
    /* For each key, we keep a timestamp of the last time it was pressed.
     * If it's 0, it wasn't pressed yet, or it was already both pressed and
     * released. We catch this second case with noRepeats.
     */
    LTArray * pTimes = LTArray_CreateStructArray(sizeof(LTTime));
    LTTime zeroTimestamp = LTTime_Zero();
    u32 nKeys = pInstance->keyCount;
    for (u32 i = 0; i < nKeys; i++) {
        pTimes->API->Append(pTimes, &zeroTimestamp);
    }

    u32 nSampleCount = 0;
    u32 currentState = 0;   /* sample being processed reflects a transition
                             * FROM this state.
                             */
    u32 mask;

    _PRINT("gen.report2.header", "\nMovement Detection\n");
    while (nSampleCount <= pInstance->sampleTop) {
        /* We also use currentState as a bitmask of the changes that occurred
         * in this transition. After one pass of the loop, we'll save the
         * current sample in it for the next pass.
         */
        _PRINT("gen.report2.direction", "    GetDirection returned %d\n",
               GetDirection(currentState, pInstance->samples[nSampleCount].nSample, nKeys));

        /* Now, find the timestamp when a key was pressed or released, so only
         * bits that changed between samples are needed.
         */
        currentState = currentState ^ pInstance->samples[nSampleCount].nSample;
        mask = 1U;
        for (u32 bitIndex = 0; bitIndex < nKeys; bitIndex++) {
            if (currentState & mask) {
                /* If pTimes for a bit is 0, the bit was just set to 1,
                 * and we record the timestamp */
                LTTime lastTimestampForKey;
                pTimes->API->Get(pTimes, bitIndex, &lastTimestampForKey);

                if (LTTime_IsZero(lastTimestampForKey)) {
                    pTimes->API->Set(pTimes, bitIndex, &(pInstance->samples[nSampleCount].tmTimestamp));
                }
                else { /* bit switched to 0. report the interval,
                        * but adjust each time for the first timestamp because
                        * only relative times are important
                        */
                    _PRINT("gen.report2.press", "Button %ld pressed from %lld for %lld\n",
                           LT_Pu32(bitIndex),
                           LT_Ps64(LTTime_GetMilliseconds(lastTimestampForKey) - LTTime_GetMilliseconds(pInstance->samples[0].tmTimestamp)),
                           LT_Ps64(LTTime_GetMilliseconds(pInstance->samples[nSampleCount].tmTimestamp) - LTTime_GetMilliseconds(lastTimestampForKey)));

                    pTimes->API->Set(pTimes, bitIndex, &zeroTimestamp);
                }
            }
            mask = mask << 1;
        }
        currentState = pInstance->samples[nSampleCount++].nSample;
    }

    nSampleCount = pInstance->sampleTop - 1;
    lt_destroyobject(pTimes);

    _PRINT("gen.report2.footer", "\n");

    s8 direction = GetDirection(pInstance->samples[0].nSample,
                                pInstance->samples[nSampleCount].nSample, nKeys);
    if (direction == 0 || direction == 0xF) {
        return LT_S32_MAX; // no definite direction
    }

    /* Looking for the distance between the highest and the lowest set bit.
     * __CLZ and __builtin_clz could work on Arm or with GCC if I could do
     * #ifdef.
     */
    u32 bitsInBoundarySamples =
        pInstance->samples[0].nSample | pInstance->samples[nSampleCount].nSample;
    u8 highestBit = nKeys - 1;
    u8 highestBitMask = 1U << highestBit;
    while ((bitsInBoundarySamples & highestBitMask) == 0) {
        highestBit--;
        bitsInBoundarySamples = bitsInBoundarySamples << 1;
    }
    bitsInBoundarySamples = bitsInBoundarySamples >> (nKeys - 1 - highestBit);
    u8 lowestBit = 0;
    while ((bitsInBoundarySamples & 1U) == 0) {
        lowestBit++;
        bitsInBoundarySamples = bitsInBoundarySamples >> 1;
    }

    /* The direction is already detected, we display the speed but don't use it
     * for now to report to the device library.
     */
    _PRINT("gen.report2.status", "direction %d time %lld distance %d\n", direction,
           LT_Ps64(LTTime_GetMilliseconds(pInstance->samples[nSampleCount + 1].tmTimestamp) - LTTime_GetMilliseconds(pInstance->samples[0].tmTimestamp)),
           highestBit - lowestBit + 1);
    _PRINT("gen.report2.value", "value returned to device %ld\n",
           LT_Ps32((highestBit - lowestBit + 1) * 100 * direction / (s32)nKeys));
    return((highestBit - lowestBit + 1) * 100 * direction / (s32)nKeys);
}
#endif

/******************************************************************************
 *  Sample Timer functions
 */

/* I am not sure if the timers are managed separately by the pClientData, i.e.
 * can we have multiple timers on the same thread with the sam TimerProc but
 * different pClientData.
 * If not, these are not going to work for multiple sliders and we will have to
 * manage a queue of timeouts.
 */
static void AddTimerEvent(SliderDevice_Instance * pInstance, LTTime t) {
    _PRINT("add.timer", "timer reset at %lld\n",
           LT_Ps64(LTTime_GetMilliseconds(LT_GetCore()->GetKernelTime())));
    s_iThread->SetTimer(s_iThread->GetCurrentThread(), t, &SampleTimerCallback,
                        NULL, pInstance);
}

static void RemoveTimerEvent(SliderDevice_Instance * pInstance) {
    LT_UNUSED(pInstance);
    _PRINT("remove.timer", "timer removed at %lld\n",
           LT_Ps64(LTTime_GetMilliseconds(LT_GetCore()->GetKernelTime())));
    s_iThread->KillTimer(s_iThread->GetCurrentThread(), &SampleTimerCallback,
                         pInstance);
}

/*****************************************************************************/
/* This function always runs on the "EventHandler" thread and no other function changes
 * the state so no need for mutexes.
 *
 * For all interrupt event that call into this function, the value is already
 * checked in ProcessSample, and the timer was started if needed.
 * For timer-driven calls, the timer is killed there because we only need a
 * timer after a key value changed.
 * In both cases, if value is 0 then the timer is not running.
 *
 * We could kill or restart the timer in this function to make it obvious how
 * we handle the timer in the state transitions, but we want to kill or restart
 * the timer as soon as we know we need to do that.
 */
static void SliderStateMachine(SliderDevice_Instance * pInstance, u32 source,
                               u32 newSample, LTTime timestamp) {
    if (!pInstance) return;

    switch (pInstance->state) {
        case INITIAL:
            if (source == kSlider_Interrupt_State_Change) {
                if (newSample) {
                    _PRINT("state.to.touch", "INITIAL->FIRST_TOUCH\n");
                    pInstance->state = FIRST_TOUCH;
                    pInstance->samples[pInstance->sampleTop].nSample = newSample;
                    pInstance->samples[pInstance->sampleTop++].tmTimestamp = timestamp;
                }
                else {
                    /* Interrupts for all keys released should not happen in
                     * this state.
                     */
                    LTLOG_YELLOWALERT("state.initial.int",
                                      "Error: 0 value interrupt in INITIAL state!\n");
                }
            }
            else if (source == kSlider_Timer_State_Change) {
                /* Timer should not be active in this state */
                LTLOG_YELLOWALERT("state.initial.timer",
                                  "Error: Timer interrupt in INITIAL state!\n");
            }
            break;
        case FIRST_TOUCH:
            if (source == kSlider_Interrupt_State_Change) {
                if (newSample) {
                    _PRINT("state.to.swipe", "FIRST_TOUCH->SWIPE\n");
                    pInstance->state = SWIPE;
                    pInstance->samples[pInstance->sampleTop].nSample = newSample;
                    pInstance->samples[pInstance->sampleTop++].tmTimestamp = timestamp;
                }
                else {
                    HandleTouch(pInstance, pInstance->samples[0].nSample);
                    _PRINT("state.to.initial", "FIRST_TOUCH->INITIAL\n");
                    pInstance->state = INITIAL;
                    pInstance->sampleTop = 0;
                }
            }
            else if (source == kSlider_Timer_State_Change) {
                _PRINT("state.to.long", "FIRST_TOUCH->LONG_PRESS\n");
                pInstance->state = LONG_PRESS;
                HandlePress(pInstance, pInstance->samples[0].nSample);
                /* Timer expiration without an interrupt doesn't mean anything
                 * in LONG_PRESS state. The long-press event is already
                 * generated and the new one for the same button is redundant.
                 * The timer is already killed in SampleTimerCallback() and no
                 * need to start a new one.
                 */
            }
            break;
        case LONG_PRESS:
            /* This state is active after a Press event is already generated,
             * or we moved here after a Swipe event when a finger is still
             * pressing on keys. From here we can go to a new swipe sequence if
             * there is a new key press interrupt, or back to INITIAL if all
             * keys are released. No events are generated either way.
             */
            if (source == kSlider_Interrupt_State_Change) {
                if (newSample) {
                    _PRINT("state.to.swipe2", "LONG_PRESS->SWIPE\n");
                    pInstance->state = SWIPE;
                    /* Started a new swipe with a new sample in sample[1]. The
                     * timestamp in sample[0] is an old one, when the long
                     * press was initiated. That could be seconds ago. We'll
                     * set it to the current time minus PRESS_LIMIT/2 to make
                     * it still long but not extremely long.
                     */
                    LTTime_SubtractFrom(timestamp, LTTime_Milliseconds(PRESS_LIMIT / 2));
                    pInstance->samples[0].tmTimestamp = timestamp;
                    pInstance->samples[1].tmTimestamp = timestamp;
                    pInstance->samples[1].nSample = newSample;
                    pInstance->sampleTop = 2;
                }
                else {
                    pInstance->state = INITIAL;
                    _PRINT("state.to.initial2", "LONG_PRESS->INITIAL\n");
                    pInstance->sampleTop = 0;
                }
            }
            else if (source == kSlider_Timer_State_Change) {
                /* Timer should not be active in this state */
                LTLOG_YELLOWALERT("state.long.timer",
                                  "Error: Timer interrupt in LONG_PRESS state!\n");
            }
            break;
        case SWIPE:
            /* We get to this state after two or more key press interrupts.
             * When we leave this state, a Swipe event is generated if we can
             * detect a consistent direction.
             */
            if (source == kSlider_Interrupt_State_Change) {
                if (newSample) {
                    pInstance->samples[pInstance->sampleTop].tmTimestamp = timestamp;
                    pInstance->samples[pInstance->sampleTop++].nSample = newSample;
                    if (pInstance->sampleTop == SAMPLE_MAX) {
                        /* Too many samples, discard the last one and replace
                         * it with 0. The next state is the same one if we just
                         * received zero value, and the timer has to be killed
                         * as well.
                         */
                        pInstance->samples[--pInstance->sampleTop].nSample = 0;
                        _PRINT("state.to.initial3", "SWIPE->INITIAL\n");
                        pInstance->state = INITIAL;
                        RemoveTimerEvent(pInstance);
                        HandleSwipe(pInstance, GenReportActive(pInstance));
                        pInstance->sampleTop = 0;
                    }
                }
                else {
                    /* Movement over the slider is over, this is when the
                     * direction and the speed are calculated. We should never
                     * end up in this state with nSampleTop==0, with a
                     * zero-value interrupt, but just in case we check if we
                     * have some non-zero samples.
                     */
                    if (pInstance->sampleTop == 0) {
                        LTLOG_YELLOWALERT("state.swipe.sample",
                                          "Error: no samples in SWIPE state!\n");
                    }
                    else {
                        /* end of swipe, send an event */
                        _PRINT("state.to.initial4", "SWIPE->INITIAL\n");
                        pInstance->state = INITIAL;
                        pInstance->samples[pInstance->sampleTop].tmTimestamp = timestamp;
                        pInstance->samples[pInstance->sampleTop].nSample = 0;
                        HandleSwipe(pInstance, GenReportActive(pInstance));
                        pInstance->sampleTop = 0;
                    }
                }
            }
            else if (source == kSlider_Timer_State_Change) {
                /* This happens when the finger is not lifted, but the swipe is
                 * over. The swipe event is generated, and we go to the state
                 * LONG_PRESS, which means a new swipe can start but if the
                 * finger is just lifted after any amount of time in state
                 * LONG_PRESS, no new events are generated. Therefore, there is
                 * no need for the timer, and the timer is already killed in
                 * SampleTimerCallback.
                 */

                u32 tempValue = pInstance->samples[pInstance->sampleTop - 1].nSample;
                /* Let's pretend that the swipe was finished halfway through
                 * the long press.
                 */
                LTTime_SubtractFrom(timestamp, LTTime_Milliseconds(PRESS_LIMIT / 2));
                pInstance->samples[pInstance->sampleTop].tmTimestamp = timestamp;
                pInstance->samples[pInstance->sampleTop].nSample = 0;
                HandleSwipe(pInstance, GenReportActive(pInstance));
                pInstance->sampleTop = 0;
                /* We are putting 'timestamp' as an initial value, but from
                 * this moment on the finger can stay pressed for seconds.
                 * We will have to adjust this timestamp when a new swipe
                 * sequence is started from LONG_PRESS.
                 */
                pInstance->samples[pInstance->sampleTop].tmTimestamp = timestamp;
                pInstance->samples[pInstance->sampleTop++].nSample = tempValue;
                pInstance->state = LONG_PRESS;
                _PRINT("state.to.press", "SWIPE->LONG_PRESS\n");
            }
            break;
        default:
            break;
    }
}

/* This function runs on the thread "EventHandler" after an interrupt is generated.
 * It acquires the status from the slider unit and calls SliderStateMachine()
 * for further processing.
 */
static void ProcessSample(void * pData) {
    SliderDevice_Instance * pInstance = (SliderDevice_Instance *)pData;
    LTTime tm = LT_GetCore()->GetKernelTime();
    _PRINT("slider.read.header", "\n******************\n");
    _PRINT("slider.read.time", "int arrived at %lld\n", LT_Ps64(LTTime_GetMilliseconds(tm)));

    LTDeviceUnit hSlider = pInstance->hSlider;
    u32 sample = s_iDriver->ReadValue(hSlider);

    _PRINT("slider.read.value", "value "BINARY_FORMAT, BYTE_TO_BINARY((u8)sample));
    _PRINT("slider.read.footer", "******************\n");

    if (!sample) {
        /* If all buttons are released, we don't need a timer for a long press.
         */
        _PRINT("process.sample.released", "all buttons released for %lx\n",
               LT_PLT_HANDLE(hSlider));
        RemoveTimerEvent(pInstance);
    }
    else {
        /* common actions for a new sample */
        AddTimerEvent(pInstance, LTTime_Milliseconds(PRESS_LIMIT));
    }
    SliderStateMachine(pInstance, kSlider_Interrupt_State_Change, sample, tm);
}

/******************************************************************************
 *  Callbacks from the driver, the sample timer and the client update callbacks
 */

/* Called from the driver after an interrupt.
 * IntCallback is called from the driver and runs in the interrupt context, so don't do anything that is not absolutely necessary and make it quick.
 * We queue ProcessSample here instead in the driver, so that the driver doesn't need to keep track of a thread where to queue a proc.
 */
static void IntCallback(void * pData) {
    s_iThread->QueueTaskProc(s_hEventThread, (LTThread_TaskProc *)&ProcessSample, NULL, pData);
}

/* TimerProc that invokes a periodic callback requested by the client. It runs
 * on the client's thread.
 */
static void ClientTimerCallback(void * pData) {
    LTDeviceUnit hDevice = VOIDPTR_TO_LTHANDLE(pData);
    SliderDevice_Instance * pInstance = GetInstanceFromHandle(hDevice);
    if (!IS_ENABLED(pInstance)) return;

    if (pInstance->hSlider && IS_PERIODIC(pInstance)) {
        LTLOG_DEBUG("client.timer.callback", "periodic callback for %lx",
                    LT_Pu32(pInstance));
        pInstance->mutex->API->Lock(pInstance->mutex);
        s32 val = pInstance->value;
        pInstance->mutex->API->Unlock(pInstance->mutex);
        (pInstance->pCallback)(hDevice, val, pInstance->pClientData);
        return;
    }
    /* something went wrong, kill the timer */
    s_iThread->KillTimer(s_iThread->GetCurrentThread(),
                         (LTThread_TimerProc *)&ClientTimerCallback,
                         LTHANDLE_TO_VOIDPTR(hDevice));
}

/* This function is queued on the "EventHandler" thread. It gets active only if the same set of keys is in the same "press" state for longer than PRESS_LIMIT.
 */
static void SampleTimerCallback(void * pData) {
    SliderDevice_Instance * pInstance = (SliderDevice_Instance *)pData;

    LTTime timestamp = LT_GetCore()->GetKernelTime();
    _PRINT("timer.callback.long", "long press at %lld\n",
           LT_Ps64(LTTime_GetMilliseconds(timestamp)));

    /* These are one-shot timers, there are always killed if a timer interrupt
     * happens. They are restarted only when an interrupt is triggered by a key
     * event. Once the keys are pressed longer than PRESS_LIMIT, we don't track
     * how much longer.
     */
    RemoveTimerEvent(pInstance);

    /* The second argument 'value' is 0 because if SampleTimerCallback is
     * called, the keys still pressed are the same ones as for the last key
     * triggered interrupt.
     */
    SliderStateMachine(pInstance, kSlider_Timer_State_Change, 0, timestamp);
}

/* Handle<Event> functions run on the device's "EventHandler" thread. These are the function that queue DispatchClientCB on the client's thread, if the client subscribes to interrupt-driven callbacks.
 * The reason why the event functions don't queue client callbacks directly is that the callbacks would have to have the signature that fits LTThread_TaskProc. This seems cheaper than to allocate a structure from heap and ask the client to free it in their callbacks.
 */
static void DispatchClientCB(void * pData) {
    SliderDevice_Instance * pInstance = (SliderDevice_Instance *)pData;
    LTDeviceUnit hDevice = GetHandleFromInstance(pInstance);
    _PRINT("dispatch.clientCB", "event callback for instance %lx\n", LT_Pu32(pInstance));
    pInstance->mutex->API->Lock(pInstance->mutex);
    s32 val = pInstance->value;
    pInstance->mutex->API->Unlock(pInstance->mutex);
    (pInstance->pCallback)(hDevice, val, pInstance->pClientData);
}

/******************************************************************************/

/* Internal function that gets the unit in  the initial state */
static void ResetUnit(SliderDevice_Instance * pInstance) {
    if (LT_GetCore()->IsHandleValid(pInstance->hSlider)) {
        s_iDriver->SetISRDispatchProc(pInstance->hSlider, NULL, NULL);
        LT_GetCore()->Destroy(pInstance->hSlider);
    }
    pInstance->hSlider     = 0;
    pInstance->pCallback   = NULL;
    pInstance->pClientData = NULL;
    pInstance->mutex->API->Lock(pInstance->mutex);
    pInstance->hCBThread   = 0;
    pInstance->lowerBound  = LT_S32_MIN + 1;  // LT_S32_MIN is reserved
    pInstance->upperBound  = LT_S32_MAX;
    pInstance->value       = 0;
    pInstance->nMaxStep    = 0;
    pInstance->keyCount    = 0;
    /* All flags to 0 */
    SET_INTDRIVEN(pInstance);
    DISABLE(pInstance);
    pInstance->mutex->API->Unlock(pInstance->mutex);
    pInstance->state       = INITIAL;
    pInstance->sampleTop   = 0;
}

/* The whole instance will be destroyed. If there is anything to be
 * deallocated, it will be always handled in ResetUnit, so we go there first.
 * That will do some work on the memory that will be freed anyway, but that's
 * an optimization for later.
 */
static void LTSliderDeviceUnit_OnDestroyHandle(LTHandle hDevice) {
    if (hDevice) {
        SliderDevice_Instance * pInstance =
            (SliderDevice_Instance *)LT_GetCore()->GetHandlePrivateData(hDevice);
        if (pInstance) {
            if (pInstance->mutex) {
                lt_destroyobject(pInstance->mutex);
                pInstance->mutex = NULL;
            }
            ResetUnit(pInstance);
        }
    }
    /* Update the array of handles */
    u32 index = GetIndexFromHandle(hDevice);
    if (index != LT_U32_MAX) {
        LTHandle tempHandle = 0;
        s_pDeviceHandles->API->Set(s_pDeviceHandles, index, &tempHandle);
    }
}

define_LTLIBRARY_INTERFACE(ILTSliderDeviceUnit, LTSliderDeviceUnit_OnDestroyHandle) {
    .Initialize                   = LTSliderDeviceUnit_Initialize,
    .RegisterForUpdates           = LTSliderDeviceUnit_RegisterForUpdates,
    .UnregisterFromUpdates        = LTSliderDeviceUnit_UnregisterFromUpdates,
    .SetValue                     = LTSliderDeviceUnit_SetValue,
    .GetValue                     = LTSliderDeviceUnit_GetValue,
} LTLIBRARY_DEFINITION

/*****************************************************************************/

/*
 * We have only one driver in one library, but if there are multiple drivers
 * then this library will have to manage mappings between ids supplied by the
 * clients when requesting a handle and ids supplied to the drivers when
 * drivers' handles are requested.
 */
static LTDeviceUnit LTDeviceSliderImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDevice = 0;

    if (nDeviceUnitNumber < s_nNumDeviceUnits) {
        /* If we already have a handle pointing to that SliderDevice_Instance,
         * return it. The assumption here is that there is only one controller and it
         * already owns a handle to this device, so we return it.
         * One alternative is to return 0, as a sign that the device is busy.
         * If we decide that multiple handles can point to the same device,
         * then the logic below must change because right now each new handle
         * allocates space for a whole SliderDevice_Instance, and any new
         * handle must be connected to that same allocated space.
         */
        s_pDeviceHandles->API->Get(s_pDeviceHandles, nDeviceUnitNumber, &hDevice);
        if (hDevice != 0) {
            LTLOG_DEBUG("create.handle.allocated", "unit is already allocated %lx\n",
                        LT_PLT_HANDLE(hDevice));
            return hDevice;
        }

        hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTSliderDeviceUnit,
                                             sizeof(SliderDevice_Instance));
        if (hDevice) {
            SliderDevice_Instance * pInstance =
                (SliderDevice_Instance *)LT_GetCore()->GetHandlePrivateData(hDevice);
            if (pInstance != NULL) {
                ResetUnit(pInstance);
                pInstance->mutex = lt_createobject(LTMutex);
                if (pInstance->mutex) {
                    /* For multiple drivers, we'll need to adjust the id */
                    pInstance->hSlider = s_pDriver->CreateDeviceUnitHandle(nDeviceUnitNumber);
                    if (pInstance->hSlider != 0) {
                        s_iDriver->SetISRDispatchProc(pInstance->hSlider, IntCallback, pInstance);
                        pInstance->keyCount = s_iDriver->GetKeyCount(pInstance->hSlider);
                        s_pDeviceHandles->API->Set(s_pDeviceHandles, nDeviceUnitNumber, &hDevice);
                        return (hDevice);
                    }
                    lt_destroyobject(pInstance->mutex);
                    pInstance->mutex = NULL;
                }
                else {
                    LTLOG_REDALERT("create.handle.mutex", "create mutex error\n");
                }
            }
            else {
                _PRINT("create.handle.private", "private data is NULL\n");
            }
            LT_GetCore()->DestroyHandle(hDevice);
            hDevice = 0;
        }
    }
    return hDevice;
}

static u32 LTDeviceSliderImpl_GetNumDeviceUnits(void) {
    return s_nNumDeviceUnits;
}

/******************************************************************************
 * Library startup and shutdown
 */
static void ShutDownDriver(void) {
    s_iDriver = NULL;
    if (s_pDriver) {
        LT_GetCore()->CloseLibrary((LTLibrary *)s_pDriver);
    }
    s_pDriver = NULL;
}

/* Destroy the array, the clients should have destroyed the handles by now and
 * all array elements should be 0.
 * We check it here and warn, and then destroy the array.
*/
static void LTDeviceSliderImpl_LibFini(void) {
    for (u32 i = 0; i < s_nNumDeviceUnits; i++) {
        LTDeviceUnit handle;
        s_pDeviceHandles->API->Get(s_pDeviceHandles, i, &handle);
        if (handle != 0) {
            LTLOG_YELLOWALERT("lib.fini",
                              "The slider library closing while handle %lx still active!\n",
                              LT_PLT_HANDLE(handle));
        }
    }
    lt_destroyobject(s_pDeviceHandles);
    s_iThread = NULL;
    ShutDownDriver();
}

/* At the startup, we create an array that keeps track of active handles and
 * maps them to their device numbers. The handles and instances are created when
 * CreateDeviceUnitHandle() is called.
 * We also get the number of available device units, and create the device
 * library worker thread.
 */
static bool LTDeviceSliderImpl_LibInit(void) {
    if (!(s_pDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceSlider", 0))) {
        return false;
    }
    if (!(s_nNumDeviceUnits = s_pDriver->GetNumDeviceUnits())) {
        LTLOG_YELLOWALERT("lib.init.slider", "can't find any slider units");
        ShutDownDriver();
        return false;
    }
    if (!(s_iDriver = (ILTDriverSlider *)LT_GetCore()->GetLibraryInterface(
        (LTLibrary *)s_pDriver, "ILTDriverSlider"))) {
        LTLOG_YELLOWALERT("lib.init.interface", "can't find the interface ILTDriverSlider");
        ShutDownDriver();
        return false;
    }

    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    /* This is a thread that will be used to process samples */
    s_hEventThread = LT_GetCore()->CreateThread("SliderEventHandler");
    if (!s_hEventThread) {
        LTLOG_YELLOWALERT("lib.init.thread", "Could not open a new thread");
        LTDeviceSliderImpl_LibFini();
        return false;
    }
    s_iThread->SetStackSize(s_hEventThread, 1024);
    s_iThread->Start(s_hEventThread, NULL, NULL);

    s_pDeviceHandles = LTArray_CreateStructArray(sizeof(LTHandle));
    LTHandle handle = 0;
    for (u32 i = 0; i < s_nNumDeviceUnits; i++) {
        s_pDeviceHandles->API->Append(s_pDeviceHandles, (void *)&handle);
    }

    return true;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceSlider) LTLIBRARY_DEFINITION

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Feb-22   commodus    created
 *  03-Apr-23   augustus    load driver from LTDeviceConfig specification
 */
