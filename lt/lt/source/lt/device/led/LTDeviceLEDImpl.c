/*******************************************************************************
 * lt/source/lt/device/led/LTDeviceLEDImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/led/LTDeviceLED.h>

DEFINE_LTLOG_SECTION("dev.led");

/***********************************************************************************************************************
 **************** BEGIN COMMON DRIVER MANAGEMENT ***********************************************************************
 **********************************************************************************************************************/

typedef struct DriverLibraryList {
    LT_SIZE             nNumDriverLibraries;
    LTDriverLibrary   **ppDriverLibraries;
} DriverLibraryList;

static DriverLibraryList s_DriverLibraries;

/***********************************************************************************************************************
 * Driver configuration.
 * Read the Device Config for this Device and load all listed Drivers, storing a pointer to each
 * in s_DriverLibraries.
 * Return the number of Driver Libraries actually loaded:                                                             */

static u32 LoadDriverLibraries(const char *pDeviceLibraryName, DriverLibraryList *pLibraryList) {
    if (!pDeviceLibraryName || !*pDeviceLibraryName || !pLibraryList) return 0;
    LTDeviceConfig *pDeviceConfig = lt_openlibrary(LTDeviceConfig);
    if (!pDeviceConfig) return false;
    LT_SIZE nDriverLibraries = pDeviceConfig->GetNumDrivers(pDeviceLibraryName);
    if (!nDriverLibraries) {
        LTLOG_YELLOWALERT("drv.lib.config.0", NULL);
        return false;
    }
    if (!(pLibraryList->ppDriverLibraries = lt_malloc(nDriverLibraries * sizeof(LTDriverLibrary *)))) {
        LTLOG_YELLOWALERT("drv.lib.array.oom", NULL);
        return false;
    }
    LTDriverLibrary **ppDriverLibrary = pLibraryList->ppDriverLibraries;
    for (LT_SIZE i = 0; i < nDriverLibraries; ++i) {
        LTLOG_DEBUG("drv.lib.name", "%s", pDeviceConfig->GetDriverAt(pDeviceLibraryName, i));
        if ((*ppDriverLibrary = (LTDriverLibrary *)LT_GetCore()->OpenLibrary(pDeviceConfig->GetDriverAt(pDeviceLibraryName, i))))
            ++ppDriverLibrary;  /* success - point to the next position in the array */
    }    /* (If the Driver didn't load, Core will complain via a YELLOWALERT) */
    /* If some Drivers failed to load, the array will be too big - trim it down: */
    pLibraryList->nNumDriverLibraries = ppDriverLibrary - pLibraryList->ppDriverLibraries;
    if (!pLibraryList->nNumDriverLibraries) {
        LTLOG_YELLOWALERT("drv.lib.0", NULL);
        lt_free(pLibraryList->ppDriverLibraries);
        pLibraryList->ppDriverLibraries = NULL;
    }
    else if (pLibraryList->nNumDriverLibraries < nDriverLibraries) {
        LTLOG_YELLOWALERT("drv.lib.n", "%lu/%lu", LT_PLT_SIZE(pLibraryList->nNumDriverLibraries), LT_PLT_SIZE(nDriverLibraries));
        lt_realloc(pLibraryList->ppDriverLibraries, pLibraryList->nNumDriverLibraries * sizeof(LTDriverLibrary *));
    }
    lt_closelibrary(pDeviceConfig);
    return pLibraryList->nNumDriverLibraries;
}

static void UnloadDriverLibraries(DriverLibraryList *pLibraryList) {
    if (pLibraryList && pLibraryList->ppDriverLibraries) {
        LTDriverLibrary **ppDriverLibrary = pLibraryList->ppDriverLibraries;
        for (LT_SIZE i = pLibraryList->nNumDriverLibraries; i; --i, ++ppDriverLibrary) LT_GetCore()->CloseLibrary((LTLibrary *)*ppDriverLibrary);
        lt_free(pLibraryList->ppDriverLibraries);
        pLibraryList->ppDriverLibraries = NULL;
        pLibraryList->nNumDriverLibraries = 0;
    }
}

/***********************************************************************************************************************
 * Return a pointer to the Driver Library which provides the Device Unit specified by the Device Unit index:          */
