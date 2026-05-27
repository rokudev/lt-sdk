/******************************************************************************
 * <lt/core/LTList.h>                                            LT Linked List
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTLIST_H
#define ROKU_LT_INCLUDE_LT_CORE_LTLIST_H

#include <lt/core/LTCore.h>
LT_EXTERN_C_BEGIN

/*  __________________________________________________________________________
 *  LTLIST OVERVIEW
 *  ______
 *  LTList implements an ISR-safe doubly-linked list using two defined types:
 *  LTList and LTList_Node. Add LTList_Node as a member variable to your arbitrary
 *  struct and that struct is then enabled to be contained in LTLists.
 *  _________
 *  API Style
 *  The interface functions in this header file are all forced inline.  This will
 *  not unduly increase the size of your code because each of these functions is
 *  only one line of code.  Either the requested operation is performed in that one
 *  line of code directly, or, due to greater complexity, that single line thunks
 *  through the LTCore public Interface to have the more complex operation performed
 *  directly.  There is no user discernible difference.
 *
 *  List Organization
 *  LTList elements are connected via LTList_Node structs that exist as internal members
 *  of the data structures being linked. LT_CONTAINER_OF() is a convenience macro that
 *  returns the pointer to the beginning of the structure containing the list node.
 *
 *  The LTList next pointer points to the beginning of the list, and the prev
 *  pointer points to the end:
 *
 *                      +------------------------------+
 *           +------+   |                              |
 *           |      P <-+   +------+       +------+    |
 *           | List |       |      |       |      |    |
 *           |      N <---> P Node N <---> P Node N <--+
 *           +------+       |      |       |      |
 *                          +------+       +------+
 *
 *  An empty LTList points to itself:
 *
 *                          +------+
 *                          |      P <--+
 *                          | List |    |
 *                          |      N <--+
 *                          +------+
 *
 *  Example:
 *
 *    // Data structure that is used as the basis for the list
 *    typedef struct {
 *        u32         nValue;
 *        LTList_Node node;
 *    } Data;
 *
 *    // The list descriptor
 *    static LTList s_list;
 *
 *    // Some data entries to put on list
 *    static Data s_data1, s_data2, s_data3;
 *
 *    void MyFunc(...) {
 *       // Initialize an empty list
 *       LTList_Init(&s_list);
 *       ...
 *       // Add elements to end of list
 *       LTList_AddTail(&s_list, &s_data1.node);
 *       LTList_AddTail(&s_list, &s_data2.node);
 *       ...
 *       // Iterate through list, adding up values as you go
 *       u32 nSum = 0;
 *       // The last node of the list points to the list descriptor
 *       for (LTList_Node * pNode = s_list.pNext;
 *                pNode != &s_list; pNode = pNode->pNext) {
 *           Data * pData = LT_CONTAINER_OF(pNode, Data, node);
 *           nSum += pData->nValue;
 *       }
 *       ...
 *       // Insertion sort of third value onto list
 *       s_data3.nValue = 33;
 *       for (LTList_Node * pNode = s_list.pNext;
 *                pNode != &s_list; pNode = pNode->pNext) {
 *           Data * pData = LT_CONTAINER_OF(pNode, Data, node);
 *           if (s_data3.nValue <= pData->nValue) {
 *               LTList_InsertBefore(pNode, &s_data3.node);
 *               break;
 *           }
 *       }
 *       ...
 *       // Iterate through list, removing elements one at a time
 *       LTList_Node * pNodeSave;
 *       for (LTList_Node * pNode = s_list.pNext;
 *               pNode != &s_list; pNode = pNodeSave) {
 *           pNodeSave = pNode->pNext;
 *           LTList_Remove(pNode);
 *       }
 *       ...
 *       // Iterate through list, in an easier fashion
 *       LTList_ForEach(pNode, &s_list) {
 *           LTList_Remove(pNode);
 *       } LTList_EndForEach;
 *    }
 *
 */

/* __________________________________________________________________________
 *  TYPEDEFS
 */

/* typedef LTList_Node first, because LTList uses it */
typedef struct LTList_Node {
    struct LTList_Node * pPrev;
        /**< pPrev points to:
         *   (a) the list descriptor if this node is the head
         *   (b) the tail if this is the list descriptor
         *   (c) the previous list node if there is one
         *   (d) self (this node) if the list is empty
         */
    struct LTList_Node * pNext;   /**< Pointer to next node (or if this node is the tail, to the head). */
        /**< pNext points to:
         *   (a) the list descriptor if this node is the tail
         *   (b) the head if this is the list descriptor
         *   (c) the next list node if there is one
         *   (d) self (this node) if the list is empty
         */
} LTList_Node;
        /**< represents a list node in an LTList.
         *
         * A list is formed as a doubly linked list of LTList_Nodes where the head node's pPrev points to the tail node
         * and the tail node's pNext points to the head node.  The user data of the node (the data for which the list
         * exists), is whatever struct the user wants, plus an LTList_Node member somewhere (anywhere) in the struct.
         */

/* now typedef LTList */
typedef LTList_Node LTList;
   /**< represents an LTList.
    *
    * An LTList is represented by a descriptor node whose pNext pointer points to the head of the list and whose
    * pPrev pointer points to the tail of the list.
    */

