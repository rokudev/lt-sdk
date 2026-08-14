# LTSystemSettings: Benefits and Differentiators

## Overview

`LTSystemSettings` is LT OS's unified persistent key-value store. It provides typed storage of integers, strings, and binary blobs in a hierarchical `/`-separated namespace, backed by CRC-validated, power-cycle-tolerant ping/pong flash with a write-coalescing RAM cache and a 5-second debounced auto-flush — all through a nine-function API that works identically across every LT target platform.

---

## Key Differentiators

### Power-Cycle-Tolerant Ping/Pong Flash Storage

Every settings section occupies two equal flash areas — ping and pong. Each flush erases the inactive side, writes the full settings image, appends a CRC32 end-of-list marker, verifies the written data by reading it back, and only then stamps the magic number that makes the new side authoritative. The active side is determined at boot by comparing monotonic counter values. If power is lost at any point during a write, the prior side remains intact and is selected on the next boot.

**Linux:** The kernel's `configfs`, `/etc` files, and `sysfs` provide no built-in write atomicity or fallback. Applications must implement their own journaling or shadow-file swap schemes. Power loss during a write to `/etc/config` leaves a truncated or corrupt file.

**FreeRTOS:** No built-in settings store exists in FreeRTOS itself. ESP-IDF's NVS partition provides similar ping/pong semantics but is ESP32-specific. On all other FreeRTOS targets the application is responsible for implementing flash durability from scratch.

### Write-Coalescing RAM Cache with Debounced 5-Second Auto-Flush

Every `Set*` call writes to an in-RAM `LTList`-based write cache and arms (or rearms) a 5-second `LTThread` timer. Rapid successive writes to the same key update the existing cache entry in place — only the final value reaches flash, regardless of how many intermediate values were written. The flush is triggered automatically when the timer fires, when the library closes (`LibFini`), or when the watchdog signals an imminent reboot via `OnRebootNotify`. A `Flush(NULL)` call provides an explicit synchronous path before a controlled reboot.

**Linux:** Write coalescing is entirely application responsibility. Every `write()` to a settings file is an immediate I/O operation; there is no OS-provided debounce or in-process coalescing layer.

**FreeRTOS / ESP-IDF NVS:** NVS writes are synchronous and immediate. Each `nvs_set_*` call commits directly to flash; rapid updates to the same key burn multiple write cycles. There is no built-in debounce or cache.

### Hierarchical Namespace with Prefix Operations

Keys are full paths: `"wifi/ssid"`, `"net/server/host"`, `/assets/certs/root`. Sections are automatically selected by prefix (`/sectionname/...` routes to the named flash partition; unprefixed keys go to the default section). `DeleteSettingsWithPrefix` and `EnumerateSettingsWithPrefix` operate atomically with respect to the cache, letting an entire subsystem's settings be enumerated or wiped with a single call.

**Linux:** `/etc` files are flat per-application; there is no system-wide typed hierarchical store. `GSettings` provides one for desktop Linux but is a large external dependency unavailable in embedded contexts.

**FreeRTOS:** NVS namespaces are flat 15-character strings; no prefix enumeration or prefix deletion exists. Bulk deletion requires knowing every key name in advance.

### Typed Storage with Flash-Optimized Integer Packing

Integers are stored as 1, 2, 4, or 8 bytes on flash depending on magnitude — a value of `42` costs 1 flash byte for the payload, not 8. Negative values are stored as their absolute value with a sign bit in the type field. String and binary values carry a 16-bit length prefix. The type system is enforced at read time: a `GetIntegerValue` on a string key returns `false` rather than silently casting.

**Linux / FreeRTOS:** File-based or NVS string-encoded configs store all integers as ASCII text with no packing. Binary blobs require application-managed encoding. There is no type enforcement at the storage layer.

### Existence Check via NULL Output Pointer

All three `Get*` functions accept `NULL` as the output pointer and still return `true` if the key exists with the correct type. This provides a cheap existence test without allocating a receive buffer.

### Platform-Uniform API

The same `LTSystemSettings` interface, flash layout, and enumeration behavior runs on ESP32, Bouffalo BL70x, Anyka AK3918x, ST Micro H755, and Linux x86/ARM. Code that reads a credential on one platform runs without modification on every other.

---

## Usage Models with Example Code

### 1. Integer Settings — Read, Write, Existence Check

```c
LTSystemSettings *cfg = lt_openlibrary(LTSystemSettings);

/* Write an integer */
cfg->SetIntegerValue("boot/count", 0);

/* Read it back */
s64 count = 0;
if (cfg->GetIntegerValue("boot/count", &count)) {
    cfg->SetIntegerValue("boot/count", count + 1);
}

/* Existence check — NULL output, no allocation needed */
bool exists = cfg->GetIntegerValue("boot/count", NULL);

lt_closelibrary(cfg);
```

### 2. String Settings — Read, Write

```c
/* Write */
cfg->SetStringValue("wifi/ssid", "MyNetwork");
cfg->SetStringValue("wifi/password", "hunter2");

/* Read — LTString grows to fit on demand */
LTString ssid = NULL;
if (cfg->GetStringValue("wifi/ssid", &ssid)) {
    lt_consoleprint("SSID: %s\n", ssid);
}
ltstring_destroy(ssid);

/* Existence check */
bool hasPassword = cfg->GetStringValue("wifi/password", NULL);
```

### 3. Binary Settings — Read, Write, Size Query