static LTDriverLibrary *GetDriverLibraryProvidingDeviceUnit(DriverLibraryList *pLibraryList, u32 nIndex, u32 *pIndexInDriver) {
    if (!pLibraryList) return NULL;
    u32 nNumDeviceUnits = 0;        /* running total of Device Units available from this Driver and all previous */
    u32 nDeviceUnitIndexBase = 0;   /* base index of Device Units provided by this Driver                        */
    for (u32 i = 0; i < pLibraryList->nNumDriverLibraries; ++i) {
        u32 nNumDeviceUnitsThisDriver = pLibraryList->ppDriverLibraries[i]->GetNumDeviceUnits();
        nNumDeviceUnits += nNumDeviceUnitsThisDriver;
        if (nNumDeviceUnits > nIndex) {
            if (pIndexInDriver) *pIndexInDriver = nIndex - nDeviceUnitIndexBase;
            return pLibraryList->ppDriverLibraries[i];
        }
        nDeviceUnitIndexBase += nNumDeviceUnitsThisDriver;
    }
    return NULL;   /* did not find a Driver with the right Device Unit index range */
}

static u32 GetNumDeviceUnits(DriverLibraryList *pLibraryList) {
    if (!pLibraryList) return 0;
    u32 nNumDeviceUnits = 0;
    for (u32 i = 0; i < pLibraryList->nNumDriverLibraries; ++i) nNumDeviceUnits += pLibraryList->ppDriverLibraries[i]->GetNumDeviceUnits();
    return nNumDeviceUnits;
}

static LTDeviceUnit CreateDeviceUnitHandle(DriverLibraryList *pLibraryList, u32 nDeviceUnitIndex) {
    u32 nIndexInDriver = 0;
    LTDriverLibrary *pLibrary = GetDriverLibraryProvidingDeviceUnit(pLibraryList, nDeviceUnitIndex, &nIndexInDriver);
    return pLibrary ? pLibrary->CreateDeviceUnitHandle(nIndexInDriver) : 0;
}

/***********************************************************************************************************************
 ****************** END COMMON DRIVER MANAGEMENT ***********************************************************************
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Standard Device Unit access:                                                                                       */

static u32 LTDeviceLEDImpl_GetNumDeviceUnits(void) { return GetNumDeviceUnits(&s_DriverLibraries); }

/* Look through the Driver LT Libraries to find a Device Unit by number.  Return the handle, or 0 upon failure: */
static LTDeviceUnit LTDeviceLEDImpl_CreateDeviceUnitHandle(u32 nDeviceUnitIndex) {
    return CreateDeviceUnitHandle(&s_DriverLibraries, nDeviceUnitIndex);
}

/***********************************************************************************************************************
 * Interface-specific Device Unit conversion:                                                                         */
static bool LTDeviceLEDImpl_GetGroupDescriptorFromUnitNumber(u32 nDeviceUnitIndex, LTDeviceLED_GroupDescriptor *pDescriptor) {
    u32 nIndexInDriver = 0;
    LTDriverLibrary *pLibrary = GetDriverLibraryProvidingDeviceUnit(&s_DriverLibraries, nDeviceUnitIndex, &nIndexInDriver);
    if (!pLibrary) return false;
    ILTDriverLED *pILTDriverLED = lt_getlibraryinterface(ILTDriverLED, pLibrary);
    if (!pILTDriverLED || !pILTDriverLED->GetGroupDescriptorFromUnitNumber(nIndexInDriver, pDescriptor)) return false;
    pDescriptor->m_nDeviceUnitNumber = nDeviceUnitIndex;
    return true;
}

static bool LTDeviceLEDImpl_GetUnitNumberFromGroupName(char const *pGroupName, u32 *pDeviceUnitNumberToSet) {
    u32 nDeviceUnitIndexBase = 0;
    for (u32 i = 0; i < s_DriverLibraries.nNumDriverLibraries; ++i) {
        LTDriverLibrary *pLibrary = s_DriverLibraries.ppDriverLibraries[i];
        ILTDriverLED *pILTDriverLED = lt_getlibraryinterface(ILTDriverLED, pLibrary);
        if (!pILTDriverLED) return false;
        u32 nDeviceUnitIndex;
        if (pILTDriverLED->GetUnitNumberFromGroupName(pGroupName, &nDeviceUnitIndex)) {
            *pDeviceUnitNumberToSet = nDeviceUnitIndexBase + nDeviceUnitIndex;
            return true;
        }
        nDeviceUnitIndexBase += pLibrary->GetNumDeviceUnits();
    }
    return false;
}

