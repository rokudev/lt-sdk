/*******************************************************************************
 * <lt/system/ipc/buffermanager/LTIPCBufferManager.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Library for managing bulk data buffers between on-device SoCs.
 *
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_SYSTEM_IPC_BUFFERMANAGER_LTBUFFERMANAGER_H
#define LT_INCLUDE_LT_SYSTEM_IPC_BUFFERMANAGER_LTBUFFERMANAGER_H

#include <lt/utility/buffer/LTBuffer.h>

LT_EXTERN_C_BEGIN

#define LT_BUFFER_ID_INVALID 0
typedef u32 LTBufferID;
/**< Opaque identifier to reference an IPC-aware buffer.
 *
 * Can be freely passed around the system across multiple cores and used to
 * retrieve the underlying source buffer irrespective of which SoC it's on.
 */

typedef enum {
    LTIPCBufferManagerStatus_MoreData,
    LTIPCBufferManagerStatus_DataConsumed,
    LTIPCBufferManagerStatus_BufferLoss,
} LTIPCBufferManagerStatus;

typedef void (*LTIPCBufferManager_StatusProc)(LTBufferID bufferID, LTBuffer *ipcBuf, LTIPCBufferManagerStatus status, void *clientData);
/**< Callback function type for the buffer-specific status events.
 *
 * This callback is used to indicate MoreData, DataConsumed, and BufferLoss status events. The callback is
 * specific to the IPC-aware LTBuffer object that was used to register for the events, so in some cases the recipient
 * only needs to check the status parameter to see what has happened to the buffer.
 *
 * @param bufferID Identifier of the IPC buffer that triggered the event
 * @param ipcBuf Pointer to the IPC buffer object that triggered the event
 * @param status The status of the buffer event
 * @param clientData The client data that was specified when the callback was registered
 */

/* Wrapper object to create, access, and manage IPC-aware LTBuffer objects */
typedef_LTObject(LTIPCBufferManager, 1) {

    LTBufferID (*CreateRemoteSourceBuffer)(u32 capacity);
    /**< Create a buffer pinned to the remote side of the IPC link and return an opaque ID for it
     *
     * The returned ID can be passed over another IPC mechanism to another
     * entity on the other SoC, which will then use it to obtain access to the buffer from that side.
     *
     * @param capacity The desired capacity of the buffer.
     * @return Buffer ID, or LTBUFFER_ID_INVALID on error.
     */

    LTBufferID (*CreateLocalSourceBuffer)(u32 capacity);
    /**< Create a buffer pinned to the local side of the IPC link and return an opaque ID for it.
     *
     * The returned ID can be passed over another IPC mechanism to another
     * entity on the other SoC, which will then use it to obtain access to the buffer from that side.
     *
     * @param capacity The desired capacity of the buffer.
     * @return Buffer ID, or LTBUFFER_ID_INVALID on error.
     */

    bool (*DestroySourceBuffer)(LTBufferID bufferID);
    /**< Destroy a source buffer.
     *
     * The buffer can exist on either side of the IPC link, and may or may not have been reserved.
     * The underlying buffer storage will be freed up, and any linked LTBuffer object will be
     * unable to perform any further buffer operations. A BufferLoss event will be notified to any
     * registered parties.
     *
     * The buffer can be destroyed from either side of the IPC link by anyone that has the buffer ID.
     * It does not have to be the same entity that created the buffer.
     *
     * @param bufferID IPC buffer to be destroyed.
     * @return True if the buffer was found, else false. If the buffer was found it will be destroyed.
     */

    LTBuffer *(*AccessBuffer)(LTBufferID bufferID);
    /**< Return an LTBuffer object that can access the source buffer linked to the buffer ID.
     *
     * Only one user can access the buffer at a time - NULL (error) is returned if the buffer has already
     * been obtained on the local side and not destroyed.
     *
     * To release the buffer and allow another user to access it, destroy the returned buffer object.
     * This will not destroy the underlying source buffer - see the DestroySourceBuffer API call for that.
     *
     * @param bufferID IPC buffer to be accessed.
     * @return LTBuffer object wrapping the buffer storage, or NULL on error.
     */

    void (*OnStatusChange)(LTBuffer *ipcBuf, LTIPCBufferManager_StatusProc callback, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *clientData);
    /**< Register for status events (MoreData, DataConsumed, and BufferLoss) for the given IPC buffer object.
     *
     * MoreData status indicates more data has become available in the source buffer. Only one event will be generated
     * when the buffer contents go from empty to non-empty. Reading a portion of the available data will not cause
     * additional MoreData events to be generated if further data is added, unless the Read has first completely drained the buffer.
     *
     * DataConsumed status indicates a buffer read has caused remaining data in the buffer to drop below a fixed threshold.
     * Only one event will be generated when the buffer contents go from above the threshold to below the threshold. The
     * threshold is currently set at 50% of the source buffer capacity.
     *
     * The BufferLoss status indicates that the underlying source buffer (whether on the local or peer SoC) has been destroyed
     * for any reason. If this status is received then no further buffer operations on the IPC buffer object will succeed
     * and the caller is expected to call lt_destroyobject() on it.
     *
     * @param ipcBuf The IPC-aware buffer object to request status events for
     * @param callback The client's event callback procedure
     * @param pClientDataReleaseProc The client's clientData release proc that gets called when the client calls NoStatusChange to unregister, or when the object is destroyed with the client still registered
     * @param pClientData Client supplied optional context data that is passed back to 'callback'
     */

    void (*NoStatusChange)(LTBuffer *ipcBuf, LTIPCBufferManager_StatusProc callback);
    /**< De-register from buffer status events.
     *
     * @param ipcBuf The IPC buffer object to stop receiving status events for
     * @param callback The client's event callback procedure
     */

} LTOBJECT_API;

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_SYSTEM_IPC_BUFFERMANAGER_LTBUFFERMANAGER_H
