# LTSystemSettings: Persistent Key-Value Storage Built for Embedded Reality

`LTSystemSettings` is LT OS's built-in library for storing small, persistent configuration values in flash. It provides a clean, typed API over a robust flash storage engine that handles wear, power-cycle safety, and write coalescing automatically — capabilities that FreeRTOS itself does not provide at all, and that Linux-based solutions either over-provision or leave fragmented across multiple subsystems.

---

## What FreeRTOS Provides

FreeRTOS has no built-in persistent storage. Developers must choose from:

- **ESP-IDF NVS** — robust, but tightly coupled to the ESP32 platform and ESP-IDF toolchain
- **EEPROM emulation libraries** — typically vendor-specific, limited to raw byte arrays, no key-value model
- **FatFS / LittleFS over SPI flash** — general-purpose filesystems, heavyweight for simple configuration storage, no built-in key-value API
- **Custom hand-rolled solutions** — common in practice, with widely varying correctness guarantees

None of these are part of FreeRTOS itself, and none offer the complete, integrated feature set of LTSystemSettings out of the box.

## What Linux Provides

Linux embedded systems have many storage options, but each comes with significant overhead for a constrained IoT device:

- **Plain files (`/etc/*.conf`, INI files)** — no type safety, no atomicity, application must handle parsing, write ordering, and power-cut recovery
- **SQLite** — full relational database; typically 300–500 KB of flash just for the library, requires a filesystem, and carries concurrency and journaling overhead far beyond configuration storage needs
- **systemd `sd-id128` / `machine-id`** — narrow-purpose identity storage, not general key-value
- **D-Bus / GSettings** — desktop-tier infrastructure; impractical for any embedded target below Linux on a capable SoC
- **`/proc` / `sysfs`** — volatile; lost on power cycle by design

The fundamental issue on Linux is that the closest analog — SQLite or a filesystem-backed store — requires a working filesystem, a filesystem driver, a VFS layer, and either journaling or application-managed atomicity. None of that is free on a constrained device.

---

## LTSystemSettings Feature Comparison

| | FreeRTOS ecosystem | Linux ecosystem | LTSystemSettings |
|---|---|---|---|
| **Built-in to OS** | No — third-party required | Fragmented across layers | **Yes — standard LT library** |
| **Platform portability** | Varies (ESP-IDF NVS is ESP32-only) | Linux-only | **Platform-independent: ESP32, Bouffalo, Linux x86, ARM** |
| **Filesystem required** | Varies | Yes (for file/SQLite-based) | **No — directly addresses flash via `LTDeviceFlash`** |
| **Data types** | Typically integer or raw bytes | Strings (INI/conf), anything (SQLite) | **Typed: `s64` integer, string, binary blob** |
| **Key model** | Integer handles or flat names | File paths or SQL schema | **Hierarchical `/section/key` path namespace** |
| **Write coalescing** | Rarely | No (file write is immediate) | **5-second idle timer batches all writes into one flash erase cycle** |
| **Power-cycle safety** | Varies by library | Requires journaling FS or `fsync` | **Built-in ping-pong: falls back to prior valid sector on any write failure** |
| **CRC validation** | Varies | No (file integrity not checked) | **Full CRC over written data; magic committed only after read-back verification** |
| **Flash wear optimization** | Varies | No concept (Linux has block layer) | **Integers packed to minimum byte size (1–8 bytes); coalescing limits erase cycles** |
| **Cache-coherent reads** | Varies | OS page cache (on a filesystem) | **Write cache checked first; a value just written is immediately readable** |
| **Prefix enumeration** | Rarely | `readdir` / SQL `LIKE` | **`EnumerateSettingsWithPrefix()` with abortable callback** |
| **Bulk delete** | Rarely | `rm` / SQL `DELETE WHERE` | **`DeleteSettingsWithPrefix()` — removes entire logical group atomically** |
| **Flush on reboot** | Application responsibility | `sync` before reboot | **Automatic: watchdog `OnRebootNotify` triggers synchronous flush before restart** |
| **Flush on library close** | N/A | N/A | **Automatic: `LibFini` blocks until background thread flushes** |
| **Background I/O** | Application responsibility | Kernel writeback threads | **Dedicated low-priority LTThread (1 KB stack) keeps flash I/O off application threads** |
| **RAM footprint** | Varies | Process + SQLite cache (MBs) | **Write cache only; integers stored inline (8 bytes); no filesystem buffer cache** |
| **Section isolation** | Varies | Separate files | **Multiple named sections from flash partition table, each independently ping-ponged** |

---

## Key Design Details

**Write coalescing and flash longevity.** Every write API call updates only the in-RAM write cache and arms a 5-second timer (reset on each subsequent write). A rapid sequence of configuration changes — common during initialization — results in exactly one flash erase-and-rewrite cycle. This directly extends the life of the flash device on hardware where erase endurance (typically 10,000–100,000 cycles) is a real constraint. Linux filesystem journaling solves atomicity but does not coalesce at the key-value granularity; each `write(2)` + `fsync(2)` is a separate I/O operation.

**Ping-pong power-cycle safety.** Flash is written by erasing the inactive half of a dual-partition area, writing new data, CRC-verifying the result, then committing a magic number that marks it valid. If power is lost at any point before the magic commit, the prior half remains intact and is selected at next boot via counter comparison. This protection requires no application awareness and no journaling filesystem — it is entirely self-contained in the settings engine operating directly on raw flash.

**Packed integer storage.** A 64-bit signed integer is stored in 1, 2, 4, or 8 bytes on flash depending on its value, with sign and width encoded in the type byte. A setting holding the value `42` occupies 1 byte of flash payload rather than 8 — directly relevant on devices where the settings partition is measured in kilobytes. Linux file-backed stores write integers as text strings; SQLite uses at minimum 1-byte type + varint, but requires the full SQLite page infrastructure around it.

**Cache-first reads.** Reads search the in-RAM write cache before accessing flash. A value set by `SetIntegerValue` is immediately visible to `GetIntegerValue` in the same execution context, with no flash latency and no risk of stale data from a pending coalesced write. Linux page cache provides similar behavior for file-backed stores, but only once a filesystem and VFS layer are in place.

**No filesystem dependency.** `LTSystemSettings` drives flash directly through the `LTDeviceFlash` device abstraction, bypassing any filesystem layer entirely. This is the critical architectural difference from both Linux file-backed stores and from FatFS/LittleFS-based FreeRTOS solutions: there is no FAT table to corrupt, no inode layer to maintain, and no filesystem mount step required at boot.

**Library lifecycle integration.** On open, LTSystemSettings discovers flash partitions automatically. On close, it blocks until the background thread has flushed all pending writes — so no configuration changes are silently lost during system shutdown or library teardown.

---

## Summary

`LTSystemSettings` delivers a complete, production-ready persistent configuration system as a standard part of LT OS. The combination of automatic write coalescing, ping-pong power-cycle safety, CRC-verified writes, packed integer encoding, cache-coherent reads, watchdog-integrated flush-on-reboot, and direct-flash access without a filesystem removes an entire class of embedded storage engineering from the application developer's responsibility.

In the FreeRTOS ecosystem, assembling an equivalent feature set requires selecting, integrating, and validating multiple third-party components — typically platform-locked. In the Linux ecosystem, the nearest equivalent (SQLite or a journaled filesystem) carries RAM and flash overhead that exceeds the total resource budget of the hardware LTSystemSettings is designed to run on. LTSystemSettings occupies the gap between both: purpose-built for constrained embedded hardware, robust enough for production, and portable across every platform LT OS supports.
