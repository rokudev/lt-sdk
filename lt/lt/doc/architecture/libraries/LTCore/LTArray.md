# LT OS Array Primitives: Benefits and Differentiators

## Overview

LT OS provides two first-class array primitives — `LTArray` and `LTAssociativeArray` — as core OS objects. Both are dynamic, index-addressable, allocation-managing containers that operate without the C standard library, the C++ runtime, or any external dependency, making them equally at home in bare-metal firmware and hosted Linux processes across all LT target platforms.

---

## Key Differentiators vs C Arrays and Other Constructs

### vs Plain C Arrays

| Capability | C Array | LTArray |
|---|---|---|
| Dynamic growth | No — fixed at declaration | Yes — doubles then adds fixed increment |
| Bounds checking | No | Yes — silent no-op on out-of-range access |
| Sort tracking | Manual | Automatic — `isSorted` flag maintained internally |
| Search algorithm | Manual linear | Binary (sorted) or linear (unsorted) — automatic selection |
| Insert at position | Manual `memmove` | Built-in `Insert`, `InsertSorted` |
| Lifecycle helpers | None | `RemoveAndFree`, `RemoveAndFreeAll` |
| 64-bit alignment | Not guaranteed | Guaranteed on first element; propagates if element size is aligned |

### vs C++ `std::vector` / `std::map`

`std::vector` and `std::map` require the C++ runtime, exception handling, RTTI, and the STL allocator chain — all of which are unavailable in the LT embedded environment. LTArray provides an equivalent feature set: dynamic growth with tunable initial capacity and growth cap, two storage strategies (contiguous and linked-list), binary search on sorted data, and a key-value associative variant, all through LT's own memory system with no external dependencies.

### vs FreeRTOS Queues and Lists

FreeRTOS queues are FIFO structures; they offer no random-access indexing, no sorting, and no search. FreeRTOS `List_t` is a priority-sorted intrusive list requiring caller-managed list items embedded in application structures. LTArray provides indexed random access, two element storage modes, integrated sort and search, and an associative dictionary — none of which are available as FreeRTOS primitives.

---

## Usage Models with Example Code

### 1. Pointer Array (Default) — storing object references

The default `LTArray` stores `void *` pointers. Integers that fit in a pointer-sized word may be stored the same way.

```c
/* Create and populate */
LTArray *names = lt_createobject(LTArray);
LTCStringKeyedArray_Set(names, "alpha", "Alice");   /* wrong example — see below */

/* Correct: use Append for a Pointer array of C strings */
array->API->Append(names, "Alice");
array->API->Append(names, "Bob");
array->API->Append(names, "Carol");

/* Sort ascending (case-insensitive) */
names->API->Sort(names, names->API->CompareCString,
                 (void *)kLTArrayCompare_IgnoreCaseAscending);

/* Find by name — binary search because array is now sorted */
s32 idx = names->API->Find(names, names->API->CompareCString,
                            "Bob", (void *)kLTArrayCompare_IgnoreCaseAscending);
/* idx == 1 */

/* Get by reference */
const char *name = names->API->Get(names, idx, NULL);

lt_destroyobject(names);
```

### 2. Struct Array — storing inline value types

Call `InitAsStructArray` (or use the `LTArray_CreateStructArray` helper) to store structs by value, eliminating a separate heap allocation per element.

```c
typedef struct { u32 id; u32 priority; char label[12]; } Task;

LTArray *tasks = LTArray_CreateStructArray(sizeof(Task));

Task t = { .id = 42, .priority = 5, .label = "render" };
tasks->API->Append(tasks, &t);

/* Get a copy */
Task copy;
tasks->API->Get(tasks, 0, &copy);

/* Get by reference — valid until next mutating operation */
Task *ref = tasks->API->Get(tasks, 0, NULL);
ref->priority = 10;  /* modify in place */

lt_destroyobject(tasks);
```

### 3. List Implementation — O(1) insert/remove at arbitrary positions

When insert and remove operations dominate and random access is infrequent, use the `List` specialization. The API is identical to the default implementation.

```c
LTArray *events = lt_createobject_typed(LTArray, List);

events->API->Append(events, someEvent);
events->API->Insert(events, 0, priorityEvent);   /* O(1) — no memmove */
events->API->Remove(events, 0);                  /* O(1) — no memmove */

lt_destroyobject(events);
```

