/******************************************************************************
 * <lt/device/i2c/LTDeviceI2C.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

/**
 * @defgroup ltdevice_i2c LTDeviceI2C
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for I2C devices.
 */

#ifndef LT_INCLUDE_LT_DEVICE_I2C_LTDEVICEI2C_H
#define LT_INCLUDE_LT_DEVICE_I2C_LTDEVICEI2C_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

typedef enum LTDeviceI2C_Capabilities_Mask {
    kLTDeviceI2C_Capability_Dma        = 1 << 0, /**< I2C supports DMA */
    kLTDeviceI2C_Capability_Async      = 1 << 1, /**< I2C supports asynchronous operation */
    kLTDeviceI2C_Capability_Master     = 1 << 2, /**< I2C interface is bus master */
    kLTDeviceI2C_Capability_Slave      = 1 << 3, /**< I2C interface is endpoint */
    kLTDeviceI2C_Capability_Hardware   = 1 << 4, /**< I2C implemented with dedicated hardware */
    kLTDeviceI2C_Capability_BitBang    = 1 << 5, /**< I2C implemented with GPIO pins */
} LTDeviceI2C_Capabilities_Mask;

typedef struct LTDeviceI2C_Capabilities {
    u32              Freq_min;      /**< Minimum I2C clock frequency */
    u32              Freq_max;      /**< Maximum I2C clock frequency */
    u8               Caps_mask;     /**< I2C capabilities mask */
} LTDeviceI2C_Capabilities;

typedef struct LTDeviceI2C_Configuration {
    u32              Frequency;     /**< I2C clock frequency */
    bool             Master;        /**< true if I2C is master, false if endpoint */
    bool             Dma;           /**< true if I2C supports DMA */
    bool             Async;         /**< true if I2C supports asynchronous operation */
} LTDeviceI2C_Configuration;

#define I2C_CONFIG_CAPS_OK(pconfig, pcaps) \
    ((pconfig)->Frequency <= (pcaps)->Freq_max && (pconfig)->Frequency >= (pcaps)->Freq_min && \
        ((((pcaps)->Caps_mask & kLTDeviceI2C_Capability_Dma) && (pconfig)->Dma) || \
        (((pcaps)->Caps_mask & kLTDeviceI2C_Capability_Async) && (pconfig)->Async) || \
        (((pcaps)->Caps_mask & kLTDeviceI2C_Capability_Master) && (pconfig)->Master) || \
        (((pcaps)->Caps_mask & kLTDeviceI2C_Capability_Slave) && !(pconfig)->Master)))

typedef enum {
    kLTI2C_TransferStatus_Unknown = 0,  /**< Uninitialized and not in use */
    kLTI2C_TransferStatus_Starting,     /**< Transfer started */
    kLTI2C_TransferStatus_Error,        /**< Transfer error */
    kLTI2C_TransferStatus_Completed,    /**< Transfer completed */
} LTI2C_TransferStatus;

typedef void (LTI2C_I2CMasterTransferStatusCallback)(LTDeviceUnit unit, LTI2C_TransferStatus status, void * pClientData);
    /**< I2C transfer callback function
      *  @param[in] unit The Device Unit handle.
      *  @param[in] status Transfer status.
      *  @param[in] pClientData Pointer passed into SPIMasterTransfer call. */

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceI2C, 1);

/**
 * @brief The API for controlling I2C devices.
 */
struct LTDeviceI2C {

    INHERIT_DEVICE_LIBRARY_BASE

    u32  (* GetBusIndexFromName)(const char *busName);
        /**< Finds I2C bus device unit index for corresponding to the given name.
         *   @param[in]  busName The name of the desired I2C bus device unit.
         *   @return     the device unit index if found else LT_U32_MAX. */

    bool (* GetDeviceCapabilities)(LTDeviceUnit unit, LTDeviceI2C_Capabilities * pCaps);
        /**< Obtains I2C capabilities information.
         *   @param[in]  unit The Device Unit handle.
         *   @param[out] pCaps Pointer to the LTDeviceI2C_Capabilities structure allocated by the caller.
         *   @return     true if capabilities are read, false if not. */

    bool (* GetDeviceConfiguration)(LTDeviceUnit unit, LTDeviceI2C_Configuration * pI2CConfig);
        /**< Obtains current I2C device configuration.
         *   @param[in]  unit The Device Unit handle.
         *   @param[out] pI2CConfig Pointer to the LTDeviceI2C_Configuration structure allocated by the caller.
         *   @return     true if configuration is received, false if not. */

    bool (* SetDeviceConfiguration)(LTDeviceUnit unit, const LTDeviceI2C_Configuration * pI2CConfig);
        /**< Sets I2C device configuration.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] pI2CConfig Pointer to the LTDeviceI2C_Configuration structure allocated and initialized by the caller.
         *   @return    true if configuration is set, false if not. */

    void (* SetTransferTimeout)(LTDeviceUnit unit, LTTime timeout);
        /**< Sets I2C transfer timeout.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] timeout The timeout for transfers. */

