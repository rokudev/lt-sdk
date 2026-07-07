# LT OS: A Purpose-Built RTOS for the Constrained Edge

The **Roku LT Operating System** is a self-contained, real-time OS designed for IoT devices with minimal resources — targeting hardware with as little as 100 MHz and 64 KB RAM. Rather than adapting a general-purpose OS downward or bolting networking onto a bare-bones scheduler, LT was architected from first principles around two imperatives: run on the smallest possible hardware, and compile identically on every platform without modification.

---

## LT OS vs. Linux

Linux is a mature, feature-rich OS, but its design assumptions conflict with deeply constrained hardware.

| | Linux | LT OS |
|---|---|---|
| **Minimum RAM** | ~2–8 MB (uClibc/BusyBox configs) | **64 KB** |
| **Kernel footprint** | MMU-required (for standard kernel) | **No MMU required** |
| **Boot time** | Seconds | **Milliseconds** |
| **Standard library** | Required (glibc/musl/uClibc) | **Zero — self-contained** |
| **Platform porting** | Kernel config, driver tree, toolchain | **Recompile, zero #ifdefs** |

Linux's process model, virtual memory subsystem, and dependency on a C standard library create an unavoidable floor that excludes a broad class of microcontrollers and IoT SoCs. LT OS eliminates that floor entirely: its core contains no `#include <stdio.h>`, no `malloc()`, and no OS-specific headers. LTCore supplies every abstraction an application needs — threads, synchronization, memory regions, atomic operations, timers, and console I/O — all implemented in pure, self-contained C.

The result is a system that runs as a static firmware image on an ESP32 or Bouffalo BL70x and as dynamically-loaded `.so` shared libraries on Linux x86 — from the **same source, with no code changes**.

---

## LT OS vs. FreeRTOS

FreeRTOS is the dominant RTOS for microcontrollers, valued for its simplicity and small scheduler footprint. LT OS targets the same hardware tier but delivers a substantially richer OS contract.

| | FreeRTOS | LT OS |
|---|---|---|
| **Scheduler** | Cooperative + preemptive | **Preemptive, 31 priority levels** |
| **Priority inheritance** | Opt-in, limited | **Built-in mutex priority inheritance** |
| **Networking** | Add-on (FreeRTOS+TCP) | **Integrated: TCP/UDP, TLS 1.3, DTLS 1.2, HTTP/HTTPS, WebRTC, STUN, SRTP, SIP** |
| **Library system** | None — application is monolithic | **Dynamic/static library loader; every component is a loadable library** |
| **OTA** | Application-managed | **First-class OTA device abstraction with ping-pong partition support** |
| **Test framework** | None built-in | **TILT — mock objects, PRN generator, performance stopwatch, multi-threaded test cases** |
| **Developer shell** | None | **LTSystemShell — network-accessible, extensible command interface** |
| **Host simulation** | Difficult | **Full Linux execution — same binary runs on device and workstation** |

Where FreeRTOS hands the developer a scheduler and leaves the rest to third-party libraries of varying quality, LT OS provides a coherent, opinionated platform. The **library architecture** is particularly distinctive: every OS component and application is packaged as an LT Library exporting a single typed root interface. Libraries are discovered, loaded, and closed through a unified runtime — giving the same dynamic composition model on embedded hardware that developers expect from a desktop OS, without the memory overhead.

The **LT networking stack** — lwip extended with WebRTC, DTLS, SRTP, X.509, and a full HTTP client — runs on the same 64 KB baseline hardware. This is not a common capability in the FreeRTOS ecosystem without moving to significantly larger hardware and integrating multiple third-party stacks.

---

## Summary

LT OS occupies a position that neither Linux nor FreeRTOS fills well: a **fully integrated, platform-portable RTOS** with enterprise-grade networking, a first-class developer experience (shell, test framework, host simulation), and a memory floor low enough for true microcontroller-class hardware. Its zero-dependency, zero-`#ifdef` design means a library written once compiles and runs correctly from an ESP32 to a Linux workstation — making it unusually productive for teams developing and testing IoT firmware.
