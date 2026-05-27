/*******************************************************************************
 * <lt/device/sdio/LTDeviceSdio.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

/**
 * @defgroup ltdevice_sdio LTDeviceSdio
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for SDIO Host interfaces.
 */

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_SDIO_LTDEVICESDIO_H
#define ROKU_LT_INCLUDE_LT_DEVICE_SDIO_LTDEVICESDIO_H

#include <lt/core/LTCore.h>

LT_EXTERN_C_BEGIN

typedef struct {
    u16 vendorId;   /* four-digit hex assigned to a vendor by PCMCIA */
    u16 productId;  /* four-digit hex assigned to a card by the vendor */
} LTDeviceSdio_CardId;

struct LTSdioFunction;

typedef void (LTDeviceSdio_InterruptHandler)(struct LTSdioFunction *pFunction);

typedef bool (LTDeviceSdio_EnumerateCardIdsProc)(LTDeviceSdio_CardId cardId, void *pClientData);
    /**<
     * @brief Callback for enumerating available SDIO cards
     *
     * @param cardId      structure containing a vendor ID and a product ID for a card
     * @param pClientData client data supplied together with the callback of this type
     * @return            true to continue enumeration, false to stop */

typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceSdio, 1) {
    void (* EnumerateCardIds)(LTDeviceSdio_EnumerateCardIdsProc *pCardIdProc, void *pClientData);
    /**< Find all the available cards and pass their IDs to the caller
     *
     *   @param[in] pCardIdProc callback for passing card IDs to the function client
     *   @param[in] pClientData client data to pass when calling pCardIdProc from this function
     *
     *   Function clients can only manage the SDIO cards with known IDs because each card defines
     *   its own communication protocol by assigning different functionality to addresses in its
     *   I/O address space. A card ID is a declaration that a card follows a specific protocol.
     *   A function client requests a list of all available cards in order to find a card whose
     *   protocol is implemented by the client. */

} LTLIBRARY_INTERFACE;