/********************************************************************************************************************************************
 * LTDeviceLEDFader:                                                                                                                       */

typedef_LTObjectImpl(LTDeviceLEDFader, LTDeviceLEDFaderImpl) {
    LTTime                                     transitionStepInterval;       /* time between color-ramping iterations                      */
    LTOThread                                 *thread;                       /* thread along which the Fader and callbacks run             */
    LTMutex                                   *mutex;                        /* serialization and interruption                             */
    ILTDriverLED_GroupType_IndicatorLamp      *iLED;                         /* LED Driver access                                          */
    LTDeviceUnit                               hLED;                         /* LED Device Unit access                                     */
    const LTDeviceLEDFaderTimingStep          *pTimingSteps;                 /* currently running sequence                                 */
    const LTDeviceLEDFaderTimingStep          *pNextStep;                    /* currently running sequence step                            */
    union {
        LTDeviceLEDFader_FadeCompleteProc             *pFadeCompleteProc;              /* single-fade completion callback                  */
        LTDeviceLEDFader_FadeSequenceStepCompleteProc *pFadeSequenceStepCompleteProc;  /* sequence-step completion callback                */
    };
    LTDeviceLEDFader_FadeSequenceCompleteProc *pFadeSequenceCompleteProc;    /* sequence completion callback                               */
    void                                      *pClientData;                  /* for the completion callbacks                               */
    u32                                        nNumTransitionStepsRemaining; /* number of color-ramping iterations remaining               */
    u32                                        previousColor;                /* color of the LED before the current sequence step          */
    u32                                        intensity;                    /* current intensity of the LED                               */
    s32                                        intensityIncrement;           /* change in intensity per color-ramping iteration            */
    u32                                        red;                          /* current red component of the LED                           */
    s32                                        redIncrement;                 /* change in red component per color-ramping iteration        */
    u32                                        green;                        /* current green component of the LED                         */
    s32                                        greenIncrement;               /* change in green component per color-ramping iteration      */
    u32                                        blue;                         /* current blue component of the LED                          */
    s32                                        blueIncrement;                /* change in blue component per color-ramping iteration       */
    u32                                        nLEDIndex;                    /* index of the LED in the Device Unit ("LED Group")          */
    u16                                        nStepCount;                   /* total number of steps in the sequence                      */
    u16                                        nNumStepsRemaining;           /* number of steps remaining in the sequence                  */
    bool                                       bThreadIsMine;                /* true if this Fader owns the thread (and should destroy it) */
    bool                                       bHoldingColor;                /* true if this Fader is currently on a hold sequence         */
} LTOBJECT_API;

/* Single Fade creation and destruction (used by LTDeviceLEDImpl_Fade() and HoldColor()) ***********************************************/

static bool CreateSingleFade(LTDeviceLEDFaderImpl *pThis) {
    if ((pThis->pTimingSteps = pThis->pNextStep = lt_malloc(sizeof(LTDeviceLEDFaderTimingStep))))
        pThis->nNumStepsRemaining = pThis->nStepCount = 1;
    return pThis->pTimingSteps;
}

static void DestroySingleFade(LTDeviceLEDFaderImpl *pThis) {
    /* This CASTS AWAY CONST in the special case of a single fade created by CreateSingleFade() above: */
    lt_free((void *)pThis->pTimingSteps);
    pThis->pTimingSteps = pThis->pNextStep = NULL;
    pThis->nStepCount = pThis->nNumStepsRemaining = 0;
}

/***************************************************************************************************************************************/
/* The functions below implement the LTDeviceLEDFader engine.  Every function that is called through a timer or a task
   proc will attempt to acquire the mutex.  If successful, the engine continues to run.  If not, the engine simply
   stops.  All of these functions run along one thread, and a function encountering a locked mutex means that the
   sequence is changing or stopping due to a call to one of the LTDeviceLEDFader API functions. */