    bool (* I2CMasterTransfer)(LTDeviceUnit unit, u8 address, void * pRXBuffer, u32 rxLen, const void * pTXBuffer, u32 txLen, bool issueStart, bool issueStop, LTI2C_I2CMasterTransferStatusCallback * pCallback, void * pClientData);
        /**< Starts I2C transfer in master mode, if both rx and tx buffers provided performs read after write.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] address I2C endpoint address.
         *   @param[in] rRxBuffer Pointer to the receive buffer, can be NULL if caller interested in transmit only.
         *   @param[in] rxLen Length of the rx buffer.
         *   @param[in] pTxBuffer Pointer to the transmit buffer, can be NULL if caller interested in received data only.
         *   @param[in] txLen Length of the tx buffer.
         *   @param[in] issueStart If true I2C start/repeated-start sequence will be issued on the bus before the transfer.
         *   @param[in] issueStop If true I2C stop sequence will be issued on the bus after the transfer.
         *   @param[in] pCallback Transfer status callback pointer.
         *   @param[in] pClientData Client data for the callback.
         *   @return    true if transfer setup is successful. */

    bool (* Reset)(LTDeviceUnit unit);
        /**< Resets I2C bus.
         *   @param[in] unit The Device Unit handle.
         *   @return    true if reset is successful. */

    bool (* ProbeAddress)(LTDeviceUnit unit, u8 address);
        /**< Probes I2C device address.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] address I2C slave address.
         *   @return    true if device is present on the bus. */

};

#ifndef DOXY_SKIP // [
typedef_LTLIBRARY_INTERFACE(ILTDriverI2C, 1) {
    /**< I2C Driver private interface - only to be used internally by LTDeviceI2C */

    u32  (* GetBusIndexFromName)(const char *busName);
        /**< Finds I2C bus device unit index for corresponding to the given name.
         *   @param[in]  busName The name of the desired I2C bus device unit.
         *   @return     the device unit index if found else LT_U32_MAX. */

    bool (* GetDeviceCapabilities)(LTDeviceUnit unit, LTDeviceI2C_Capabilities * pCaps);
        /**< Obtains I2C capabilities information.
         *   @param[in]  unit The Device Unit handle.
         *   @param[out] pCaps Pointer to the LTDeviceI2C_Capabilities structure allocated by the caller.
         *   @return     true if capabilities are read, false if not. */

    bool (* GetDeviceConfiguration)(LTDeviceUnit unit, LTDeviceI2C_Configuration * pI2CConfig);
        /**< Obtains current I2C device configuration.
         *   @param[in]  unit The Device Unit handle.
         *   @param[out] pI2CConfig Pointer to the LTDeviceI2C_Configuration structure allocated by the caller.
         *   @return     true if configuration is received, false if not. */

    bool (* SetDeviceConfiguration)(LTDeviceUnit unit, const LTDeviceI2C_Configuration * pI2CConfig);
        /**< Sets I2C device configuration.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] pI2CConfig Pointer to the LTDeviceI2C_Configuration structure allocated and initialized by the caller.
         *   @return    true if configuration is set, false if not. */

    void (* SetTransferTimeout)(LTDeviceUnit unit, LTTime timeout);
        /**< Sets I2C transfer timeout.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] timeout The timeout for transfers. */

    bool (* I2CMasterTransfer)(LTDeviceUnit unit, u8 address, void * pRXBuffer, u32 rxLen, const void * pTXbuffer, u32 txLen, bool issueStart, bool issueStop, LTI2C_I2CMasterTransferStatusCallback * pCallback, void * pClientData);
        /**< Starts I2C transfer in master mode, if both rx and tx buffers provided performs read after write.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] address I2C slave address.
         *   @param[in] pRXBuffer Pointer to the receive buffer, can be NULL if caller interested in transmit only.
         *   @param[in] rxLen Length of the rx buffer.
         *   @param[in] pTXBuffer Pointer to the transmit buffer, can be NULL if caller interested in received data only.
         *   @param[in] txLen Length of the tx buffer.
         *   @param[in] issueStart If true I2C start/repeated-start sequence will be issued on the bus before the transfer.
         *   @param[in] issueStop If true I2C stop sequence will be issued on the bus after the transfer.
         *   @param[in] pCallback Transfer status callback pointer.
         *   @param[in] pClientData Client data for the callback.
         *   @return    true if transfer setup is successful. */

    bool (* Reset)(LTDeviceUnit unit);
        /**< Resets I2C device.
         *   @param[in]  unit The Device Unit handle.
         *   @return     true if reset is successful. */

    bool (* ProbeAddress)(LTDeviceUnit unit, u8 address);
        /**< Probes I2C device address.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] address I2C endpoint address.
         *   @return    true if device is present on the bus. */

} LTLIBRARY_INTERFACE;
#endif // DOXY_SKIP ]

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_I2C_LTDEVICEI2C_H

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  23-Jun-21   titus       created
 *  30-Sep-21   commodus    added 'const' for tx_buffer
 */