typedef_LTObject(LTSdioFunction, 1) {
    bool (* AssignFunction)(LTSdioFunction *pThis, LTDeviceSdio_CardId cardId, u8 nFunctionId);
    /**< Associate this LTSdioFunction object with the function \a nId on the card with the given
     *   card ID
     *   @param[in] pThis      SDIO function
     *   @param[in] cardId     Card ID whose function is requested by the caller
     *   @param[in] functionId Function ID of the function to associate with pThis
     *   @return    \a true if a card and a function \a nId on the card are found,
     *              \a false if the card with the given ID is not available on any active SDIO Host
     *              interface, or if the card supports fewer functions than \a nId, or if there is
     *              already an object associated with this function.
     *
     *   The functions ConstructObject and AssignFunction are always called together in this
     *   specific order. This is a rule that accounts for not having an object-specific constructor,
     *   where object specific parameters are set.
     *
     *   Clients use an LTSdioFunction object to communicate to a specific function on the card
     *   on other side of a bus. Each client can communicate only with the functions on the cards
     *   with specific card IDs.
     *   The access to the function on the card is exclusive. If an LTSdioFunction object is already
     *   associated with the given function, no new LTSdioFunction objects can be associated with
     *   the same function. Only when the associated LTSdioFunction is destroyed, a new object can
     *   be associated with the function.
     *
     *   The assumption is that a function is uniquely identified by the card ID and the function's
     *   ID (order number) on the card. There may be cases where functions on a card have their own
     *   {vendorId, productId} pair, and their order is not enforced for all instances of the cards
     *   that share the same card ID.
     *   The SDIO spec allows it, but we will deal with it if we ever encounter such case. */

    bool  (* ReserveBus)(LTSdioFunction *pThis);
    /**< Reserve the SDIO Host interface and the bus for this LTSdioFunction object
     *   @param[in] pThis SDIO function
     *   @return    \a true if the bus is successfully reserved. This may fail if this object is not
     *              assigned to any function.
     *
     *   An SDIO card can support up to seven functions, and only one of them can use an SDIO Host
     *   interface and a bus at a time. SDIO Host interfaces do not queue up requests, and their
     *   low-level functions are not thread safe.
     *   The underlying synchronization primitives in this function and in its counterpart function
     *   ReleaseBus allow LTSdioFunction clients to implement critical sections in their code.
     *   It is a responsibility of the LTSdioFunction clients to use this pair of functions to
     *   coordinate the access to the bus. */

    bool  (* ReleaseBus)(LTSdioFunction *pThis);
    /**< Release a previously reserved SDIO Host interface and a bus
     *   @param[in] pThis  SDIO function
     *   @return    \a true if the bus is successfully released, \a false if a supplied object is
     *              not assigned to any function. */

    bool  (* DirectWrite)(LTSdioFunction *pThis, u32 nCardAddr, const u8 data);
    /**< Write one byte of data using the command path
     *   @param[in] pThis     SDIO function
     *   @param[in] nCardAddr card I/O space address
     *   @param[in] data      data to be written to the card
     *   @return    \a true if the write is successful
     *
     *   This function implements CMD52, only the CMD pin is used for transmission. */

    bool  (* DirectRead)(LTSdioFunction *pThis, u32 nCardAddr, u8 *pData);
    /**< Read one byte of data using the command path
     *   @param[in]  pThis     SDIO function
     *   @param[in]  nCardAddr card I/O space address
     *   @param[out] pData     location to write the data read from the card
     *   @return     \a true if the read is successful, \a false if the content in pData is not
     *               valid
     *
     *   This function implements CMD52, only the CMD pin is used for transmission. */

    bool  (* BufferWrite)(LTSdioFunction *pThis, u32 nCardAddr, const u8 *pSrc, LT_SIZE nSize);
    /**< Send the content of a buffer to a card using the data path
     *   @param[in]  pThis     SDIO function
     *   @param[in]  nCardAddr card I/O space address
     *   @param[in]  pSrc      pointer to a buffer in memory
     *   @param[in]  nSize     size of a buffer in memory
     *   @return     \a true if the write is successful
     *
     *   This function implements CMD53, DATA pins are used for transmission. */

    bool  (* BufferRead)(LTSdioFunction *pThis, u32 nCardAddr, u8 *pDest, LT_SIZE nSize);
    /**< Receive the data from the card using the data path and write it to a buffer
     *   @param[in]  pThis     SDIO function
     *   @param[in]  nCardAddr card I/O space address
     *   @param[out] pDest     pointer to a buffer in memory
     *   @param[in]  nSize     size of a buffer in memory
     *   @return     \a true if the read is successful, otherwise the content in pDest is not valid
     *
     *   This function implements CMD53, DATA pins are used for transmission. */

    bool  (* EnableFunction)(LTSdioFunction *pThis);
    /**< Enable the function assigned to this object
     *   @param[in]  pThis SDIO function
     *   @return     \a true if the function is successfully enabled
     *
     *   Transmission requests are accepted only if a function is enabled. */

    bool  (* DisableFunction)(LTSdioFunction *pThis);
    /**< Disable the function assigned to this object
     *   @param[in]  pThis SDIO function
     *   @return     \a true if the function is successfully disabled
     *
     *   Transmission requests to a disabled function are always rejected. */

    void  (* SetFunctionClientData)(LTSdioFunction *pThis, void *pClientData);
    /**< Client that created an LTSdioFunction object supplies a pointer to its private data
     *   @param[in]  pThis       SDIO function
     *   @param[in]  pClientData pointer to an opaque object owned by the client */

    void* (* GetFunctionClientData)(LTSdioFunction *pThis);
    /**< Return a pointer to the client's private data
     *   @param[in]  pThis SDIO function
     *   @return     pointer to an opaque object owned by the client */

    bool (* SetBlocksize)(LTSdioFunction *pThis, u32 nBlocksize);
    /**< Set transfer block size for a function
     *   @param[in]  pThis      SDIO function
     *   @param[in]  nBlocksize block size for this SDIO function
     *   @return                \a true if the block size is a valid number
     *
     *   For large transfer, the amount of data sent over the bus is expressed in number of blocks.
     *   Different SDIO drivers and interfaces may require that a block size is a multiple of a
     *   certain number of bytes.
     *   This number can be changed at any time between transfers. */

    u32 (* GetBlocksize)(LTSdioFunction *pThis);
    /**< Get transfer block size for a function
     *   @param[in]  pThis SDIO function
     *   @return           block size for this function */

    u32  (* GetBlocksizeCeiling)(LTSdioFunction *pThis, u32 nByteLen);
    /**< Find the lowest number of blocks that can accommodate a buffer of the supplied length and
     *   return the number of total bytes required
     *   @param[in] pThis    SDIO function
     *   @param[in] nByteLen length of a buffer that needs to fit into a number of blocks
     *   @return    number of bytes that is a multiple of the function's block size and can
     *              accommodate a buffer of the size \a nByteLen
     *
     *   SDIO clients may use this function to ensure that they are always sending or receiving
     *   multiple complete blocks of data. That avoids a possible use of fractional transfers that
     *   may need to be performed without DMA and slow the transfer. */

    bool (* SetInterruptHandler)(LTSdioFunction *pThis, LTDeviceSdio_InterruptHandler *pHandler);
    /**< Set an interrupt handler for a function
     *   @param[in]  pThis    SDIO function
     *   @param[in]  pHandler handler supplied by the client
     *   @return     \a true if the handler is successfully set
     *
     *   An interrupt from a card is a signal that there is a content to be read from the card.
     *   The SDIO driver will read the card status to determine which function on the card requested
     *   a read transaction, and then it will call the handler for the corresponding LTSdioFunction
     *   object. The handler normally queues up a read transaction to be executed after the
     *   interrupt. */

    bool (* RemoveInterruptHandler)(LTSdioFunction *pThis);
    /**< Remove the interrupt handler for a function
     *   @param[in]  pThis SDIO function
     *   @return     \a true if the handler is successfully removed */

    bool (* EnableInterrupts)(LTSdioFunction *pThis);
    /**< Enable function interrupts
     *   @param[in]  pThis  SDIO function
     *   @return     \a true if the interrupts are successfully enabled */

    bool (* DisableInterrupts)(LTSdioFunction *pThis);
    /**< Disable function interrupts
     *   @param[in]  pThis  SDIO function
     *   @return     \a true if the interrupts are successfully disabled */

    void (* Abort)(LTSdioFunction *pThis);
    /**< Abort any current transfer and bring the function and/or card in the initialized state
     *   @param[in]  pThis  SDIO function
     *
     *   The actual implementation of this API may differ depending whether the device is a host or
     *   a secondary device. In some cases a function transfer can be aborted without reinitializing
     *   the card, while in other cases the complete initialization may be needed. But, it is always
     *   possible to detect which function is holding the bus, and this function should be called
     *   only for that function. */

} LTOBJECT_API;

/** @} */

LT_EXTERN_C_END

#endif /* #ifndef ROKU_LT_INCLUDE_LT_DEVICE_SDIO_LTDEVICESDIO_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  20-Mar-24   commodus    created
 */