```c
/* Write a binary blob (e.g. a 16-byte UUID) */
u8 deviceUUID[16];
byteOps->GenUUID(deviceUUID);
cfg->SetBinaryValue("device/uuid", deviceUUID, sizeof(deviceUUID));

/* Size query — pass NULL data pointer, actual size written to *pSizeInBytes */
u32 size = 0;
cfg->GetBinaryValue("device/uuid", NULL, &size);   /* size == 16 */

/* Read with known size */
u8 readback[16];
u32 readSize = sizeof(readback);
if (cfg->GetBinaryValue("device/uuid", readback, &readSize)) {
    /* readback contains the UUID, readSize == 16 */
}
```

### 4. Delete a Single Setting

```c
cfg->DeleteSetting("wifi/password");

/* Verify it is gone */
bool gone = !cfg->GetStringValue("wifi/password", NULL);   /* true */
```

### 5. Delete All Settings with a Prefix

```c
/* Remove an entire subsystem's settings in one call */
cfg->DeleteSettingsWithPrefix("wifi/");

/* The default section's keys are unprefixed — clear all of them */
cfg->DeleteSettingsWithPrefix("");
```

### 6. Enumerate Settings with a Prefix

The callback receives the full key, the suffix relative to the search prefix, the data type, and client data. Return `false` to abort early.

```c
typedef struct { u32 count; } EnumCtx;

static bool OnSetting(const char *pKey, const char *pKeySuffix,
                       LTSystemSettingsDataType type, void *pClientData) {
    EnumCtx *ctx = (EnumCtx *)pClientData;
    ctx->count++;
    lt_consoleprint("key=%s suffix=%s type=%c\n", pKey, pKeySuffix, (char)type);
    return true;   /* continue */
}

EnumCtx ctx = { 0 };
cfg->EnumerateSettingsWithPrefix("net/", OnSetting, &ctx);
lt_consoleprint("Found %lu net/ settings\n", LT_Pu32(ctx.count));
```

Callbacks may call `GetIntegerValue`, `SetStringValue`, `DeleteSetting`, etc. They must not call `DeleteSettingsWithPrefix` or `EnumerateSettingsWithPrefix`.

### 7. Hierarchical Sections via Prefix Routing

Keys beginning with `/sectionname/` are routed to the named flash partition; unprefixed keys go to the default section. The routing is transparent to the caller.

```c
/* Written to the default flash settings partition */
cfg->SetStringValue("device/name", "sensor-01");

/* Written to the /certs/ flash partition (if configured) */
cfg->SetBinaryValue("/certs/root_ca", rootCaDer, rootCaDerLen);
cfg->SetBinaryValue("/certs/client",  clientDer, clientDerLen);

/* Enumerate only certificate settings */
cfg->EnumerateSettingsWithPrefix("/certs/", OnSetting, NULL);

/* Wipe all certificates */
cfg->DeleteSettingsWithPrefix("/certs/");
```

### 8. Explicit Flush — Before Reboot or for Write-Once Sections

```c
/* Flush all Read/Write sections before a controlled reboot */
cfg->Flush(NULL);

/* Flush a specific section only */
cfg->Flush("/certs/");
```

### 9. Erase an Entire Section

`EraseSection` deletes the write cache and physically erases all flash sectors for the section, returning the hardware to a blank state.

```c
/* Factory reset: erase all settings */
cfg->EraseSection(NULL);          /* default section */
cfg->EraseSection("/certs/");     /* named section */
```

### 10. Read-Once-Cache-in-RAM Pattern

The header recommends reading settings once at startup and caching in RAM, as flash reads are relatively expensive on embedded targets.

```c
typedef struct {
    char    ssid[64];
    char    password[64];
    s64     txPower;
    bool    valid;
} WiFiConfig;

static WiFiConfig s_wifiCfg;

static void LoadWiFiConfig(LTSystemSettings *cfg) {
    LTString tmp = NULL;
    if (cfg->GetStringValue("wifi/ssid", &tmp)) {
        lt_strncpyTerm(s_wifiCfg.ssid, tmp, sizeof(s_wifiCfg.ssid));
    }
    if (cfg->GetStringValue("wifi/password", &tmp)) {
        lt_strncpyTerm(s_wifiCfg.password, tmp, sizeof(s_wifiCfg.password));
    }
    ltstring_destroy(tmp);
    cfg->GetIntegerValue("wifi/txPower", &s_wifiCfg.txPower);
    s_wifiCfg.valid = true;
}

/* Before writing, check cached value to avoid unnecessary flash wear */
static void UpdateSSID(LTSystemSettings *cfg, const char *newSSID) {
    if (lt_strcmp(s_wifiCfg.ssid, newSSID) != 0) {
        lt_strncpyTerm(s_wifiCfg.ssid, newSSID, sizeof(s_wifiCfg.ssid));
        cfg->SetStringValue("wifi/ssid", newSSID);   /* arms 5-sec flush timer */
    }
}
```

---

## Design Considerations

Flash reads bypass the write cache, so a key that has been `Set*` but not yet flushed is served from the RAM cache during the same session — the caller always observes the most recent value regardless of flush state. `EnumerateSettingsWithPrefix` must not call prefix operations from its callback; doing so produces non-deterministic results because the enumeration and modification would share the same underlying list. Settings reads are relatively expensive on embedded targets (flash I/O per call) so the read-once, cache-in-RAM pattern shown above is recommended for frequently accessed values.
