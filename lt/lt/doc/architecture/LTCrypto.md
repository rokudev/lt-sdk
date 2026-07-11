# LT Crypto: A Purpose-Built, Three-Tier Crypto Architecture for Embedded IoT

`LTSystemCrypto` is LT OS's built-in cryptography library. Rather than wrapping a single third-party toolkit, LT crypto is structured as a three-tier object system — software implementations, hardware-accelerated implementations, and hardware-secured key operations — all behind a single, platform-independent API that automatically dispatches to the best available backend at runtime.

---

## Architecture Overview

```
LTSystemCrypto (application API — single interface, same on every platform)
        ↓
LTDriverCrypto (driver layer — three object tiers)
    ├── LTSoftwareCrypto*    — portable pure-software implementations
    ├── LTHardwareCrypto*    — platform hardware acceleration (ESP32, OpenSSL on Linux, etc.)
    └── LTSecureCrypto*      — hardware-secured operations; private keys never leave secure storage
        ↓
Platform implementations
    ├── ESP32:  SHA engine, AES engine, RSA/BigNum engine (clock-gated, hardware registers)
    ├── Linux:  OpenSSL EVP backend (LTHardwareCrypto* maps to EVP calls)
    └── macOS:  Apple platform crypto backend
```

`GetOptions()` returns a bitfield reporting which algorithms are available in hardware vs. software. `GetStatus()` per-algorithm returns `HW_Only`, `SW_Only`, `HW_SW`, or `Disabled`. Applications query capability at startup and choose accordingly — or simply call the `LTSystemCrypto` API and let the driver layer dispatch automatically.

---

## Algorithm Coverage

| Category | Algorithms |
|---|---|
| **Hash** | SHA-1, SHA-256, SHA-512 (internal, used by EdDSA) |
| **HMAC** | HMAC-SHA1, HMAC-SHA256 |
| **Symmetric** | AES-128-GCM (AEAD), AES-128-CTR, AES-128-CBC, AES-128-XTS |
| **Asymmetric / Signature** | Ed25519 (EdDSA), ECDSA-P256 |
| **Key Exchange** | X25519 (ECDHE) |
| **DRBG** | HMAC-SHA256 DRBG, SP 800-90A, 128-bit security, reseed interval 2^16 |
| **BigNum / RSA** | RSA 512/1024/2048 (hardware BigNum engine on ESP32) |
| **Key Management** | Provisioned keys, wrapped keys, hardware key IDs, key references |
| **Certificate Management** | X.509 DER certificates, CA key store, device-specific secure cert storage |

---

## Code Size by Algorithm (ESP32)

Sizes below are measured from release builds for the ESP32 (`iot.indoor_plug` product, build `RokuLT-007.29S99999X`, 2026-07-07). Each algorithm is an independent object file; a product links only the algorithms it uses — unused entries contribute zero bytes to the firmware image.

### LTSoftwareCrypto — portable software implementations

| Algorithm / Component | Object file | Bytes |
|---|---|---:|
| SHA-1 (block + digest) | `LTDriverCryptoSha1.o` | 1,280 |
| SHA-1 sequential (streaming) | `LTDriverCryptoSeqSha1.o` | 479 |
| SHA-256 (block + digest) | `LTDriverCryptoSha256.o` | 1,587 |
| SHA-256 sequential (streaming) | `LTDriverCryptoSeqSha256.o` | 483 |
| HMAC core | `LTDriverCryptoHmac.o` | 659 |
| HMAC-SHA256 | `LTDriverCryptoHmacSha256.o` | 439 |
| HMAC-SHA256 sequential (streaming) | `LTDriverCryptoSeqHmacSha256.o` | 654 |
| ECDSA-P256 (sign + verify) | `LTDriverCryptoEcdsaP256.o` | 2,779 |
| ECDSA-P256 secure (key-reference) | `LTDriverCryptoSecureEcdsaP256.o` | 920 |
| Secure HMAC-SHA256 (key-reference) | `LTDriverCryptoSecureHmacSha256.o` | 463 |
| P256 curve math (field arithmetic) | `LTDriverCryptoP256.o` | 4,935 |
| BigNum math (Montgomery, modular) | `LTDriverCryptoBigNum.o` | 1,053 |
| Key manager | `LTDriverCryptoKeyManager.o` | 2,881 |
| Provisioned data (UDS, device keys) | `LTDriverCryptoProvisionedData.o` | 788 |
| Certificate manager | `LTDriverCryptoCertManager.o` | 1,012 |
| Library infrastructure | `LTDriverCrypto.o` | 881 |
| **LTSoftwareCrypto total** | | **21,293** |