static void BeginNextStep(void *pClientData);
static void RampColors(void *pClientData);
static void NotifySingleFadeComplete(LTDeviceLEDFaderImpl *pThis, bool bSuccess);
static void NotifyFadeSequenceStepComplete(LTDeviceLEDFaderImpl *pThis, bool bSuccess);
static void NotifyFadeSequenceComplete(LTDeviceLEDFaderImpl *pThis, bool bSuccess);

/* The sequence step is complete.  Advance to the next step, or start at the beginning: */
static void AdvanceStep(void *pClientData) {
    LTDeviceLEDFaderImpl *pThis = pClientData;
    if (pThis->mutex->API->TryLock(pThis->mutex)) {  /* the sequence is not changing - proceed. */
        pThis->thread->API->KillTimer(pThis->thread, &AdvanceStep, pThis);
        pThis->bHoldingColor = false;
        NotifyFadeSequenceStepComplete(pThis, true);
        if (--pThis->nNumStepsRemaining) {
            ++pThis->pNextStep;
        } else {
            NotifyFadeSequenceComplete(pThis, true);
            pThis->nNumStepsRemaining = pThis->nStepCount;   /* restart the sequence */
            pThis->pNextStep = pThis->pTimingSteps;
        }
        pThis->mutex->API->Unlock(pThis->mutex);    /* allow the sequence to be interrupted by an API function call */
    }
    BeginNextStep(pThis);
}

/* The fader has ramped the color to its new state.  Hold the color for the dwell time, then advance to the next step.
   PRECONDITION: the mutex is held: */
static void HoldColor(LTDeviceLEDFaderImpl *pThis) {
    pThis->thread->API->KillTimer(pThis->thread, &RampColors, pThis);
    pThis->bHoldingColor = true;
    pThis->iLED->SetLEDColor(pThis->hLED, pThis->nLEDIndex, pThis->previousColor = pThis->pNextStep->color);
    switch (pThis->pNextStep->dwell) {
    case LT_U16_MAX:    /* reserved value used by Fade() to signal a single-step fade stops the sequence */
        NotifySingleFadeComplete(pThis, true);
        DestroySingleFade(pThis);
        break;
    case 0:             /* a zero dwell stops the sequence */
        NotifyFadeSequenceStepComplete(pThis, true);
        NotifyFadeSequenceComplete(pThis, true);
        break;
    default:            /* any other value advances to the next step after the dwell time */
        pThis->thread->API->SetTimer(pThis->thread, LTTime_Milliseconds(pThis->pNextStep->dwell), &AdvanceStep, NULL, pThis);
    }
}

/* Increment the color components toward the final color state for this sequence step: */
static void IncrementColors(LTDeviceLEDFaderImpl *pThis) {
    --pThis->nNumTransitionStepsRemaining;
    pThis->iLED->SetLEDColor(pThis->hLED, pThis->nLEDIndex,   ((pThis->intensity += pThis->intensityIncrement) & 0x7F800000) << 1
                                                            | ((pThis->red       += pThis->redIncrement)       & 0x7F800000) >> 7
                                                            | ((pThis->green     += pThis->greenIncrement)     & 0x7F800000) >> 15
                                                            | ((pThis->blue      += pThis->blueIncrement)      & 0x7F800000) >> 23);
}

/* The fader is ramping the color towards the final color state for this sequence step.  Continue to increment
   the color until the transition time has elapsed, then hold the color for the dwell time: */
static void RampColors(void *pClientData) {
    LTDeviceLEDFaderImpl *pThis = pClientData;
    if (pThis->mutex->API->TryLock(pThis->mutex)) {   /* the sequence is not changing - proceed. */
        if (pThis->nNumTransitionStepsRemaining)
            IncrementColors(pThis);
        else
            HoldColor(pThis);
        pThis->mutex->API->Unlock(pThis->mutex);
    }
}

/* Calculate an increment of a single color component, given its beginning and ending value,
   and the number of transition steps.  Prevent a color component that is changing (beginning
   and ending values not equal) from not changing at all: */
