# LT OS Threading: A Task-Dispatch Model Built for Embedded Reality

LTThread is not just a wrapper around a platform thread — it is a complete thread execution model built around a structured dispatch loop. Every LTThread runs a scheduler-driven loop that processes queued **TaskProcs**, fires **timers**, and delivers **event notifications**, then re-enters a wait state. This design gives LT threads a richer contract than either POSIX threads or FreeRTOS tasks, without adding overhead to threads that don't use it.

---

## LTThread vs. POSIX Threads

POSIX threads (`pthreads`) are a low-level substrate: they give you a thread function, a mutex, a condition variable, and little else. Everything beyond that — graceful shutdown, timer dispatch, resource lifecycle, per-thread diagnostics — is left to the application to implement, correctly, every time.

| | POSIX threads | LTThread |
|---|---|---|
| **Execution model** | User-written `while(1)` loop | Built-in dispatch loop: TaskProcs + timers + events |
| **Timers** | Separate `timer_create` API, system-global | **Per-thread `SetTimer` / `SetTimerAbsolute`; fire in thread context** |
| **Deadline timers** | Not available | **`SetTimerAbsolute`: deadline-anchored, self-realigning on missed deadlines** |
| **Graceful shutdown** | `pthread_cancel` (not graceful) | **`Terminate()` → `IsTerminatePending()` → `WaitUntilFinished()` protocol** |
| **Resource lifecycle** | Manual | **`ClientDataReleaseProc`: guaranteed reclaim whether TaskProc ran or was purged** |
| **Mutex priority inversion** | Opt-in attribute, often omitted | **Always-on in `LTMutex`; low-priority holder elevated automatically** |
| **Recursive mutex** | Opt-in `PTHREAD_MUTEX_RECURSIVE` | **`LTMutex` is always nestable** |
| **Idempotent wake-up** | Not available | **`QueueTaskProcIfRequired`: at-most-one pending copy in queue** |
| **Thread-local storage** | Integer keys (`pthread_key_t`) | **String-keyed with `ClientDataReleaseProc` callbacks** |
| **Stack diagnostics** | Not available | **Current usage + high-water mark via `GetStackUsage()`** |
| **Per-thread heap tracking** | Not available | **Current + high-water mark via `GetHeapUsage()`** |
| **Thread state visibility** | Not available | **9 queryable states: `NotStarted` → `WaitBlocked` → `TerminatePending` → `Terminated`** |
| **Runtime thread snapshot** | Not available | **`SnapshotRunningThreads()`: coherent `ps`-style view of all threads** |
| **ISR-safe queueing** | No ISR concept in POSIX | **`QueueTaskProc` and `QueueTaskProcIfRequired` are `LT_ISR_SAFE`** |
| **ISR-to-thread events** | No ISR abstraction | **`NotifyEventFromISR()` via system proxy thread** |
| **Power management** | Not available | **`SetAsWakeupTimer()`: timer can wake system from low-power sleep** |

The `ClientDataReleaseProc` pattern deserves particular attention. When a TaskProc and its associated data are queued to a thread, thread shutdown before the TaskProc executes is a common race. LT solves this structurally: the release proc is *always* called — with a `ReleaseReason` indicating whether the TaskProc completed, was purged due to shutdown, or was rejected because the queue was full — so no handed-off resource can be silently lost. POSIX offers no equivalent.

---

## LTThread vs. FreeRTOS Tasks

FreeRTOS tasks are also user-written `while(1)` loops. FreeRTOS provides software timers as a separate daemon task and queues as untyped byte buffers, leaving structured inter-task communication and resource management to the application.

| | FreeRTOS | LTThread |
|---|---|---|
| **Execution model** | User `while(1)` loop | **Structured dispatch loop: TaskProcs + timers + events** |
| **Priority levels** | Configurable (default 5, up to 32+) | **Fixed 31 levels; always available** |
| **Priority inheritance** | Opt-in (mutex type) | **Always on in `LTMutex`** |
| **Timers** | Separate Timer Service Task | **Per-thread, fire in owner thread's context** |
| **Deadline timers** | Not available | **`SetTimerAbsolute`: anchored deadline, catches up after preemption** |
| **Graceful shutdown** | `vTaskDelete()` (immediate, not graceful) | **`Terminate()` + shutdown sequence: cancel timers → purge queue → call `ExitProc`** |
| **Idempotent wake-up** | Not available | **`QueueTaskProcIfRequired`** |
| **Resource lifecycle** | Manual | **`ClientDataReleaseProc`** |
| **Thread-local storage** | Void pointer array by integer index | **String-keyed with release callbacks** |
| **Stack high-water mark** | `uxTaskGetStackHighWaterMark()` | **Stack + heap, both current and high-water, always available** |
| **Runtime snapshot** | `vTaskGetRunTimeStats()` (compile-time opt-in) | **`SnapshotRunningThreads()` always available; includes CPU time, stack, heap, state** |
| **ISR-safe API** | Separate `FromISR` functions with different signatures | **Same API; `LT_ISR_SAFE` annotation marks safe entry points** |
| **Event system** | Untyped queues / event groups | **`LTEvent`: typed, multi-subscriber, dispatched in each receiver's own thread context** |
| **Power management** | Tickless idle (global) | **`SetAsWakeupTimer()`: per-timer wakeup control** |

The `LTEvent` system is a standout differentiator at the system level. Where FreeRTOS inter-task communication uses untyped byte queues that require each side to agree on a layout, LTEvent dispatches strongly typed callbacks directly in each registered receiver's thread context. A notifier calls `NotifyEvent(hEvent, arg1, arg2, ...)` once; each registered subscriber receives the callback with the correct typed arguments in its own thread — no queue buffer sizing, no type casting, no polling.

---

## Summary

LTThread shifts embedded threading from a raw scheduling primitive to a structured execution contract. The task-queue dispatch model, deterministic `ClientDataReleaseProc` lifecycle, always-on priority-inheriting mutexes, built-in per-thread timers with deadline support, and the `LTEvent` typed publish-subscribe system together eliminate whole categories of bugs — resource leaks at shutdown, priority inversion, missed wake-ups, silent queue overflow — that embedded developers routinely encounter and manually guard against in both POSIX and FreeRTOS environments.
