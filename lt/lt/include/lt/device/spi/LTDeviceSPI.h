/*******************************************************************************
 * <lt/device/spi/LTDeviceSPI.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltdevice_spi LTDeviceSPI
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for SPI buses
 */

#ifndef LT_INCLUDE_LT_DEVICE_SPI_LTDEVICESPI_H
#define LT_INCLUDE_LT_DEVICE_SPI_LTDEVICESPI_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

typedef enum LTDeviceSPI_Capabilities_Mask {
    kLTDeviceSPI_Capability_Dma        = 1 << 0, /**< SPI supports DMA */
    kLTDeviceSPI_Capability_Async      = 1 << 1, /**< SPI supports asynchronous operation */
    kLTDeviceSPI_Capability_Master     = 1 << 2, /**< SPI interface is bus master */
    kLTDeviceSPI_Capability_Slave      = 1 << 3, /**< SPI interface is endpoint */
    kLTDeviceSPI_Capability_Hardware   = 1 << 4, /**< SPI implemented with dedicated hardware */
    kLTDeviceSPI_Capability_BitBang    = 1 << 5, /**< SPI implemented with GPIO pins */
} LTDeviceSPI_Capabilities_Mask;

typedef enum LTDeviceSPI_Mode {
    kLTDeviceSPI_Mode_0    = 0, /**< SPI Mode CPOL=0 (Clock 0 at idle), CPHA=0 (data in on leading clk edge, out on trailing) */
    kLTDeviceSPI_Mode_1    = 1, /**< SPI Mode CPOL=0 , CPHA=1 (data in on trailing clk edge, out on leading) */
    kLTDeviceSPI_Mode_2    = 2, /**< SPI Mode CPOL=1 (Clock 1 at idle) , CPHA=0 */
    kLTDeviceSPI_Mode_3    = 3, /**< SPI Mode CPOL=1 , CPHA=1 */
} LTDeviceSPI_Mode;

typedef struct LTDeviceSPI_Capabilities {
    u32              Freq_min;  /**< Minimum SPI clock frequency */
    u32              Freq_max;  /**< Maximum SPI clock frequency */
    u8               Bits_min;  /**< Minimum SPI bus bit-width */
    u8               Bits_max;  /**< Maximum SPI bus bit-width */
    u8               Caps_mask; /**< SPI capabilities mask */
} LTDeviceSPI_Capabilities;

typedef struct LTDeviceSPI_Configuration {
    u32              Frequency; /**< SPI clock frequency */
    u8               Bits;      /**< SPI bus bit-width */
    LTDeviceSPI_Mode Mode;      /**< SPI bus Mode */
    bool             Master;    /**< true if SPI is master, false if endpoint */
    bool             Dma;       /**< true if SPI supports DMA */
    bool             Async;     /**< true if SPI supports asynchronous operation */
} LTDeviceSPI_Configuration;

#define SPI_CONFIG_CAPS_OK(pconfig, pcaps) \
    ((pconfig)->Frequency <= (pcaps)->Freq_max && (pconfig)->Frequency >= (pcaps)->Freq_min && \
        (pconfig)->Bits <= (pcaps)->Bits_max && (pconfig)->Bits >= (pcaps)->Bits_min && \
        ((((pcaps)->Caps_mask & kLTDeviceSPI_Capability_Dma) && (pconfig)->Dma) || \
        (((pcaps)->Caps_mask & kLTDeviceSPI_Capability_Async) && (pconfig)->Async) || \
        (((pcaps)->Caps_mask & kLTDeviceSPI_Capability_Master) && (pconfig)->Master) || \
        (((pcaps)->Caps_mask & kLTDeviceSPI_Capability_Slave) && !(pconfig)->Master)))

typedef enum {
    kLTSPI_TransferStatus_Unknown = 0,  /**< Uninitialized and not in use */
    kLTSPI_TransferStatus_Starting,     /**< Transfer started */
    kLTSPI_TransferStatus_Error,        /**< Transfer error */
    kLTSPI_TransferStatus_Completed,    /**< Transfer completed */
} LTSPI_TransferStatus;

typedef void (LTSPI_SPIMasterTransferStatusCallback)(LTDeviceUnit unit, LTSPI_TransferStatus status, void * pClientData);
    /**< SPI transfer callback function.
      *  @param[in] unit The Device Unit handle.
      *  @param[in] status Transfer status.
      *  @param[in] pClientData Pointer passed into SPIMasterTransfer call. */

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceSPI, 1);

/** SPI Device public interface. */
struct LTDeviceSPI {

    INHERIT_DEVICE_LIBRARY_BASE

