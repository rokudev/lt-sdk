# LTCore — Differentiators and Usage Guide

## Overview

LTCore is the operating system kernel of LT OS — a self-contained, zero-libc, platform-independent RTOS kernel delivering threading, synchronization, memory management, event dispatch, time, library management, and ISR services in a single dynamically-loadable library. It runs identically across ESP32, Bouffalo BL70x, Anyka AK3918x, ST H755, Linux x86, and Linux ARM without conditional compilation anywhere in application code.

---

## Common Architecture Differentiator

Both FreeRTOS and Linux expose their kernel services through C function calls bound at link time to platform-specific implementations. LTCore exposes its entire API through a single C++ pure-virtual function-table struct (`LTCoreApi`) resolved at runtime by the LT library manager. Every service — threads, mutexes, timers, memory, events, ISRs — is reachable through one pointer, `LT_GetCore()`, and the same call sequence compiles and runs on every supported platform with no `#ifdef` branching. This is the structural differentiator that makes every subsequent feature portable by construction.

---

## Feature-by-Feature Differentiators

### LTThread — Event-Driven Task Queue Thread

FreeRTOS tasks are infinite loops that block on RTOS primitives. Linux threads use `pthread` with blocking I/O and condition variables. LTThread introduces a third model: a **task queue thread** that wakes to process queued `LTThread_TaskProc` functions, then returns to a wait state. Key differentiators:

- **Resource ownership handoff with `LTThread_ClientDataReleaseProc`**: Every `QueueTaskProc` call accepts an optional release proc that is **guaranteed** to be called exactly once — either after the TaskProc executes, or (with the purge reason) if the thread is terminated before the proc runs. This eliminates the resource-leak class entirely: heap buffers, pool items, and reference counts are reclaimed deterministically without try/catch or shared_ptr.
- **`QueueTaskProcIfRequired`**: deduplicate: if a matching `(proc, clientData)` triplet is already in the queue, skip the enqueue — one TaskProc invocation services all pending updates. This is ISR-safe.
- **Thread-local storage by string key** (`SetThreadSpecificClientData`): any library can associate named data with any thread without preallocated slots.
- **Two timer types**: `SetTimer` (relative, repeating, with catch-up) and `SetTimerAbsolute` (deadline-periodic, re-aligns after missed deadlines). Both use `(proc, clientData)` identity for update-or-create semantics. `SetAsWakeupTimer` can arm a timer to wake the system from deep sleep.
- **`SnapshotRunningThreads`**: synchronous callback with stack-size, stack-current, stack-max, heap-current, heap-max, run-time, and state for every thread — without a dedicated monitor task.
- **31 priorities** (0–30), **`StartSynchronous`** for blocking until InitProc completes, and **`WaitUntilFinished`** with timeout.

FreeRTOS has equivalent basic threading but no task-queue model, no guaranteed release proc, no deadline timers, and no heap-usage-per-thread tracking.

```c
// Create, start, queue work, and tear down a thread
LTCore *core = LT_GetCore();
ILTThread *iThread = lt_getlibraryinterface(ILTThread, core);
LTThread hThread = core->CreateThread("sensor");
iThread->SetStackSize(hThread, 4096);
iThread->SetPriority(hThread, 5);
iThread->Start(hThread, myInitProc, myExitProc);

// Queue a task from any thread (or ISR)
typedef struct { u8 *pBuf; u32 len; } SensorData;
SensorData *pData = lt_malloc(sizeof(SensorData));
iThread->QueueTaskProc(hThread, processSensorData, freeClientData, pData);
// processSensorData runs in hThread context; freeClientData frees pData regardless

// Periodic 100ms timer
iThread->SetTimer(hThread, LTTime_Milliseconds(100), myTimerProc, NULL, NULL);

// Snapshot all threads
iThread->SnapshotRunningThreads(mySnapshotCb, NULL);

// Graceful shutdown
iThread->Terminate(hThread);
iThread->WaitUntilFinished(hThread, LTTime_Seconds(5));
core->DestroyHandle(hThread);
```

### LTEvent — Typed, Thread-Correct Asynchronous Notification

FreeRTOS uses queues or semaphores for inter-task notification — type-erased, polling-oriented. Linux uses signals, condition variables, or eventfd — none deliver typed callbacks in the receiver's own thread context. LTEvent delivers typed function arguments directly into the receiver's registered callback, in the receiver's own thread, using a `DispatchProc` that the event creator supplies. Key differentiators:

- **Typed delivery**: the event creator describes argument types via `LTArgsDescriptor`; `NotifyEvent` accepts varargs; receivers get a concrete, typed callback with the correct function signature. No casting, no manual deserialization.
- **Immediate state delivery** (`bNotifyEventStateImmediately`): on registration, the event creator's `NotifyImmediateEventStateProc` fires synchronously to deliver the current state — avoiding the race between "subscribe" and "first notification" that plagues all pub/sub systems.
- **`DispatchCompleteProc`**: fires in the notifier's thread when all receivers have processed the event, allowing the notifier to reclaim event argument memory deterministically.
- **`NotifyEventFromISR`**: schedules event notification on a system proxy thread from an ISR context, bridging the ISR-to-event-system gap without a separate wakeup mechanism.
- **Any-thread registration**: `RegisterThreadForEvent` lets one thread register another for event delivery — useful for device drivers routing events to application threads.

```c
// Event creator
static LTArgsDescriptor s_argDesc = { kLTArgType_u32, kLTArgType_u32, kLTArgType_None };
static void MyDispatch(LTEvent hEvent, void *pProc, LTArgs *pArgs, void *pClientData) {
    ((void(*)(u32, u32, void*))pProc)(
        LTArgs_GetU32(pArgs, 0), LTArgs_GetU32(pArgs, 1), pClientData);
}
LTEvent hTempEvent = LT_GetCore()->CreateEvent(&s_argDesc, MyDispatch, NULL, myStateProc, myState);

// Receiver registers in its own thread context; gets current state immediately
ILTEvent *iEvent = lt_gethandleinterface(hTempEvent, ILTEvent);
iEvent->RegisterForEvent(hTempEvent, myTempCallback, myReleaseProc, myCtx, true);

// Notifier fires; myTempCallback runs in receiver's thread
iEvent->NotifyEvent(hTempEvent, (u32)85, (u32)60); // temp=85, humidity=60
```

### LTMutex — Priority-Inversion-Safe, Recursive