### 4. Sorted Insert and Binary Search

`InsertSorted` maintains sort order on every insert using binary search (default impl) or linear search (list impl), avoiding a separate `Sort` call.

```c
static int CompareByPriority(const void *a, const void *b, void *cd) {
    u32 pa = ((Task *)a)->priority, pb = ((Task *)b)->priority;
    return (pa > pb) ? 1 : (pa < pb) ? -1 : 0;
}

LTArray *queue = LTArray_CreateStructArray(sizeof(Task));

Task t1 = { .id = 1, .priority = 3 };
Task t2 = { .id = 2, .priority = 7 };
Task t3 = { .id = 3, .priority = 1 };

queue->API->InsertSorted(queue, CompareByPriority, &t1, NULL);
queue->API->InsertSorted(queue, CompareByPriority, &t2, NULL);
queue->API->InsertSorted(queue, CompareByPriority, &t3, NULL);
/* Array is [priority=1, priority=3, priority=7] */

s32 found = queue->API->Find(queue, CompareByPriority, &t2, NULL);
/* Binary search — found == 2 */
```

### 5. Allocation Tuning and Trim

For arrays whose final size is predictable, `TuneAllocation` reduces realloc churn. `Trim` recovers excess capacity after bulk population.

```c
LTArray *buf = lt_createobject(LTArray);
/* Pre-size for 64 items; grow by at most 16 at a time after that */
buf->API->TuneAllocation(buf, 64, 16);

for (u32 i = 0; i < 64; i++) buf->API->Append(buf, items[i]);

/* Reclaim any over-allocated capacity */
buf->API->Trim(buf);
```

### 6. Associative Array — binary-key dictionary

`LTAssociativeArray` is a sorted key-value store. Keys are arbitrary byte sequences; keys ≤ 14 bytes are stored inline with no secondary allocation. Lookup, insert, and remove are all O(log N) via binary search.

```c
LTAssociativeArray *cfg = lt_createobject(LTAssociativeArray);

/* C-string keys using the provided helper wrappers */
LTCStringKeyedArray_Set(cfg, "host",    "192.168.1.1");
LTCStringKeyedArray_Set(cfg, "port",    "8080");
LTCStringKeyedArray_Set(cfg, "timeout", "5000");

const char *host = LTCStringKeyedArray_Get(cfg, "host", NULL);

if (LTCStringKeyedArray_Exists(cfg, "port")) {
    LTCStringKeyedArray_Remove(cfg, "port");
}

lt_destroyobject(cfg);
```

### 7. Associative Struct Array — inline struct values, arbitrary keys

Combining `LTAssociativeArray` with `InitAsStructArray` stores structs by value at each key with a single contiguous allocation per element.

```c
typedef struct { u32 hits; u32 misses; } CacheStats;

LTAssociativeArray *stats = LTAssociativeArray_CreateStructArray(sizeof(CacheStats));

CacheStats s = { .hits = 100, .misses = 5 };
LTCStringKeyedArray_Set(stats, "image_cache", &s);

CacheStats *ref = LTCStringKeyedArray_Get(stats, "image_cache", NULL);
ref->hits++;   /* modify in place */

lt_destroyobject(stats);
```

### 8. Enumeration of an Associative Array

`Enumerate` visits every key-value pair in key order. Return `false` from the callback to abort early.

```c
static bool PrintEntry(LTAssociativeArray *array, const void *key, u16 keySize,
                        void *value, void *clientData) {
    lt_consoleprint("key=%s val=%s\n", (const char *)key, (const char *)value);
    return true;  /* continue */
}

cfg->API->Enumerate(cfg, PrintEntry, NULL);
```

---

## Design Considerations

`LTArray` explicitly documents that **no internal mutex is provided** — callers are responsible for synchronization. This is a deliberate, correct design for an embedded OS: locking granularity, lock type (mutex vs. spinlock), and the exact critical section boundaries are application concerns that the array should not pre-decide. `GetStorage` returns `NULL` for the List implementation, so code that drops to raw pointer arithmetic must use the default implementation. `InsertSorted` requires the array to already be in sorted order; mixing arbitrary `Append`/`Insert` calls with `InsertSorted` on an unsorted array returns `-1`.