/* __________________________________________________________________________
 *  DATA ACCESS HELPER MACRO - LTList_GetNodeDataOfType
 */
#define LTList_GetNodeDataOfType(NodeDataType, pNode) LT_CONTAINER_OF(pNode, NodeDataType, node)

/* __________________________________________________________________________
 *  ITERATION HELPER MACROS - LTList_ForEach
 */
#define LTList_ForEach(node, list) { \
    LTList_Node * pLTListNodeForEachNodeSave, * node; \
    for (node = (list)->pNext; node != (list); node = pLTListNodeForEachNodeSave) { \
        pLTListNodeForEachNodeSave = node->pNext;
    /**< allows easy iteration of LTList elements
     *
     * Use as follows: <pre>
     *
     * typedef struct MyData {
     *    char name[32];
     *    u32  nRefCount];
     *    LTList_Node node;
     * } MyData;
     *
     * LTList s_list;
     *
     * void SomeFunction(void) {
     *     / * assume list is already initialized * /
     *     / * iterate through the list looking for the MyData record with name "Donaldo" * /
     *     MyData * pFoundMyData = NULL;
     *
     *     LTList_ForEach(pNode, &s_list)
     *     {
     *         MyData * pMyData = LTList_GetNodeDataOfType(MyData, pNode);
     *         if (0 == lt_strcmp(pMyData->name, "Donaldo")) { pFoundMyData = pMyData; break; }
     *     }
     *     LTList_EndForEach;
     *
     *     if (pFoundMyData != NULL) {
     *         LT_GetCore()->ConsolePrint("Found MyData with name %s! I Rule!\n", pFoundMyData->name);
     *     }
     * }
     * </pre>
     */
/*      ______ EndForEach  */
#define LTList_EndForEach } }
    /**< used for ending an LTList_ForEach iteration
     */

/* __________________________________________________________________________
 *  LTList HELPERS
 */
LT_INLINE void
LTList_LinkToPrev(LTList * pList, LTList_Node * pNodeToLink) LT_ISR_SAFE {
    pNodeToLink->pPrev = pList->pPrev;
    pNodeToLink->pNext = pList;
    pList->pPrev->pNext = pNodeToLink;
    pList->pPrev = pNodeToLink;
}

LT_INLINE void
LTList_LinkToNext(LTList * pList, LTList_Node * pNodeToLink) LT_ISR_SAFE {
    pNodeToLink->pPrev = pList;
    pNodeToLink->pNext = pList->pNext;
    pList->pNext->pPrev = pNodeToLink;
    pList->pNext = pNodeToLink;
}

/* __________________________________________________________________________
 *  LTList INTERFACE FUNCTIONS
 */
LT_INLINE void
LTList_Init(LTList * pList) LT_ISR_SAFE {
    pList->pPrev = pList;
    pList->pNext = pList;
}

LT_INLINE void
LTList_InsertHead(LTList * pList, LTList_Node * pNodeToInsert) LT_ISR_SAFE {
    /**< Link new node to front of list */
    /* NB: In LTList the next pointer points to the beginning of the list */
    LTList_LinkToNext(pList, pNodeToInsert);
}

LT_INLINE void
LTList_AddTail(LTList * pList, LTList_Node * pNodeToAppend) LT_ISR_SAFE {
    /**< Link new node to end of list */
    /* NB: In LTList the previous pointer points to the end of the list */
    LTList_LinkToPrev(pList, pNodeToAppend);
}

LT_INLINE void
LTList_InsertBefore(LTList_Node * pNodeExisting, LTList_Node * pNodeToInsert) LT_ISR_SAFE {
    /**< Link new node before existing node */
    LTList_LinkToPrev(pNodeExisting, pNodeToInsert);
}

LT_INLINE void
LTList_AddAfter(LTList_Node * pNodeExisting, LTList_Node * pNodeToAdd) LT_ISR_SAFE {
    /**< Link new node after existing node */
    LTList_LinkToNext(pNodeExisting, pNodeToAdd);
}

LT_INLINE void
LTList_Remove(LTList_Node * pNodeToRemove) LT_ISR_SAFE {
    pNodeToRemove->pNext->pPrev = pNodeToRemove->pPrev;
    pNodeToRemove->pPrev->pNext = pNodeToRemove->pNext;
    // Mark removed node as removed
    pNodeToRemove->pPrev = pNodeToRemove;
    pNodeToRemove->pNext = pNodeToRemove;
}

LT_INLINE bool
LTList_IsEmpty(LTList * pListToCheck) LT_ISR_SAFE {
    /**< Returns true if list is empty */
    return (pListToCheck->pPrev == pListToCheck);
}

LT_INLINE bool
LTList_IsNodeLinked(LTList_Node * pNodeToCheck) LT_ISR_SAFE {
    /**< Returns true if node is on a list */
    return (pNodeToCheck->pPrev != pNodeToCheck);
}

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTLIST_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  18-Jan-22   tiberius    created
 *  12-Feb-22   augustus    added more dox, added ForEach macro; fine-tuned some names
 *  06-Sep-24   augustus    added LTList_GetNodeDataOfType
 */
