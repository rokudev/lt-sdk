# LTUtilityByteOps: Benefits and Differentiators

## Overview

`LTUtilityByteOps` is a unified LT OS library that bundles seven categories of byte-level utilities — random byte generation, UUID lifecycle management, Base64 encode/decode, hex encode/decode, streaming CRC32, and buffer byte reversal — into a single, dependency-free library root interface. The entire library compiles identically on every LT platform, from 100 MHz embedded microcontrollers to Linux x86, with no conditional compilation and no external library headers.

---

## Key Differentiators

### One Library for All Common Byte Operations

Linux scatters equivalent functionality across `zlib` (CRC32), `libuuid` (UUID), OpenSSL or GLib (Base64), and hand-rolled code (hex, random). FreeRTOS provides none of it — developers source each capability independently, with incompatible APIs and separate initialization paths. `LTUtilityByteOps` delivers all seven categories through a single library handle, consistent argument conventions, and a single `#include`. There is no integration work, no linker dependency graph to manage, and no version mismatch surface.

### Entropy-Adaptive PRNG — Hardware First, Software Fallback

The PCG-XSH-RR generator (Melissa O'Neil, Harvey Mudd College, 2014) produces 2⁶⁴ distinct streams of uniformly distributed non-recurring 32-bit values in a 2⁶⁴ number space from a state that fits in two `u64` words. Seeding attempts `LTDriverCryptoEntropy` first — the platform's hardware RNG — and falls back automatically to a software entropy source that samples kernel time against atomic loop counts over 4 × 5 ms windows, XORing timing jitter with a loop counter. The seed buffer is zeroed after use. The generator is mutex-protected and thread-safe.

FreeRTOS has no built-in PRNG. Linux `getrandom()` is a syscall and unavailable in bare-metal contexts; `/dev/urandom` requires a file-system driver. Neither offers a documented software fallback path for environments without hardware entropy.

### RFC 4122–Compliant UUID Generation with Round-Trip String I/O

`GenUUID` produces a Version 4, Variant 1 UUID with 122 bits of randomness by generating 16 PCG bytes and stamping the version (`0x40`) and variant (`0x80`) bits per the RFC. `UUIDToString` and `StringToUUID` provide validated round-trip conversion to/from the canonical `"xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"` form. `IsNullUUID` and `NullifyUUID` provide lifecycle sentinels.

Linux depends on `libuuid` (an external package) or manual RFC 4122 implementation. FreeRTOS has no UUID facility at all.

### Base64 with Explicit Buffer-Sizing Queries

`GetBase64EncodeBufferRequirement(n)` and `GetBase64DecodeBufferRequirement(n)` return the exact buffer size needed before any allocation, using the standard formulas `(((n+2)/3)*4)+1` and `(((n+3)/4)*3)` respectively. Encoding returns the exact output strlen; decoding returns the exact number of decoded bytes (never over-reports due to padding). Incomplete input blocks (fewer than 2 valid Base64 characters) produce no output bytes rather than silently corrupting the output buffer.

Linux stdlib has no Base64 functions. OpenSSL and GLib provide them but require external linkage. FreeRTOS has none.

### Hex Encode/Decode with Case Selection and Fault Tolerance

`HexEncode` accepts a `bLowerCase` flag selecting between `"0123456789abcdef"` and `"0123456789ABCDEF"` output alphabets. `HexDecode` accepts mixed-case input (`A–F` and `a–f`) and stops cleanly at the first invalid character, returning the count of successfully decoded bytes rather than leaving the output in an indeterminate state. Odd-length input strings are handled by ignoring the trailing nibble.

Linux `printf("%02x")` encodes but provides no standard decode path. FreeRTOS provides neither.

### Streaming CRC32

`Crc32` uses the standard IEEE 802.3 / zlib / PKZIP polynomial (`0xEDB88320`, table-driven, Gary S. Brown). Calling it with `*pCrc = 0` on the first buffer and reusing the returned `*pCrc` for subsequent buffers accumulates the CRC across an arbitrary number of partial-buffer calls with no intermediate state object. The result is identical to a single-call CRC over the concatenated input.

Linux uses `zlib`'s `crc32()` — an external dependency. FreeRTOS has no CRC primitive; hardware CRC peripherals require platform-specific driver code.

### Platform-Uniform API

The same `LTUtilityByteOps` library interface, behavior, and wire-level output is guaranteed on all LT targets: ESP32, Bouffalo BL70x, Anyka AK3918x, ST Micro H755, Linux x86, and Linux ARM. Code that generates a UUID or a Base64-encoded credential on one platform produces bit-identical results on every other.

---

## Usage Models with Example Code

### 1. Random Bytes

```c
LTUtilityByteOps *byteOps = lt_getlibrary(LTUtilityByteOps);

/* Fill a buffer with random bytes */
u8 key[32];
byteOps->GenRandomBytes(key, sizeof(key));

/* Generate a random token as a lowercase hex string */
char token[33];  /* 16 bytes → 32 hex chars + null */
byteOps->GenRandomBytesAsHexString(16, token, sizeof(token));
/* token == e.g. "a3f91bc204e87d5a..." */
```

### 2. UUID Generation and String Conversion

```c
/* Generate a Version 4 UUID */
u8 uuid[LT_UUID_BYTE_LEN];
byteOps->GenUUID(uuid);

/* Convert to canonical string form */
char uuidStr[LT_UUID_STRING_LEN + 1];  /* 37 bytes */
byteOps->UUIDToString(uuid, uuidStr, sizeof(uuidStr));
/* uuidStr == e.g. "550e8400-e29b-41d4-a716-446655440000" */

/* Parse a UUID string back to bytes */
u8 parsed[LT_UUID_BYTE_LEN];
bool ok = byteOps->StringToUUID(uuidStr, parsed);

/* Null UUID lifecycle management */
byteOps->NullifyUUID(uuid);
if (byteOps->IsNullUUID(uuid)) { /* uuid is unset */ }
```

### 3. Base64 Encode / Decode

```c
const u8 data[] = { 0x48, 0x65, 0x6C, 0x6C, 0x6F };  /* "Hello" */
u32 dataLen = sizeof(data);

/* Size the output buffer exactly */
u32 encBufLen = byteOps->GetBase64EncodeBufferRequirement(dataLen);
char *encoded = lt_malloc(encBufLen);

u32 encodedLen = byteOps->Base64Encode(data, dataLen, encoded, encBufLen);
/* encoded == "SGVsbG8=" */

/* Decode back */
u32 decBufLen = byteOps->GetBase64DecodeBufferRequirement(encodedLen);
u8 *decoded = lt_malloc(decBufLen);
u32 decodedLen = byteOps->Base64Decode(encoded, encodedLen, decoded, decBufLen);
/* decodedLen == 5, decoded == { 0x48, 0x65, 0x6C, 0x6C, 0x6F } */

lt_free(encoded);
lt_free(decoded);
```

### 4. Hex Encode / Decode

```c
const u8 bytes[] = { 0xDE, 0xAD, 0xBE, 0xEF };

/* Encode to lowercase hex string — buffer must be 2*len+1 */
char hexStr[9];
byteOps->HexEncode(bytes, sizeof(bytes), hexStr, sizeof(hexStr), true);
/* hexStr == "deadbeef" */

/* Encode to uppercase */
byteOps->HexEncode(bytes, sizeof(bytes), hexStr, sizeof(hexStr), false);
/* hexStr == "DEADBEEF" */

/* Decode (accepts upper, lower, or mixed case) */
u8 decoded[4];
u32 n = byteOps->HexDecode("DeAdBeEf", 8, decoded, sizeof(decoded));
/* n == 4, decoded == { 0xDE, 0xAD, 0xBE, 0xEF } */
```

### 5. Streaming CRC32

```c
/* Single-buffer CRC */
u32 crc = 0;
byteOps->Crc32(firmware, firmwareLen, &crc);

/* Multi-buffer / streaming CRC — same result as single call over concatenation */
u32 streamCrc = 0;
byteOps->Crc32(header,  headerLen,  &streamCrc);
byteOps->Crc32(payload, payloadLen, &streamCrc);
byteOps->Crc32(trailer, trailerLen, &streamCrc);
/* streamCrc == CRC32 of header+payload+trailer */

/* Verify received data integrity */
if (streamCrc != expectedCrc) { /* data corrupt */ }
```

### 6. SwapBytes — In-Place Buffer Reversal

```c
/* Reverse byte order for endian conversion of an arbitrary buffer */
u8 bigEndianValue[] = { 0x00, 0x00, 0x04, 0xD2 };  /* 1234 big-endian */
byteOps->SwapBytes(bigEndianValue, sizeof(bigEndianValue));
/* bigEndianValue == { 0xD2, 0x04, 0x00, 0x00 } */

/* Reverse a UUID's byte order for a legacy system */
byteOps->SwapBytes(uuid, LT_UUID_BYTE_LEN);
```

### 7. Combining Operations — Random Credential Generation

```c
/* Generate a random Base64-encoded 32-byte session key */
u8 rawKey[32];
byteOps->GenRandomBytes(rawKey, sizeof(rawKey));

u32 credBufLen = byteOps->GetBase64EncodeBufferRequirement(sizeof(rawKey));
char *credential = lt_malloc(credBufLen);
byteOps->Base64Encode(rawKey, sizeof(rawKey), credential, credBufLen);
/* credential is a 44-character Base64 string ready for an HTTP Authorization header */

/* Generate a correlation ID for a network request */
u8 corrId[LT_UUID_BYTE_LEN];
char corrIdStr[LT_UUID_STRING_LEN + 1];
byteOps->GenUUID(corrId);
byteOps->UUIDToString(corrId, corrIdStr, sizeof(corrIdStr));

lt_free(credential);
```

---

## Design Considerations

`GenRandomBytes` uses PCG-XSH-RR, which is statistically strong and well-suited for token generation, UUID seeding, and nonce production. It is not a cryptographic RNG; for cryptographic key material, the output should be processed through an appropriately seeded block cipher (e.g. AES) or a cryptographic hash. `StringToUUID` accepts lowercase hex digits only — uppercase variants in the input return `false`. `HexDecode` stops at the first invalid character and returns the byte count decoded up to that point, so callers should check the return value against the expected length when input validity matters.
