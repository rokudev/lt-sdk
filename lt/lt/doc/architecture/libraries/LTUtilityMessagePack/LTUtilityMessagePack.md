# LTUtilityMessagePack — Differentiators and Usage Guide

## Overview

`LTUtilityMessagePack` is the LT OS MessagePack encoder/decoder, built for resource-constrained IoT devices (100 MHz, 64 KB RAM). It implements the full [MessagePack specification](https://github.com/msgpack/msgpack/blob/master/spec.md) and extends it with LT-native features unavailable in FreeRTOS or Linux counterparts.

---

## Differentiators vs. FreeRTOS and Linux

### vs. FreeRTOS
FreeRTOS ships with no MessagePack library. Third-party ports (e.g., mpack) require `stdlib.h`, `malloc`/`free`, and manual porting for each target BSP. LTUtilityMessagePack:

- **Integrates natively with LT memory** (`lt_malloc`, `lt_free`, `lt_realloc`) — no libc dependency anywhere in the public or private interface.
- **Runs identically across all LT platforms** (ESP32, Bouffalo BL70x, Anyka AK3918x, ST H755, Linux x86/ARM) without conditional compilation or platform porting work.
- **Loads as a dynamically-linked LT library** — applications do not statically link the codec; it is shared across the firmware image via the LT library manager, saving flash and RAM on embedded targets.
- **Provides compile-time size macros** (`MP_U32_SIZE`, `MP_STR_SIZE`, `MP_BIN_SIZE`, etc.) that let callers statically size encode buffers at zero runtime cost.

### vs. Linux (msgpack-c / libmsgpack)
Linux MessagePack libraries are mature but designed for hosted environments. LTUtilityMessagePack adds value even when running on Linux x86:

- **Dual API surface**: an *object API* (`Put`/`Get`, pointer-based, ideal for decoding received data) and a *buffer-streaming API* (`Write`/`Read`, built on `LTBuffer`, ideal for producing data streamed to a socket, file, or DMA transfer) — both in a single library.
- **Zero-copy string decoding**: `PutCString` always stores the null terminator in the wire format; `GetString` returns a direct pointer into the encoded buffer, eliminating the copy step common in msgpack-c's `msgpack_object_str` model.
- **Structured search without full decode**: `FindCString` and `FindInteger` scan arrays or map *keys* with position preservation — if not found, position is unchanged; if found in a map, position advances to the corresponding *value*. msgpack-c requires a full unpack tree for equivalent key lookup.
- **Container-bounded navigation**: `SkipWithin`, `SkipContainer`, and the `GetValue`-then-`SkipContainer` pattern allow efficient traversal and early exit without allocating an object tree.
- **Deferred map assembly via `PutMessagePack`**: when the number of map entries is unknown until serialization is complete, encode items into a secondary `LTMessagePack_Obj`, then splice it into the primary with a single call after writing the map header — no two-pass buffer management needed.
- **LT-extended table type**: `WriteTable`/`ReadTable` encode a structured table (row/column headers, nil-terminated or fixed-length rows) as a standard MessagePack extended type, interoperable with any spec-compliant parser while carrying LT-native semantics.
- **Endian safety without alignment assumptions**: integer serialization uses shift-based byte construction (`Set16`/`Get16`, etc.) that is correct on both big-endian and little-endian hosts and tolerates non-aligned byte sequences — important for embedded targets where `memcpy`-cast tricks are UB.
- **Type-dispatch generic macros** (`LTMessagePackPut`, `LTMessagePackGet`) select the correct API call from the C11 `_Generic` mechanism — the encoder and decoder read naturally without per-type boilerplate.

---

## Usage Models with Example Code

### 1. Encode into a caller-supplied buffer

```c
u8 buf[64];
LTMessagePack_Obj obj;
mp->Init(&obj, buf, sizeof(buf));

mp->PutMap(&obj, 2);
mp->PutCString(&obj, "id");     mp->PutIntU32(&obj, 42);
mp->PutCString(&obj, "ok");     mp->PutBoolean(&obj, true);

u32 len = mp->GetPosition(&obj); // total encoded bytes
send(socket, buf, len);
```

### 2. Auto-growing encode buffer (unknown size at call time)

```c
LTMessagePack_Obj obj;
mp->Init(&obj, NULL, 64);       // LT allocates; expands as needed

mp->PutArray(&obj, item_count);
for (u32 i = 0; i < item_count; i++)
    mp->PutIntU32(&obj, items[i]);

u32 len = mp->GetPosition(&obj);
transmit(obj.head, len);
mp->Free(&obj);                  // release LT-managed memory
```

### 3. Decode with type-generic helper macros

```c
LTMessagePack_Obj obj;
mp->Init(&obj, received_data, received_len);

u32 count;
mp->GetArray(&obj, &count);
for (u32 i = 0; i < count; i++) {
    u32 value;
    if (LTMessagePackGet(mp, &obj, &value))   // _Generic picks GetInteger
        process(value);
}
```

### 4. Map key lookup with `FindCString`

```c
LTMessagePack_Obj obj;
mp->Init(&obj, data, data_len);

LTMessagePack_Value map;
mp->GetValue(&obj, &map);             // decode map header

if (mp->FindCString(&obj, &map, "temperature")) {
    float temp;
    LTMessagePackGet(mp, &obj, &temp);  // position is on the value
}
```

### 5. Multi-pass decoding with position save/restore

```c
u32 start = mp->GetPosition(&obj);    // save position at start of map

if (mp->FindCString(&obj, &map, "type")) {
    const char *type;
    LTMessagePackGetCString(mp, &obj, &type);
    // ...
}

mp->SetPosition(&obj, start);         // rewind for second pass
if (mp->FindCString(&obj, &map, "payload")) { /* ... */ }
```

### 6. Skip unknown fields during decode

```c
LTMessagePack_Value container;
mp->GetValue(&obj, &container);       // get array or map descriptor

// Process only first 2 elements, then jump to end
mp->SkipWithin(&obj, &container, 2);
mp->SkipContainer(&obj, &container);  // jump past remaining elements
```

### 7. Deferred map assembly with `PutMessagePack`

```c
LTMessagePack_Obj items;
mp->Init(&items, NULL, 128);
u32 count = 0;
for (each_sensor) {
    mp->PutCString(&items, sensor->name);
    mp->PutFloat32(&items, sensor->value);
    count++;
}

LTMessagePack_Obj out;
mp->Init(&out, tx_buf, sizeof(tx_buf));
mp->PutMap(&out, count);              // now we know the count
mp->PutMessagePack(&out, &items);     // splice in the pre-built items
mp->Free(&items);
```

### 8. Buffer-streaming API (`Write`/`Read` over `LTBuffer`)

```c
// Encoding into an LTBuffer (socket, ring buffer, DMA)
mp->WriteMap(buf, 1);
mp->WriteCString(buf, "event", /*terminated=*/true);
mp->PrintString(buf, "sensor_%u", sensor_id);  // formatted string encode

// Decoding from an LTBuffer
u32 count;
mp->ReadMap(buf, &count);
for (u32 i = 0; i < count; i++) {
    const char *key = mp->CopyString(buf);      // heap copy, zero-terminated
    u32 value;
    mp->ReadIntU32(buf, &value);
    process(key, value);
    mp->FreeString(buf, key);
}
```

### 9. User-defined extended types

```c
// Encode a MAC address as extended type 5
u8 mac[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
mp->PutExtended(&obj, 5, mac, sizeof(mac));

// Decode
u8 *data; u32 len;
s8 type = mp->GetExtended(&obj, &data, &len);
if (type == 5 && len == 6) memcpy(mac_out, data, 6);
```

### 10. Compile-time buffer sizing

```c
#define SENSOR_MSG_SIZE \
    MP_MAP_SIZE(2) + \
    MP_STR_SIZE(sizeof("temp")) + MP_FLOAT32_SIZE + \
    MP_STR_SIZE(sizeof("seq"))  + MP_U32_SIZE(UINT32_MAX)

static u8 sensor_buf[SENSOR_MSG_SIZE];
```

---

## Summary

`LTUtilityMessagePack` delivers full MessagePack compliance with zero external dependencies, dual object/buffer API surfaces, compile-time sizing, zero-copy decode, structured search, and LT-native memory management — all in a single dynamically-loadable library that runs identically from ESP32 to Linux x86 without a line of conditional compilation.
