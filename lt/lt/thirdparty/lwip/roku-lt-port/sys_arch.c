#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/system/crypto/LTSystemCrypto.h>

#include "lwipopts.h"
#include <lwip/sys.h>
#include <lwip/timeouts.h>
#include <lwip/def.h>

static LTCore         *s_Core;
static ILTThread      *s_pThread;
static LTSystemCrypto *s_pSystemCrypto;

static LTMutex         *s_hMutex;
static LTThread        s_hMutexOwner;
static u32             s_mutexDepth;
static LTThread        s_hNetworkThread;

#if (! defined LWIP_DEBUG)
#define LWIP_DEBUG 0
#endif

#if LWIP_DEBUG
const char *lwip_strerr(err_t err) {
    static const char * const lut[] = {
        "OK",
        "MEM",
        "BUF",
        "TIMEOUT",
        "RTE",
        "INPROGRESS",
        "VAL",
        "WOULDBLOCK",
        "USE",
        "ALREADY",
        "ISCONN",
        "CONN",
        "IF",
        "ABRT",
        "RST",
        "CLSD",
        "ARG",
    };

    return lut[-err];
}
#endif // LWIP_DEBUG

void
lt_lwip_sys_init(void) {
    s_Core = LT_GetCore();
    s_pThread = lt_getlibraryinterface(ILTThread, s_Core);
    s_pSystemCrypto = lt_openlibrary(LTSystemCrypto);

    s_hMutex = lt_createobject(LTMutex);
    s_hNetworkThread = s_pThread->GetCurrentThread();
}

void
lt_lwip_sys_destroy(void) {
    lt_destroyobject(s_hMutex);
    s_hMutex = NULL;
    lt_closelibrary(s_pSystemCrypto);
}

void
lt_lwip_lock(void) {
    s_hMutex->API->Lock(s_hMutex);
    if (s_mutexDepth++ == 0) {
        s_hMutexOwner = s_pThread->GetCurrentThread();
    }
}

void
lt_lwip_unlock(void) {
    LT_ASSERT(s_mutexDepth > 0);
    if (--s_mutexDepth == 0) {
        s_hMutexOwner = 0;
    }
    s_hMutex->API->Unlock(s_hMutex);
}

void
lt_lwip_check_locking(const char *func) {
    (void)func;
    if (s_hMutex->API->TryLock(s_hMutex)) {
        LT_ASSERT(s_hMutexOwner == s_pThread->GetCurrentThread());
        s_hMutex->API->Unlock(s_hMutex);
    } else {
        LT_ASSERT(0);
    }
}

u32
lt_lwip_rand(void) {
    u32 retVal = 0;
    if (! (s_pSystemCrypto && s_pSystemCrypto->GenRandomBytes((u8 *)&retVal, sizeof(retVal)))) {
        /* should never happen, engage backup plan just in case */
        retVal = (u32)LTTime_GetMicroseconds(s_Core->GetKernelTime());
    }
    return retVal;
}

// LwIP's regular timers take args (similar to LT timers)
// LwIP's cyclic timers take no arguments so have to be wrapped with
// the following function:
static void
cyclic_timer_handler(void *func) {
    lwip_cyclic_timer_handler timer_handler = (lwip_cyclic_timer_handler)func;
    lt_lwip_lock();
    timer_handler();
    lt_lwip_unlock();
}

void
sys_timeouts_init(void) {
    for (int i=0; i < lwip_num_cyclic_timers; i++) {
        const struct lwip_cyclic_timer *tmr = (lwip_cyclic_timers + i);
        s_pThread->SetTimer(s_hNetworkThread,
                            LTTime_Milliseconds(tmr->interval_ms),
                            cyclic_timer_handler, NULL,
                            (void *)tmr->handler);
    }
}

void
sys_timeouts_destroy(void) {
    for (int i=0; i < lwip_num_cyclic_timers; i++) {
        const struct lwip_cyclic_timer *tmr = (lwip_cyclic_timers + i);
        s_pThread->KillTimer(s_hNetworkThread,
                             cyclic_timer_handler,
                             (void *)tmr->handler);
    }
}
