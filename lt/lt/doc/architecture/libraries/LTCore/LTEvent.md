# LT OS Asynchronous Events: Benefits and Differentiators

## Overview

LT OS provides a first-class asynchronous event model (`LTEvent`) as a core OS primitive. Where Linux and FreeRTOS require application code to assemble multi-component patterns for async notification, LT OS delivers the complete pattern — type-safe callbacks, thread-affine dispatch, multi-subscriber fan-out, ISR-to-thread bridging, and lifecycle-safe client data handoff — in a single, unified interface that works identically across all LT target platforms.

---

## Thread-Affine Callback Dispatch

**LT OS:** Event callbacks execute in the subscriber's own thread context, automatically. The caller of `NotifyEvent()` never blocks waiting for receivers; the event system dispatches to each receiver's thread queue independently. Callers receive ISR-safety annotations (`LT_ISR_SAFE`) in the API surface itself, so thread-safety contracts are enforced at the type level.

**Linux:** `signalfd`, `eventfd`, `inotify`, and `epoll` all require the subscriber to own a polling loop. Delivering typed arguments to a callback in a specific thread requires the application to build its own dispatch table, argument serialization, and thread-wakeup machinery — typically via pipes, message queues, or condition variables.

**FreeRTOS:** Event groups and task notifications wake a task but carry no typed payload. Passing structured data across a task boundary requires a separate `xQueueSend` and a receiver-side `xQueueReceive` poll, with application-managed serialization. There is no native mechanism to guarantee delivery to a specific task context with arbitrary typed arguments.

---

## Strongly Typed, Zero-Copy Argument Delivery

**LT OS:** Event arguments are described at creation time via `LTArgsDescriptor`. When `NotifyEvent()` is called with varargs, the event system captures those arguments into a heap-accessible `LTArgs` structure, traverses the registered receiver list, and for each receiver invokes the creator-supplied `LTEvent_DispatchProc` in that receiver's thread context, unpacking `LTArgs` back into concrete typed C function parameters. Receivers see a normal function call with the exact signature declared for the event — no casting, no manual deserialization.

**Linux:** `signalfd`/`eventfd` carry at most a 64-bit integer. Structured data requires a socket or message queue with application-defined serialization. Callback dispatch with typed arguments must be implemented from scratch, typically involving `pthread_cond_signal` and shared memory protected by a mutex.

**FreeRTOS:** Task notifications carry a single 32-bit value. Queue messages carry a fixed-size opaque byte block defined at queue-creation time; the receiver casts the block manually. Typed argument dispatch to a named callback function in a specific task does not exist as a primitive.

---

## Immediate State Notification on Registration

**LT OS:** When registering for an event, a subscriber may pass `bNotifyEventStateImmediately = true`. If the event creator supplied a `LTEvent_NotifyImmediateEventStateProc`, the current event state is delivered synchronously to the new subscriber before `RegisterForEvent` returns. This closes the registration-race window: a subscriber that starts after an event has already fired does not miss the initial state.

**Linux:** Missed-notification races are common. An `epoll` listener that registers after an `eventfd` has already been written must separately query current state through a different mechanism or risk starting in a stale or unknown condition. No standard facility provides synchronous state replay on registration.

**FreeRTOS:** Event groups record a bitmask but carry no payloads, and reading the group is a polling operation, not a registration with state delivery. The application must coordinate separately to handle late joiners.

---

## ISR-to-Thread Bridge

**LT OS:** `NotifyEventFromISR()` is marked `LT_ISR_SAFE` and accepts a `LTEvent_ISRThreadProxyNotifyProc`. It schedules the proxy on a system thread, which then calls `NotifyEvent()` with the full argument set, delivering a structured, typed notification all the way to every subscriber's callback — all from an interrupt context entry point with no subscriber-visible difference.

**Linux (userspace):** ISRs do not exist in userspace. Kernel driver-to-userspace signaling uses `signalfd`, `eventfd`, or `ioctl` — all requiring the receiver to poll or block, with no typed argument delivery.

**FreeRTOS:** `xQueueSendFromISR` / `xTaskNotifyFromISR` exist for ISR-to-task communication but deliver a fixed-size opaque block or a 32-bit value. Typed multi-subscriber fan-out from an ISR requires significant application scaffolding.

---

## Deterministic Client Data Lifecycle

**LT OS:** Every registration accepts an optional `LTThread_ClientDataReleaseProc`. The event system guarantees this release proc is called with an `LTThread_ReleaseReason` when the subscriber unregisters, when the event is destroyed while subscribers are still registered, or when the subscriber thread terminates. Client data ownership transfer is explicit and deterministic with no subscriber-polling required.

**Linux / FreeRTOS:** Resource reclamation on unsubscription is entirely application responsibility. There is no standard mechanism for a subscriber's client data to be released automatically when the event source is torn down or the subscriber's thread exits.

---

## Platform-Uniform API

**LT OS:** The `LTEvent` API is identical on all LT targets — Linux x86, Linux ARM, ESP32, Bouffalo, Anyka, ST Micro — with zero conditional compilation required in event-producing or event-consuming code.

**Linux:** The async notification landscape is fragmented across `signalfd`, `eventfd`, `inotify`, `netlink`, `epoll`, D-Bus, and POSIX signals, each with different semantics, portability constraints, and argument-passing capabilities.

**FreeRTOS:** Event groups, task notifications, and queues each cover a different slice of the problem space. Choosing among them and assembling them into a typed callback pattern is left to the application, and the result is not portable beyond FreeRTOS targets.

---

## Summary

| Feature | LT OS | Linux | FreeRTOS |
|---|---|---|---|
| Thread-affine callback dispatch | Built-in | Manual | Manual |
| Typed argument delivery to callback | Built-in | Manual serialization | Fixed-size opaque block |
| Multi-subscriber fan-out | Built-in | Manual | Manual |
| Immediate state on registration | Built-in | Not available | Not available |
| ISR-safe typed notification | Built-in | N/A (userspace) | Partial (no typed fan-out) |
| Deterministic client data release | Built-in | Manual | Manual |
| Cross-platform uniform API | Yes | No | No |

LT OS `LTEvent` elevates asynchronous notification from a raw inter-thread signaling primitive into a complete, type-safe, lifecycle-managed publish/subscribe system that operates uniformly from bare-metal interrupt handlers through multi-threaded hosted Linux processes.