static void CalculateColorIncrement(LTDeviceLEDFaderImpl *pThis, u32 now, u32 next, s32 *pIncrement) {
    *pIncrement = next - now;
    if (*pIncrement) {
        *pIncrement /= (s32)pThis->nNumTransitionStepsRemaining;
        if (!*pIncrement) *pIncrement = (next > now) ? 1 : -1;
    }
}

/* Determine how much a color component (intensity, red, green, or blue) changes from one transition step to the next.  The
   current 8-bit value of a color component is shifted up as high as possible in its u32 to allow for maximum precision of
   the increment.  That 8-bit color component value is then shifted back down to its correct position for passing to LTDeviceLED: */
static void BeginColorRamp(LTDeviceLEDFaderImpl *pThis) {
    CalculateColorIncrement(pThis, (pThis->intensity = (pThis->previousColor & 0xFF000000) >> 1),  (pThis->pNextStep->color & 0xFF000000) >> 1,  &pThis->intensityIncrement);
    CalculateColorIncrement(pThis, (pThis->red       = (pThis->previousColor & 0x00FF0000) << 7),  (pThis->pNextStep->color & 0x00FF0000) << 7,  &pThis->redIncrement);
    CalculateColorIncrement(pThis, (pThis->green     = (pThis->previousColor & 0x0000FF00) << 15), (pThis->pNextStep->color & 0x0000FF00) << 15, &pThis->greenIncrement);
    CalculateColorIncrement(pThis, (pThis->blue      = (pThis->previousColor & 0x000000FF) << 23), (pThis->pNextStep->color & 0x000000FF) << 23, &pThis->blueIncrement);
    IncrementColors(pThis);
    pThis->thread->API->SetTimer(pThis->thread, pThis->transitionStepInterval, &RampColors, NULL, pThis);
}

/* Execute the next step in a blink-pattern sequence: */
static void BeginNextStep(void *pClientData) {
    LTDeviceLEDFaderImpl *pThis = pClientData;
    if (pThis->mutex->API->TryLock(pThis->mutex)) {   /* the sequence is not changing - proceed. */
        if ((pThis->nNumTransitionStepsRemaining = pThis->pNextStep->transition / LTTime_GetMilliseconds(pThis->transitionStepInterval)))
            BeginColorRamp(pThis);
        else
            HoldColor(pThis);
        pThis->mutex->API->Unlock(pThis->mutex);
    }
}

static LTTime OnSleepActionProc(LTCore_SleepAction action, LTTime wakeupTimeOrSleepDuration, void *pClientData) {
    LT_UNUSED(wakeupTimeOrSleepDuration);
    LTDeviceLEDFaderImpl *pThis = pClientData;
    /* When waking up from sleep while on a hold sequence, restore LED back to the expected color.
     * Restoration does not need to happen if the sequence is actively being changed
     * since the LED will be changed to a new color. */
    if (action == kLTCore_SleepAction_AwakenedFromSleep) {
        pThis->mutex->API->Lock(pThis->mutex);
        if (pThis->bHoldingColor && pThis->pNextStep) {
            pThis->iLED->SetLEDColor(pThis->hLED, pThis->nLEDIndex, pThis->pNextStep->color);
        }
        pThis->mutex->API->Unlock(pThis->mutex);
    }
    return LTTime_Zero();
}

/* Completion callback mechanics *******************************************************************************************************/
/* For each callback type, define a callback context, a task proc to call the callback, and a function
   for the engine to call to generate the respective context and queue the task proc.
   The context is necessary because the callbacks use data object data that is mutex-protected, and the callback is called
   along the Fader's thread outside the mutex. */

static void ReleaseClientData(LTThread_ReleaseReason releaseReason, void *pClientData) { LT_UNUSED(releaseReason); lt_free(pClientData); }

/* Single-fade-complete callback ***************************************************************************************/

typedef struct {
    LTDeviceLEDFader_FadeCompleteProc    *pProc;             /* proc to call to signal that the fade completed */
    void                                 *pClientData;       /* client data for the proc                       */
    u32                                   color;             /* the color that was actually reached            */
    bool                                  success;           /* true if the fade ran its course                */
} SingleFadeCompleteContext;