### Esp32DriverCrypto — ESP32 hardware-accelerated implementations

| Algorithm / Component | Object file | Bytes |
|---|---|---:|
| SHA-256 (hardware SHA engine) | `Esp32DriverCryptoSha256.o` | 1,398 |
| HMAC-SHA256 (hardware SHA engine) | `Esp32DriverCryptoHmacSha256.o` | 700 |
| AES-128-GCM (hardware AES engine) | `Esp32DriverCryptoAesGcm.o` | 2,747 |
| X25519 (hardware BigNum engine) | `Esp32DriverCryptoX25519.o` | 1,662 |
| Curve25519 glue | `Esp32DriverCrypto25519.o` | 165 |
| BigNum (hardware RSA/BigNum engine) | `Esp32DriverCryptoBigNum.o` | 2,110 |
| Random (hardware RNG) | `Esp32DriverCryptoRandom.o` | 789 |
| Engine management (clock-gating) | `Esp32DriverCryptoEngine.o` | 531 |
| Library infrastructure | `Esp32DriverCrypto.o` | 1,052 |
| **Esp32DriverCrypto total** | | **11,154** |

### Combined total (ESP32)

| Library | Bytes |
|---|---:|
| LTSoftwareCrypto | 21,293 |
| Esp32DriverCrypto | 11,154 |
| **All crypto algorithms combined** | **32,447** |

The complete LT crypto stack — every algorithm, both HW and SW tiers, key management, certificate management, and library infrastructure — compiles to **32,447 bytes** on ESP32. A product using only a subset (e.g., SHA-256 + AES-GCM + X25519) links a proportionally smaller footprint. For comparison, a minimal mbedTLS build targeting the same algorithm set is typically 60–100 KB.

---

## LT Crypto vs. FreeRTOS / wolfSSL / mbedTLS

FreeRTOS has no built-in cryptography. Most FreeRTOS IoT projects use wolfCrypt (bundled with wolfSSL) or mbedTLS. Both are monolithic toolkits that must be configured and compiled as a unit.

| | wolfCrypt / mbedTLS (FreeRTOS) | LT Crypto |
|---|---|---|
| **Architecture** | Monolithic toolkit; configure-time feature flags | **Per-algorithm loadable libraries; link only what's used** |
| **Hardware acceleration** | Optional, requires per-platform HAL glue code | **Built-in three-tier dispatch; HW transparent to application** |
| **Secure key storage** | Application-managed; no standard abstraction | **`LTSecureCrypto*` + `LTDriverCrypto_KeyReference`; private keys never exposed in app memory** |
| **Runtime capability query** | Compile-time `#define` only | **`GetOptions()` / `GetStatus()` — runtime HW/SW discovery** |
| **Code size** | wolfCrypt: ~20–250 KB depending on config; mbedTLS: ~60–100 KB typical | **32 KB total for all algorithms on ESP32; per-algorithm range 439–4,935 bytes; product links only what it uses** |
| **Platform portability** | Portable but no HW acceleration abstraction | **Same API on ESP32 (HW registers), Linux (OpenSSL EVP), macOS (platform crypto)** |
| **DRBG standard** | wolfCrypt: NIST DRBG; mbedTLS: CTR-DRBG | **HMAC-SHA256 DRBG, SP 800-90A, 128-bit security** |
| **Streaming hash** | Context struct managed by caller | **`CreateSeqSHA256` / `Clone` / `Update` / `Finish` / `Destroy` — full lifecycle management** |
| **Context cloning** | mbedTLS has `mbedtls_md_clone`; wolfCrypt partial | **`CloneSeqSHA1/256`, `CloneSeqHMACSHA1/256` — needed for TLS transcript hashing** |
| **Key management API** | Not included | **`LTDriverCryptoKeyManager` + `LTSecureKeyManager`: named keys, CA keys, provisioned device identity** |
| **Certificate store** | Not included | **`LTDriverCryptoCertManager` + `LTSecureCertManager`: named certs, DER chains, device-specific storage** |

