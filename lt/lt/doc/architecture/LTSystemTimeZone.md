# LTTime and LTSystemTimeZone: Benefits and Differentiators

## Overview

The Roku LT Operating System provides two complementary time primitives: **LTTime**, a nanosecond-precision clock type built into `LTCore`, and **LTSystemTimeZone**, a full-featured timezone and calendar library layered on top of it. Together they deliver a self-contained, deterministic, and resource-efficient time stack purpose-built for embedded IoT devices — with no dependency on any host OS, libc, or external timezone database.

---

## The LTTime Foundation

`LTTime` is a typed 64-bit nanosecond value (`struct { s64 nNanoseconds; }`) that the compiler treats as a distinct type, preventing accidental integer coercion. Its entire API is expressed as `LT_INLINE` and `LT_ISR_SAFE` functions — arithmetic, comparison, conversion, and construction — so every time operation compiles to direct register math with zero function-call overhead and is safe to call from interrupt service routines.

`LTCore` exposes three independent clocks through the same `LTTime` type:

| Clock | Purpose |
|---|---|
| `GetKernelTime()` | Monotonic uptime since LT boot |
| `GetClockTimeUTC()` | POSIX epoch (nanoseconds since 1/1/1970), or zero if not yet set |
| `GetApproximateClockTimeUTC()` | Best-effort fallback (defaults to build-time UTC) |

The `LTTimeBase` struct pairs any two clocks at a single capture instant, eliminating the drift introduced when a UTC offset is computed and stored before the kernel clock advances — a subtle correctness issue that most RTOS time implementations ignore.

---

## LTSystemTimeZone: Self-Contained Timezone Intelligence

### No External Database Required

FreeRTOS has no timezone library at all; applications that need local time must integrate a third-party package (e.g., `esp-idf`'s `newlib` tzdata, or a ported `tzdata` tarball) and manage its update lifecycle. Linux delegates timezone handling to `glibc`/`musl` and a separately maintained `/usr/share/zoneinfo` binary database. Both approaches add flash footprint, update complexity, and runtime file-system access.

`LTSystemTimeZone` ships its entire timezone table as a read-only C array of `LTTimeZone` structs containing POSIX.1-2001 TZ strings. There is no file I/O, no database parser, and no separate update channel. All world-region zones — US, Canada, Mexico, Brazil, Europe, Russia, India, Japan, Australia, New Zealand, the Pacific, and full UTC ±14 — are available from first boot.

### Nanosecond-Precision Throughout

Where `struct tm` on Linux tracks only integer seconds, and FreeRTOS tick-based time is typically millisecond-granular, every `LTSystemTimeZone` API accepts and returns `LTTime`. UTC↔local conversions, DST boundary checks, and `CalendarTimeToClockTime` all preserve nanosecond resolution end-to-end.

### Full DST Handling from First Principles

The library implements the complete POSIX.1-2001 TZ rule grammar — `M`-form (nth weekday of month), `J`-form (Julian day, no leap), and ordinal-day forms — to compute DST transitions for any year without pre-baked tables. This means it correctly resolves transitions for future dates without any update, and handles half-hour and 45-minute UTC offsets (India, Nepal, Chatham Islands) that many simplified RTOS libraries do not support.

### Thread-Safe by Design

The system timezone index is stored in an `LTAtomic`, so reads never block. When `SetUserTimeZone` updates the mutable "User" zone slot, it holds an `LTMutex` only for the brief copy of the reference-data struct. Hot-path operations (`GetClockTimeLocal`, `ClockTimeUTCToLocal`, `IsClockTimeUTCDaylightSaving`) check a cached reference pointer first and, on a hit, perform a single mutex-guarded `memcpy` before doing purely arithmetic UTC↔local conversion — no string parsing on the critical path.

### Dynamic User Timezone

`SetUserTimeZone` accepts UTC offsets and DST start/end timestamps at runtime and synthesizes a valid POSIX TZ string on the fly. This is essential for IoT devices that receive timezone information from a cloud provisioning service and must not hard-code a region. No equivalent is available in FreeRTOS without custom code, and on Linux this facility exists only at the process level via the `TZ` environment variable.

### Bidirectional Calendar Conversion with Validation

`ClockTimeToCalendarTime` and `CalendarTimeToClockTime` round-trip `LTTime` through a structured `LTCalendarTime` (year, month, day, hour, minute, second, millisecond, weekday — all `u16` fields, 16 bytes total). `CalendarTimeToClockTime` validates every field and returns `false` for out-of-range inputs rather than invoking undefined behavior. Linux's `mktime` silently normalizes out-of-range fields; FreeRTOS has no equivalent without newlib.

Converting local time in any timezone to local time in any other timezone is straightforward: call `ClockTimeLocalToUTC()` with the source timezone ID to obtain UTC, then call `ClockTimeUTCToLocal()` with the destination timezone ID. The local time passed to the first call may come from `GetClockTimeLocal()` for the current moment, or from filling out an `LTCalendarTime` struct with any desired date and time and calling `CalendarTimeToClockTime()` — enabling arbitrary cross-timezone conversions for past, present, and future dates.

### Human-Readable Output, No printf Required

`ClockTimeToHumanReadableString` renders a complete timestamp — weekday, month, day, year, HH:MM:SS.mmm, 12-or-24-hour mode, and DST-aware timezone abbreviation — into a caller-supplied buffer using only `lt_snprintf`. This works without `<stdio.h>`, `strftime`, or any locale library, enabling readable log output on targets where the C runtime is absent.

---

## Summary Comparison

| Capability | FreeRTOS | Linux (glibc) | LT (LTTime + LTSystemTimeZone) |
|---|---|---|---|
| Time resolution | Tick (~1–10 ms) | Microsecond (clock_gettime) | Nanosecond (LTTime) |
| ISR-safe time read | Platform-dependent | No | Yes (LT_ISR_SAFE) |
| Clock drift correction | None built-in | NTP via OS | LTTimeBase pairing |
| Timezone library | None (third-party) | glibc + tzdata file | Built-in, no file I/O |
| DST computation | None built-in | tzdata binary | POSIX TZ rule parser |
| Half-hour UTC offsets | Varies by port | Yes | Yes |
| Dynamic user timezone | No | TZ env var (process) | SetUserTimeZone() API |
| Thread-safe zone switch | N/A | N/A | Atomic index + mutex |
| Calendar ↔ clock | None / newlib | mktime / gmtime | Bidirectional, validated |
| Time zone conversion | No | localtime / mktime | LocalToUTC → UTCToLocal, any zone pair |
| Human-readable output | None / newlib | strftime (libc) | Built-in, no libc |
| External dependencies | Varies | glibc, tzdata | None |

---

`LTSystemTimeZone` delivers the timezone and calendar correctness of a full Linux stack in a form that fits a 64 KB embedded device — zero external dependencies, deterministic performance, nanosecond precision, and ISR-safe time primitives from the ground up.