static void SingleFadeComplete(void *pClientData) {
    SingleFadeCompleteContext *pContext = pClientData;
    if (pContext->pProc) pContext->pProc(pContext->color, pContext->success, pContext->pClientData);
}

static void NotifySingleFadeComplete(LTDeviceLEDFaderImpl *pThis, bool bSuccess) {
    SingleFadeCompleteContext *pContext = lt_malloc(sizeof(SingleFadeCompleteContext));
    if (pContext) {
        *pContext = (SingleFadeCompleteContext){ .pProc = pThis->pFadeCompleteProc, .pClientData = pThis->pClientData,
                                                 .color = pThis->previousColor,     .success = bSuccess };
        pThis->thread->API->QueueTaskProc(pThis->thread, SingleFadeComplete, ReleaseClientData, pContext);
    }
}

/* Fade-sequence-step-complete callback ********************************************************************************/

typedef struct {
    LTDeviceLEDFader_FadeSequenceStepCompleteProc      *pProc;       /* proc to call to signal that the step completed */
    void                                               *pClientData; /* client data for the proc                       */
    const LTDeviceLEDFaderTimingStep                   *pStep;       /* the currently running step                     */
    bool                                                bSuccess;    /* true if the step ran its course                */
} FadeSequenceStepCompleteContext;

static void FadeSequenceStepComplete(void *pClientData) {
    FadeSequenceStepCompleteContext *pContext = pClientData;
    if (pContext->pProc) pContext->pProc(pContext->pStep, pContext->bSuccess, pContext->pClientData);
}

static void NotifyFadeSequenceStepComplete(LTDeviceLEDFaderImpl *pThis, bool bSuccess) {
    FadeSequenceStepCompleteContext *pContext = lt_malloc(sizeof(FadeSequenceStepCompleteContext));
    if (pContext) {
        *pContext = (FadeSequenceStepCompleteContext){ .pProc = pThis->pFadeSequenceStepCompleteProc, .pClientData = pThis->pClientData,
                                                       .pStep = pThis->pNextStep, .bSuccess = bSuccess };
        pThis->thread->API->QueueTaskProc(pThis->thread, FadeSequenceStepComplete, ReleaseClientData, pContext);
    }
}

/* Fade-sequence-complete callback *************************************************************************************/

typedef struct {
    LTDeviceLEDFader_FadeSequenceCompleteProc          *pProc;       /* proc to call to signal that the sequence completed */
    void                                               *pClientData; /* client data for the proc                           */
    const LTDeviceLEDFaderTimingStep                   *pSequence;   /* the sequence that was running                      */
    bool                                                bSuccess;    /* true if the sequence ran its course                */
} FadeSequenceCompleteContext;

static void FadeSequenceComplete(void *pClientData) {
    FadeSequenceCompleteContext *pContext = pClientData;
    if (pContext->pProc) pContext->pProc(pContext->pSequence, pContext->bSuccess, pContext->pClientData);
}

static void NotifyFadeSequenceComplete(LTDeviceLEDFaderImpl *pThis, bool bSuccess) {
    FadeSequenceCompleteContext *pContext = lt_malloc(sizeof(FadeSequenceCompleteContext));
    if (pContext) {
        *pContext = (FadeSequenceCompleteContext) { .pProc = pThis->pFadeSequenceCompleteProc, .pClientData = pThis->pClientData,
                                                    .pSequence = pThis->pTimingSteps, .bSuccess = bSuccess };
        pThis->thread->API->QueueTaskProc(pThis->thread, FadeSequenceComplete, ReleaseClientData, pContext);
    }
}

/* Engine stop *************************************************************************************************************************/

/* PRECONDITION: this function is called while the mutex is held. */
static void KillTimers(LTDeviceLEDFaderImpl *pThis) {
    if (pThis->thread) {
        pThis->thread->API->KillTimer(pThis->thread, &AdvanceStep, pThis);
        pThis->thread->API->KillTimer(pThis->thread, &RampColors, pThis);
    }
    if (pThis->pTimingSteps) {  /* A sequence is present and might be running */
        switch (pThis->pNextStep->dwell) {
        case LT_U16_MAX:                    /* single fade */
            DestroySingleFade(pThis);
            NotifySingleFadeComplete(pThis, false);
            break;
        case 0:                             /* last step in a sequence that does not repeat */
            if (!pThis->nNumTransitionStepsRemaining) break;  /* the sequence is already complete */
            /* fall through */
        default:
            NotifyFadeSequenceStepComplete(pThis, false);
            NotifyFadeSequenceComplete(pThis, false);
            break;
        }
    }
}