### ESP32 Hardware Acceleration

On ESP32, LT drives the hardware crypto engines directly at register level, with explicit clock-gating (`Esp32_ClockEnableCryptoClock` / `Disable`) to minimize power consumption between operations. Three independent hardware engines are managed:

- **SHA engine** — SHA-1 and SHA-256 with hardware block processing
- **AES engine** — AES-128 block cipher (CBC, CTR, GCM modes accelerated)
- **RSA/BigNum engine** — hardware modular arithmetic for RSA 512/1024/2048 and ECC field operations

The result codes include `kLTSystemCrypto_Result_Busy`, `kLTSystemCrypto_Result_Timeout`, and `kLTSystemCrypto_Result_HwError`, giving the application full visibility into hardware engine contention without silently falling back to software without notification.

---

## LT Crypto vs. Linux / OpenSSL

On Linux, OpenSSL is the dominant crypto library. It is comprehensive but designed for server-class workloads, and its use in constrained or embedded contexts carries meaningful trade-offs.

| | OpenSSL (Linux) | LT Crypto |
|---|---|---|
| **Library footprint** | ~1–3 MB (full); ~200–400 KB (stripped/minimal) | **32 KB total for all algorithms on ESP32; scales down to individual algorithm sizes for products using a subset** |
| **API complexity** | EVP layer requires context alloc, init, update, final, free — 5–6 calls per operation | **Single-call API for common cases: `GenDigestSHA256(data, len, digest)` — 1 call** |
| **Streaming API** | Available (EVP_DigestInit/Update/Final) | **Available with explicit Create/Clone/Update/Finish/Destroy lifecycle** |
| **Hardware acceleration** | Via ENGINE API or provider (complex integration) | **Transparent: LTHardwareCrypto* objects swap in; app code unchanged** |
| **Secure key storage** | OpenSSL has no standard abstraction for TEE/HSM keys | **`LTSecureCrypto*` accepts `LTDriverCrypto_KeyReference`; key material stays in secure hardware** |
| **Runtime capability discovery** | None — all OpenSSL algorithms always present | **`GetOptions()` bitfield — application learns what hardware offers** |
| **Platform independence** | Linux-only | **Same LTSystemCrypto API on ESP32, Linux, macOS; zero #ifdefs in application code** |
| **Dependency** | External library (system or vendored) | **Self-contained LT library; no external dependency outside LT** |
| **Standard compliance** | FIPS-capable via specific build | **DRBG: SP 800-90A; Ed25519/P256: RFC 8446 TLS 1.3 signature types** |

The Linux platform implementation in LT uses OpenSSL's EVP API as the `LTHardwareCrypto*` backend — OpenSSL here plays the role of the platform's hardware crypto accelerator, wrapped behind the same driver interface used by the ESP32 register-level implementation. This means the same application-level crypto code runs on either platform with no modification.

---

## Key Differentiators

**Secure key references** are the standout architectural feature. `LTDriverCrypto_KeyReference` supports four key types: plain (not recommended), provisioned (factory device identity — UDS, CommPrivate, AliasPrivate), wrapped, and hardware key IDs. `LTSecureCrypto*` objects accept references directly, so signing or HMAC operations can be performed using keys that never appear in application memory. Neither FreeRTOS toolkits nor OpenSSL provide this abstraction as part of their standard API.

**Per-algorithm library granularity** means a product that only uses SHA-256 and AES-128-GCM links exactly those two implementation files. There is no monolithic crypto library to configure; unused algorithms contribute zero code to the firmware image.

**Transparent HW/SW dispatch** means application code never needs to check for hardware availability. When the ESP32 SHA engine is available, `GenDigestSHA256` dispatches there automatically; when only software is present, it dispatches to the pure-software implementation — same result, same API, different performance.

---

## Summary

LT Crypto delivers a complete, production-ready cryptographic system as a standard part of LT OS. Its three-tier HW/SW/Secure architecture, runtime capability discovery, per-algorithm code granularity, and hardware-secured key reference model address embedded security requirements that OpenSSL was not designed for and that FreeRTOS-era toolkits address only partially. The same application-layer crypto code runs unchanged on a 100 MHz ESP32 with hardware AES acceleration, a Linux workstation backed by OpenSSL, or a macOS build backed by Apple platform crypto — without a single `#ifdef` in the calling code.
