# LT OS Linked List: Benefits and Differentiators

## Overview

`LTList` is LT OS's intrusive doubly-linked list. It is implemented entirely as forced-inline functions and macros in a single header — no source file, no runtime library, zero overhead beyond the two pointers woven into each element. The same API runs in thread context, in ISR context, and inside the kernel scheduler itself, making it the single list primitive used at every layer of LT OS.

---

## Key Differentiators

### Intrusive Design Eliminates Secondary Allocation

`LTList` is *intrusive*: the `LTList_Node` struct is embedded directly as a member of any application-defined data type. Adding an element to a list costs zero extra allocations — no wrapper node, no heap round-trip, no allocator lock.

Compare with:

| Construct | Node allocation | Overhead per element |
|---|---|---|
| `LTList` (intrusive) | None — embedded in struct | 2 pointers (8 or 16 bytes) |
| C++ `std::list` | One heap alloc per element | Node wrapper + allocator overhead |
| FreeRTOS `List_t` | `ListItem_t` embedded, but requires `xItemValue` sort key | 3 pointers + `TickType_t` per node |
| Linux kernel `list_head` | Same intrusive model — LTList is directly equivalent | 2 pointers |

This is critical on devices with 64 KB RAM: every prevented allocation is reclaimed RAM and eliminated fragmentation.

### Zero Dependencies, Full ISR Safety

Every function is annotated `LT_ISR_SAFE` and resolves to one or two pointer assignments. There are no locks, no memory barriers injected by the list itself, and no calls into any other subsystem. The caller controls atomicity with whatever mechanism is appropriate for the context — a spinlock in thread code, interrupt masking in ISR code. This is correct for an embedded OS: the list does not guess at synchronization granularity.

### One Type Works Everywhere in LT OS

The kernel scheduler uses `LTList` for its per-priority run queues and timer queues (see `LTKernel.c`). The buffer manager uses it for pending-operation queues. The settings system uses it for section and write-cache chains. The `LTArray` List implementation is backed by `LTList`. Using a single, proven primitive at every layer eliminates the class of bugs that arise from mixing multiple list implementations.

### `LTList_IsNodeLinked` — Free "Is Queued?" Test

After `LTList_Remove`, the removed node's pointers are set to point to itself. This makes `LTList_IsNodeLinked` a trivial, allocation-free sentinel check with no list handle needed — a pattern commonly emulated with a separate boolean flag in other implementations.

### `LTList_ForEach` — Removal-Safe Iteration

The `LTList_ForEach` / `LTList_EndForEach` macro pair saves the next pointer before invoking the loop body, making it safe to remove the current node mid-iteration — the most common mistake with hand-written list loops.

---

## Usage Models with Example Code

### 1. Basic Setup — Embed Node, Initialize List

```c
typedef struct {
    u32         id;
    u32         priority;
    char        name[16];
    LTList_Node node;          /* embed directly — no wrapper allocation */
} Task;

static LTList s_taskQueue;
static Task   s_tasks[4];

void Init(void) {
    LTList_Init(&s_taskQueue);   /* empty list: pNext == pPrev == self */
}
```

### 2. Add to Tail / Insert at Head

```c
/* Append — FIFO order */
LTList_AddTail(&s_taskQueue, &s_tasks[0].node);
LTList_AddTail(&s_taskQueue, &s_tasks[1].node);

/* Prepend — LIFO / highest priority first */
LTList_InsertHead(&s_taskQueue, &s_tasks[2].node);
```

### 3. Insertion Sort — Maintaining Priority Order

```c
void EnqueueByPriority(LTList *pList, Task *pNew) {
    for (LTList_Node *pNode = pList->pNext; pNode != pList; pNode = pNode->pNext) {
        Task *pExisting = LT_CONTAINER_OF(pNode, Task, node);
        if (pNew->priority >= pExisting->priority) {
            LTList_InsertBefore(pNode, &pNew->node);
            return;
        }
    }
    LTList_AddTail(pList, &pNew->node);   /* lowest priority — append */
}
```

### 4. `LT_CONTAINER_OF` — Recover the Enclosing Struct

```c
/* Process the head element */
if (!LTList_IsEmpty(&s_taskQueue)) {
    LTList_Node *pHead = s_taskQueue.pNext;
    LTList_Remove(pHead);                           /* O(1) unlink */
    Task *pTask = LT_CONTAINER_OF(pHead, Task, node);
    RunTask(pTask);
}
```

This is the canonical LTList pattern. `LT_CONTAINER_OF` computes `(Task *)((u8 *)pHead - offsetof(Task, node))` — a compile-time pointer adjustment, no runtime cost.

### 5. `LTList_ForEach` — Safe Iteration with Removal

`LTList_ForEach` pre-saves the next pointer, so removing the current node mid-loop is safe.

```c
/* Find a task by name and remove it */
Task *pFound = NULL;
LTList_ForEach(pNode, &s_taskQueue) {
    Task *pTask = LTList_GetNodeDataOfType(Task, pNode);
    if (lt_strcmp(pTask->name, "render") == 0) {
        LTList_Remove(pNode);           /* safe — next pointer already saved */
        pFound = pTask;
        break;
    }
} LTList_EndForEach;
```

One-liner bulk removal (as seen in `LTShellWiFi.c`):
```c
LTList_ForEach(pNode, &s_apList) lt_free(pNode); LTList_EndForEach;
```

### 6. `LTList_IsNodeLinked` — "Is Queued?" Without a Separate Flag

After `LTList_Remove`, the node's pointers are self-referential. `LTList_IsNodeLinked` exploits this as a zero-cost "is this item currently on a list?" test — no boolean field, no list handle needed.

```c
/* ISR-safe deferred work: only queue once */
if (!LTList_IsNodeLinked(&pTask->node)) {
    LTList_AddTail(&s_pendingWork, &pTask->node);
}
```

### 7. `LTList_AddAfter` — Insert After a Known Node

```c
/* Splice a new task immediately after the currently running task */
LTList_AddAfter(&pRunning->node, &pNew->node);
```

### 8. Multiple Lists per Struct — Via Multiple `LTList_Node` Members

Because the node is embedded, a single struct can participate in multiple independent lists simultaneously by carrying more than one `LTList_Node` member.

```c
typedef struct {
    u32         id;
    LTList_Node runLink;     /* on the scheduler run queue */
    LTList_Node timerLink;   /* simultaneously on the timer queue */
} LTKThread;
```

This is exactly how `LTKernel.c` implements the LT kernel scheduler — each `LTKThread` lives on both a priority run queue and a timer expiry queue at the same time, with no secondary allocation.

---

## Design Considerations

`LTList` intentionally provides **no internal synchronization** — callers own the lock. This is the correct design for code that must run in ISR context: a lock inside the list would deadlock or be impossible to implement correctly on bare-metal. The tradeoff is that the caller must apply a spinlock or interrupt-mask bracket wherever shared access requires it.

`LTList_ForEach` pre-saves only the immediate next pointer; if another thread adds nodes ahead of the current position mid-iteration without external synchronization, those new nodes may or may not be visited. This is standard behavior for unsynchronized intrusive lists and is not a defect — it is a direct consequence of the zero-overhead design.