/* LTObject Creation, Destruction, Initialization **************************************************************************************/

static bool LTDeviceLEDFaderImpl_ConstructObject(LTDeviceLEDFaderImpl *pThis) { LT_UNUSED(pThis); return true; }

static void LTDeviceLEDFaderImpl_DestructObject(LTDeviceLEDFaderImpl *pThis) {
    if (pThis->mutex) pThis->mutex->API->Lock(pThis->mutex); /* If any of the timer procs find the mutex held, they do not continue the sequence. */
    KillTimers(pThis);
    if (pThis->mutex) pThis->mutex->API->Unlock(pThis->mutex);
    if (pThis->bThreadIsMine) lt_destroyobject(pThis->thread);
    lt_destroyobject(pThis->mutex);
    lt_destroyhandle(pThis->hLED);
}

static void RegisterForEvents(void *clientData) {
    LT_GetCore()->OnSleepAction(OnSleepActionProc, clientData);
}

static bool LTDeviceLEDFaderImpl_Initialize(LTDeviceLEDFaderImpl *pThis, const char *pGroupName, u32 nLEDIndex, LTOThread *pOThread, LTTime interval) {
    if (!pGroupName || !*pGroupName) return false;
    do {
        if (!(pThis->mutex = lt_createobject(LTMutex))) break;
        u32 nDeviceUnitNumber;
        if (!LTDeviceLEDImpl_GetUnitNumberFromGroupName(pGroupName, &nDeviceUnitNumber)) break;
        if (!(pThis->hLED = LTDeviceLEDImpl_CreateDeviceUnitHandle(nDeviceUnitNumber))) break;
        if (   !(pThis->iLED = lt_gethandleinterface(ILTDriverLED_GroupType_IndicatorLamp, pThis->hLED))
            || pThis->iLED->GetNumberOfElements(pThis->hLED) <= nLEDIndex) break;
        if (!pOThread) {        /* no thread given - start one */
            if (!(pOThread = lt_createobject(LTOThread))) break;
            static const char kDefaultThreadName[] = "LEDFader";
            char *pThreadName = lt_malloc(kLTThread_MaxNameBuff);
            if (pThreadName) {
                lt_strncpyTerm(pThreadName, kDefaultThreadName, kLTThread_MaxNameBuff);
                if (sizeof(kDefaultThreadName) < kLTThread_MaxNameLen)
                    lt_strncpyTerm(pThreadName + sizeof(kDefaultThreadName) - 1, pGroupName, kLTThread_MaxNameBuff - sizeof(kDefaultThreadName));
            }
            pOThread->API->SetStackSize(pOThread, 768);
            pOThread->API->Start(pOThread, pThreadName ? pThreadName : kDefaultThreadName, NULL, NULL);
            pThis->bThreadIsMine = true;
        }
        pThis->thread = pOThread;
        pThis->nLEDIndex = nLEDIndex;
        pThis->transitionStepInterval = interval;
        pThis->thread->API->QueueTaskProc(pThis->thread, RegisterForEvents, NULL, pThis);
        return true;
    } while (0);
    lt_destroyobject(pThis->thread);
    lt_destroyhandle(pThis->hLED);
    lt_destroyobject(pThis->mutex);
    return false;
}

/* LTDeviceLEDFader API ****************************************************************************************************************/