FreeRTOS mutexes support priority inheritance but require explicit task-notification setup. Linux `pthread_mutex_t` with `PTHREAD_PRIO_INHERIT` is close, but requires libc. LTMutex is recursive (nestable), priority-inversion-safe by construction (the holder is elevated to the waiter's priority), and uses `LTObject` lifecycle management — destroyed with `lt_destroyobject`.

```c
LTMutex *mutex = lt_createobject(LTMutex);
mutex->API->Lock(mutex);
    // protected section
mutex->API->Unlock(mutex);
// TryLock for non-blocking:
if (mutex->API->TryLock(mutex)) {
    // got it
    mutex->API->Unlock(mutex);
}
lt_destroyobject(mutex);
```

### LTMonitor — ISR-Notify-Safe Condition Variable

FreeRTOS has no direct condition-variable equivalent without complex task-notification patterns. Linux uses `pthread_cond_t`. LTMonitor provides an Enter/Wait/Notify/Exit monitor whose `Notify` is explicitly ISR-safe (naked notify, no Enter/Exit required from ISR context). This is the canonical way to port vendor driver code that spins on an OS semaphore. When possible, the preferred pattern is `QueueTaskProcIfRequired` instead (noted inline in the header).

```c
// Driver ISR wakes driver thread via LTMonitor
static LTMonitor *s_monitor;
bool DriverThread_InitProc(void) {
    s_monitor = lt_createobject(LTMonitor);
    while (s_bRunning) {
        s_monitor->API->Enter(s_monitor);
        s_monitor->API->Wait(s_monitor, LTTime_Infinite());
        processHardwareData();
        s_monitor->API->Exit(s_monitor);
    }
    lt_destroyobject(s_monitor);
    return false;
}
void DriverISR(void) {
    readHardwareRegisters();
    s_monitor->API->Notify(s_monitor);  // naked notify from ISR
}
```

### LTCountingSemaphore — ISR-Safe Counting Semaphore

FreeRTOS has counting semaphores. Linux uses `sem_t`. LTCountingSemaphore adds: a max-count bound (`Signal` is a no-op at ceiling), an explicit `SignalFromThread` that guarantees no lost wakeup between predicate check and `Wait`, and ISR-safe `Signal` and `TryWait` using CAS loops — no monitor interaction in the hot ISR path. It consolidates the pattern previously implemented independently in WiFi, BLE, and UART OS adaptation layers.

```c
LTCountingSemaphore *sem = lt_createobject(LTCountingSemaphore);
sem->API->Init(sem, 10, 0);     // max=10, initial=0

// ISR context: signal
sem->API->Signal(sem);

// Thread context: blocking wait with timeout
if (sem->API->Wait(sem, LTTime_Milliseconds(500))) {
    // acquired
}
lt_destroyobject(sem);
```

### LTSpinLock — SMP-Safe ISR/Thread Spinlock

FreeRTOS spinlocks exist for SMP ports but are not portable across single-core/multi-core targets. Linux `raw_spinlock_t` is kernel-internal. LTSpinLock disables interrupts on the current core before acquiring; on a single-core system this collapses to disable/enable with no spin; on SMP it spins with interrupts disabled until the other core releases. This makes the same driver code correct on both topologies without conditional compilation.

### LTTime — Nanosecond Type-Safe Time

FreeRTOS time is `TickType_t` — a unit-less integer that varies with `configTICK_RATE_HZ`. Linux `struct timespec` carries nanoseconds in two separate fields. LTTime is a **strongly-typed s64 nanoseconds** value that the compiler refuses to implicitly convert to/from integers, eliminating unit mismatch bugs. All operations (`LTTime_Add`, `LTTime_Subtract`, `LTTime_Milliseconds`, comparisons) are ISR-safe inline functions. `LTTimeBase` solves the clock-setting race: capture both kernel time and UTC offset atomically, then `SetClockTimeBaseUTC` stores both — no post-calculation drift even under context switches or heavy load.

```c
LTTime elapsed = LTTime_Subtract(core->GetKernelTime(), startTime);
if (LTTime_IsGreaterThan(elapsed, LTTime_Milliseconds(500)))
    lt_consoleprint("Took %lld ms\n", LTTime_GetMilliseconds(elapsed));

// Accurate UTC: capture reference point atomically
LTTimeBase base = { .primaryClockTime = core->GetKernelTime(),
                    .secondaryClockTime = fetchedUTCFromNTP };
core->SetClockTimeBaseUTC(&base);
LTTime utc = core->GetClockTimeUTC();   // always accurate, no drift
```

### Memory Management — Region-Aware, Per-Thread Tracked

FreeRTOS `pvPortMalloc` is a single-heap allocator with no debug hooks. Linux `malloc` is libc. LTCore's allocator (`lt_malloc`, `lt_realloc`, `lt_free`) adds:

- **Named memory regions** (`lt_malloc_from_region`): allocate from BSP-registered heaps (e.g., DMA-only SRAM). Region name is looked up once at startup via `GetNamedMemoryRegion`; free via `lt_free` — region is recorded in the block header.
- **Per-thread heap tracking**: `GetHeapUsage` reports current and high-water heap for any thread.
- **Heap enumeration** (`EnumerateHeapAllocatedBlockInfo`): filter by thread or tag; enumerate every live allocation with callsite (file/line) in debug builds.
- **Callsite macros**: `lt_malloc`/`lt_free` embed `__FILE__`/`__LINE__` into every allocation header (configurable, zero-overhead in release).
- **`SnapshotMemstat`**: 64-bit packed memstat (current/max/total with unit scaling) for at-a-glance logging without allocation.

```c
// Named DMA region allocation
LTMemoryRegion dma = core->GetNamedMemoryRegion("wifi_dma");
void *p = lt_malloc_from_region(dma, 4096);
lt_free(p);   // region inferred from block header

// Heap stats snapshot
u64 stat = core->SnapshotMemstat();
char buf[48];
core->FormatCanonicalMemstatString(stat, buf, sizeof(buf), true);
lt_consoleprint("%s\n", buf);  // e.g. "[134.24k/256.00k used, 185.65k hi]"

// Enumerate all live allocations on the current thread
core->EnumerateHeapAllocatedBlockInfo(core->GetCurrentThread(), 0, myEnumCb, NULL);
```

### Library Manager — Dynamic Open/Close/Hook/Mock/Snapshot

FreeRTOS has no library management concept. Linux uses `dlopen`/`dlsym`. LTCore's library manager adds:

- **Reference-counted open/close**: `lt_openlibrary` / `lt_closelibrary`; library init/fini called exactly once.
- **Library hooks** (`SetLibraryHook`): intercept open/close with SubstituteOpen (replace entire library), AfterOpen (patch vtable), or BeforeClose (cleanup). Used by the test framework for mock injection.
- **`MockObject`/`UnmockObject`**: temporarily redirect `lt_createobject` to a mock specialization for unit tests without modifying production code.
- **Snapshot enumeration** (`SnapshotOpenLibraries`): iterate all open libraries with their build version strings.
- **Handle introspection** (`GetHandlesByInterface`, `GetHandleStateString`, `IsHandleValid`, `ReserveHandlePrivateData`/`ReleaseHandlePrivateData`): handles track destroy-pending state and block destruction while reserved.

```c
// Library open/close
LTDeviceWiFi *wifi = lt_openlibrary(LTDeviceWiFi);
// ... use wifi ...
lt_closelibrary(wifi);

// Unit test: mock a sensor
core->MockObject("LTDeviceImageSensor", NULL, "MockImageSensor");
LTDeviceImageSensor *sensor = lt_openlibrary(LTDeviceImageSensor); // gets mock
core->UnmockObject(NULL, NULL);  // restore all

// Snapshot open libraries
core->SnapshotOpenLibraries(myLibSnapshotCb, NULL);
```

### Low-Power Sleep — Cooperative, Thread-Idle Triggered

FreeRTOS tickless idle is a single hook. Linux has no embedded sleep model. LTCore's sleep model: set `EnterSleepModeProc` (called by LTCore in a highest-priority thread when all application threads are idle for the configured idle delay). Multiple libraries can issue **sleep disallowance grants** (`DisallowSleepMode` / `ReallowSleepMode`). Any thread can register a `SleepActionEventProc` that returns a required-wakeup kernel time, letting subsystems (e.g., a heartbeat library) schedule wakeups without owning the sleep entry point.

```c
// BSP registers sleep entry
core->SetEnterSleepModeProc(myEnterSleepProc, myCtx);
core->SetEnterSleepModeIdleDelay(LTTime_Seconds(5));

// Library prevents sleep while transfer in progress
u32 grant = core->DisallowSleepMode();
doTransfer();
core->ReallowSleepMode(grant);
```

### LTList — ISR-Safe Intrusive Doubly-Linked List

FreeRTOS has no standard intrusive list. Linux `list_head` is kernel-internal. LTList embeds a `LTList_Node` in any struct; all operations (`InsertHead`, `AddTail`, `InsertBefore`, `Remove`, `IsEmpty`) are ISR-safe inline functions. `LTList_ForEach` is a deletion-safe iteration macro. Zero dynamic allocation — the node is the struct member.

```c
typedef struct { u32 id; LTList_Node node; } Item;
static LTList s_queue;
LTList_Init(&s_queue);

Item a = { .id = 1 }; LTList_AddTail(&s_queue, &a.node);
Item b = { .id = 2 }; LTList_AddTail(&s_queue, &b.node);

LTList_ForEach(pNode, &s_queue) {
    Item *pItem = LT_CONTAINER_OF(pNode, Item, node);
    lt_consoleprint("id=%u\n", pItem->id);
} LTList_EndForEach;
```

### LTArray / LTAssociativeArray — Dynamic Collections

FreeRTOS has no dynamic container. Linux uses `glib` or manually-rolled structures (libc-dependent). LTArray supports pointer arrays and struct arrays, binary search on sorted arrays, `InsertSorted`, and a `List` implementation for O(1) insert/delete. `LTAssociativeArray` provides arbitrary-key → arbitrary-value mapping (struct or pointer), with enumeration callback and variable key-size hint for allocation tuning. Both use `lt_malloc` internally — no libc dependency.

```c
// Struct array
LTArray *arr = LTArray_CreateStructArray(sizeof(MyRecord));
MyRecord r = { .id = 42 };
arr->API->Append(arr, &r);
arr->API->Sort(arr, myCompare, NULL);
s32 idx = arr->API->Find(arr, myCompare, &r, NULL);
lt_destroyobject(arr);

// String-keyed associative array
LTAssociativeArray *map = lt_createobject(LTAssociativeArray);
LTCStringKeyedArray_Set(map, "version", (void*)(uintptr_t)3);
u32 ver = (u32)(uintptr_t)LTCStringKeyedArray_Get(map, "version", NULL);
lt_destroyobject(map);
```

### LTStdlib — Portable, Reentrant Standard Library Subset

FreeRTOS typically links against a vendor libc or newlib. Linux uses glibc. LTStdlib provides an `lt_`-prefixed curated set — `lt_memcpy`, `lt_snprintf` (vsnprintf-based, ISR-safe), `lt_qsort` (with client data), `lt_bsearch`/`lt_bsearchIndex` (returning pointer or index), `lt_strdup`, and the full `ltstring_` suite (`LTString` is a `char *` that auto-grows via `ltstring_set`, `ltstring_format`, `ltstring_append`, `ltstring_insert`, `ltstring_stripwhitespace`). All are dispatched through a single `LT_GetStdlib()` vtable — implementation can be updated without relinking application code.

```c
LTString s = ltstring_create("Hello");
ltstring_appendformat(&s, ", world %d!", 2026);  // auto-grows
lt_consoleprint("%s\n", s);
ltstring_destroy(s);
```

### LTProbe — Zero-Overhead Performance Probe

FreeRTOS and Linux have profiling tools, but none integrated with the RTOS thread model at this level. LTProbe preallocates all memory at create time (no allocation in the hot path), captures kernel time, stack usage, heap usage, and a tag string per probe using atomics (thread-safe multi-thread capture), and prints a formatted columnar report with per-interval elapsed and cumulative times.

```c
LTProbe *probe = LTProbe_Create("WiFi Join", 8, true);
LTProbe_CaptureProbe(probe, "Start Scan");
// ... scan ...
LTProbe_CaptureProbe(probe, "Scan Done");
// ... associate ...
LTProbe_CaptureProbe(probe, "Associated");
LTProbe_ConsolePrintReport(probe);
LTProbe_Destroy(probe);
// Output: kernel time, elapsed, cumulative, stack size/curr/max, heap curr/max per probe
```

### LTCrashdump — Structured Crash Data

FreeRTOS has no standardized crash data format. Linux uses core files. LTCrashdump defines a typed binary format (`LTCrashdumpHeader` + per-context `LTCrashdumpContext` with register state + stack dump + software version string) written to a flash partition via an `LT_ISR_SAFE` callback during fault. A corresponding log backup (`LTLogBackupHeader`) preserves the logger ring buffer. The magic number, version field, and ABI string allow tooling to decode crashes across firmware versions.

### LTSecurity — Security Token and Crypto Channel Framework

FreeRTOS has no security framework. Linux uses OpenSSL/mbedTLS. LTCore defines the `LTSecurity_Info` interface (vendor-agnostic) for: hardware RNG, OTP read/write, AES-128-ECB/CBC, SHA-256, Ed25519, and DMA-capable crypto channels. The `LTSecurityLTAT` (LT Authentication Token) structure defines a signed, timestamped claim token with four 32-bit claim groups — used by `LTDeviceIdentity` for production authorization without exposing raw keys.

---

## ISR Safety — A Unified Contract

Every LTCore function that can be called from an ISR is annotated `LT_ISR_SAFE` in the header. This is enforced by the type system on platforms that support ISR-safe annotations. The annotation covers: `GetKernelTime`, `FormatCanonicalTimeString`, `InsideInterruptContext`, `InterruptsAreDisabled`, `Disable`/`Enable`, `QueueTaskProc`, `QueueTaskProcIfRequired`, `LTMonitor::Notify`, `LTCountingSemaphore::Signal`/`TryWait`, `LTList_*`, and `LTSpinLock::Lock`/`Unlock`. FreeRTOS requires separate `FromISR` suffixed functions; LTCore unifies the API with type-annotated safety guarantees.

---

## Summary

LTCore delivers a complete RTOS kernel — threads, timers, events, mutexes, semaphores, spinlocks, memory (with region awareness and per-thread tracking), library management (with mocking and hooking), a self-contained standard library, intrusive collections, profiling, crash dump, and security — in a single library with a zero-libc, zero-conditional-compilation API that runs identically on embedded and hosted targets. The task-queue thread model, guaranteed client-data release proc, typed event delivery with immediate-state notification, and `LTTime` type safety are the most distinctive differentiators relative to both FreeRTOS and Linux.