    u32  (* GetBusIndexFromName)(const char *busName);
        /**< Finds SPI bus device unit index for corresponding to the given name.
         *   @param[in]  busName The name of the desired SPI bus device unit.
         *   @return     the device unit index if found else LT_U32_MAX. */

    bool (* GetDeviceCapabilities)(LTDeviceUnit unit, LTDeviceSPI_Capabilities * pCaps);
        /**< Obtains SPI capabilities information.
         *   @param[in]  unit The Device Unit handle.
         *   @param[out] pCaps Pointer to the LTDeviceSPI_Capabilities structure allocated by the caller.
         *   @return     true if capabilities are read, false if not. */

    bool (* GetDeviceConfiguration)(LTDeviceUnit unit, LTDeviceSPI_Configuration * pSPIConfig);
        /**< Obtains current SPI device configuration.
         *   @param[in]  unit The Device Unit handle.
         *   @param[out] pSPIConfig Pointer to the LTDeviceSPI_Configuration structure allocated by the caller.
         *   @return     true if configuration is received, false if not. */

    bool (* SetDeviceConfiguration)(LTDeviceUnit unit, const LTDeviceSPI_Configuration * pSPIConfig);
        /**< Sets SPI device configuration.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] pSPIConfig Pointer to the LTDeviceSPI_Configuration structure allocated and initialized by the caller.
         *   @return    true if configuration is set, false if not. */

    bool (* SPIMasterTransfer)(LTDeviceUnit unit, u8 * pRXBuffer, u8 * pTXBuffer, u32 buffLen, LTSPI_SPIMasterTransferStatusCallback * pCallback, void * pClientData);
        /**< Starts SPI transfer in master mode.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] pRXBuffer Pointer to the receive buffer, can be NULL if caller interested in transmit only.
         *   @param[in] pTXBuffer Pointer to the transmit buffer, can be NULL if caller interested in received data only.
         *   @param[in] buffLen Length of the buffers above.
         *   @param[in] pCallback Transfer status callback pointer.
         *   @param[in] pClientData Client data for the callback.
         *   @return    true if transfer parameters are set, false if not. */

};

#ifndef DOXY_SKIP // [
typedef_LTLIBRARY_INTERFACE(ILTDriverSPI, 1) {
    /**< SPI Driver private interface - only to be used internally by LTDeviceSPI */

    u32  (* GetBusIndexFromName)(const char *busName);
        /**< Finds SPI bus device unit index for corresponding to the given name.
         *   @param[in]  busName The name of the desired SPI bus device unit.
         *   @return     the device unit index if found else LT_U32_MAX. */

    bool (* GetDeviceCapabilities)(LTDeviceUnit unit, LTDeviceSPI_Capabilities * pCaps);
        /**< Obtains SPI capabilities information.
         *   @param[in]  unit The Device Unit handle.
         *   @param[out] pCaps Pointer to the LTDeviceSPI_Capabilities structure allocated by the caller.
         *   @return     true if capabilities are read, false if not. */

    bool (* GetDeviceConfiguration)(LTDeviceUnit unit, LTDeviceSPI_Configuration * pSPIConfig);
        /**< Obtains current SPI device configuration.
         *   @param[in]  unit The Device Unit handle.
         *   @param[out] pSPIConfig Pointer to the LTDeviceSPI_Configuration structure allocated by the caller.
         *   @return     true if configuration is read, false if not. */

    bool (* SetDeviceConfiguration)(LTDeviceUnit unit, const LTDeviceSPI_Configuration * pSPIConfig);
        /**< Sets SPI device configuration.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] pSPIConfig Pointer to the LTDeviceSPI_Configuration structure allocated and initialized by the caller.
         *   @return    true if configuration is set, false if not. */

    bool (* SPIMasterTransfer)(LTDeviceUnit unit, u8 * pRXBuffer, u8 * pTXBuffer, u32 buffLen, LTSPI_SPIMasterTransferStatusCallback * pCallback, void * pClientData);
        /**< Starts SPI transfer in master mode.
         *   @param[in] unit The Device Unit handle.
         *   @param[in] pRXBuffer Pointer to the receive buffer, can be NULL if caller interested in transmit only.
         *   @param[in] pTXBuffer Pointer to the transmit buffer, can be NULL if caller interested in received data only.
         *   @param[in] buffLen Length of the buffers above.
         *   @param[in] pCallback Transfer status callback pointer.
         *   @param[in] pClientData Client data for the callback.
         *   @return    true if transfer parameters are set, false if not. */

} LTLIBRARY_INTERFACE;
#endif // DOXY_SKIP ]

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_SPI_LTDEVICESPI_H

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jun-21   titus       created
 */