static void LTDeviceLEDFaderImpl_Fade(LTDeviceLEDFaderImpl *pThis, u32 nColor, LTTime transition, LTDeviceLEDFader_FadeCompleteProc *pFadeCompleteProc, void *pClientData) {
    pThis->mutex->API->Lock(pThis->mutex); /* If any of the timer procs find the mutex held, they do not continue the sequence. */
    KillTimers(pThis);
    pThis->pFadeCompleteProc = pFadeCompleteProc;
    pThis->pClientData = pClientData;
    if (!CreateSingleFade(pThis)) {
        LTLOG_YELLOWALERT("fade.single.oom", NULL);
        DestroySingleFade(pThis);
        NotifySingleFadeComplete(pThis, false);
        pThis->mutex->API->Unlock(pThis->mutex);
        return;
    }
    /* Dwell time is not used in a single-step fade; this special value thereof is used to signal HoldColor() to release the memory of
       the step when the step is complete.  The value of LT_U16_MAX is RESERVED for use by this function only.
       This CAST AWAY CONST in the special case of a single fade created by CreateSingleFade() above: */
    *((LTDeviceLEDFaderTimingStep *)pThis->pNextStep) = (LTDeviceLEDFaderTimingStep){ .color = nColor, .dwell = LT_U16_MAX,
                                                                                      .transition = LTTime_GetMilliseconds(transition) };
    pThis->thread->API->QueueTaskProc(pThis->thread, &BeginNextStep, NULL, pThis);
    pThis->mutex->API->Unlock(pThis->mutex);
}

static void LTDeviceLEDFaderImpl_StartFadeSequence(LTDeviceLEDFaderImpl *pThis, const LTDeviceLEDFaderTimingStep *pStepList, u16 nTimingStepCount, LTDeviceLEDFader_FadeSequenceStepCompleteProc *pStepCompleteProc, LTDeviceLEDFader_FadeSequenceCompleteProc *pSequenceCompleteProc, void *pClientData) {
    pThis->mutex->API->Lock(pThis->mutex); /* If any of the timer procs find the mutex held, they do not continue the sequence. */
    KillTimers(pThis);
    pThis->pNextStep = pThis->pTimingSteps = pStepList;
    pThis->nNumStepsRemaining = pThis->nStepCount = nTimingStepCount;
    pThis->pFadeSequenceStepCompleteProc = pStepCompleteProc;
    pThis->pFadeSequenceCompleteProc = pSequenceCompleteProc;
    pThis->pClientData = pClientData;
    /* LT_U16_MAX is reserved for dwell times of single steps generated by Fade(); it is s signal to HoldColor() to release
       the memory of the step when the step is complete.  It cannot exist in any steps given by the client. */
    for (; nTimingStepCount; --nTimingStepCount, ++pStepList) if (pStepList->dwell == LT_U16_MAX) {
        LTLOG_YELLOWALERT("fade.seq.dwell", "Illegal value %u in sequence step %u", LT_U16_MAX, pThis->nStepCount - nTimingStepCount);
        NotifyFadeSequenceComplete(pThis, false);
        pThis->mutex->API->Unlock(pThis->mutex);
        return;
    }
    pThis->thread->API->QueueTaskProc(pThis->thread, &BeginNextStep, NULL, pThis);
    pThis->mutex->API->Unlock(pThis->mutex);
}

/***********************************************************************************************************************
 * Library startup and shutdown.
 * Attempt to open Driver Libraries. If no drivers available, close the Driver Library and return false.
 * Otherwise, the Device is open successfully - return true.                                                          */

static void LTDeviceLEDImpl_LibFini(void) {
    LTLOG_DEBUG("fini", NULL);
    UnloadDriverLibraries(&s_DriverLibraries);
}

static bool LTDeviceLEDImpl_LibInit(void) {
    LTLOG_DEBUG("init", NULL);
    if (!LoadDriverLibraries("LTDeviceLED", &s_DriverLibraries)) { LTDeviceLEDImpl_LibFini(); return false; }
    return true;
}

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceLED)
    .GetGroupDescriptorFromUnitNumber = LTDeviceLEDImpl_GetGroupDescriptorFromUnitNumber,
    .GetUnitNumberFromGroupName       = LTDeviceLEDImpl_GetUnitNumberFromGroupName
LTLIBRARY_DEFINITION;

define_LTObjectImplPublic(LTDeviceLEDFader, LTDeviceLEDFaderImpl, Initialize, Fade, StartFadeSequence)

LTLIBRARY_EXPORT_INTERFACES(LTDeviceLED, (LTDeviceLEDFaderImpl));

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-Jan-21   constantine created
 *  12-Apr-23   constantine Convert to Device Config
 */
