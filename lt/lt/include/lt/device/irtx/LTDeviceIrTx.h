/*******************************************************************************
 * <lt/device/irtx/LTDeviceIrTx.h> LTDeviceIrTx
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_IRTX_LTDEVICEIRTX_H
#define LT_INCLUDE_LT_DEVICE_IRTX_LTDEVICEIRTX_H

#include <lt/LTTypes.h>
#include <lt/device/keypad/LTDeviceKeypad_KeyDefs.h>

LT_EXTERN_C_BEGIN

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceIrTx, 1);

struct LTDeviceIrTx {

    INHERIT_DEVICE_LIBRARY_BASE

    bool (*TransmitKey) (LTKey key, bool oneShot, bool isStb);
        /**<
         * @brief Transmit the specified key in Roku NEC format IR code.
         *        This function will block until the code has been sent.
         *
         * @param key     - The key value
         * @param oneShot - True for sending only a single initial packet.
         *                  False to send the initial and then subsequent
         *                  press-and-hold repeats until told to
         *                  FinishTransmission().
         * @param isStb   - True to specify Set Top Box codes, false to specify
         *                  Roku TV codes.
         *
         * @returns true on success, false otherwise.
         *
         */

    bool (*GetNECCodeFromKey) (LTKey key, bool isStb, u16 *system_code, u8 *cmd_code);
        /**<
         * @brief Lookup the specified key and return the Roku NEC format IR
         *        system_code and cmd_code values.  These codes can later be sent
         *        to the TransmitNEC function (below).
         *
         * @param key        - The key value.
         * @param isStb      - True to specify Set Top Box codes, false for Roku
         *                     TV codes.
         * @param systemCode - pointer to where the system code should be
         *                     written.
         * @param cmdCode    - pointer to where the cmd code should be written.
         *
         * @returns true on success, false otherwise.
         *
         */

    bool (*TransmitNEC)(u16 systemCode, u8 cmdCode, bool oneShot);
        /**<
         * @brief Transmit the specified NEC format IR code.
         *        This function will queue up the transmit request and return.
         *
         * @param systemCode - the 16-bit system code.
         * @param cmdCode    - the 8-bit cmd (button) code.
         * @param oneShot    - True for sending only a single initial packet.
         *                     False to send the initial and then subsequent
         *                     press-and-hold repeats until told to
         *                     FinishTransmission().
         *
         * @returns true on success, false otherwise.
         *
         */

    bool (*TransmitUniversal)(u8 const *data, u16 dataLength, bool oneShot);
        /**<
         * @brief Transmit the universal IR code from the specified data.
         *        This function will queue up the transmit request and return.
         *
         * @param data       - pointer to the universal data to transmit.
         * @param dataLength - length of the data to transmit.
         * @param oneShot    - True for sending only a single initial packet.
         *                     False to send the initial and then subsequent
         *                     press-and-hold repeats until told to
         *                     FinishTransmission().
         *
         * @returns true on success, false otherwise.
         *
         */

    bool (*FinishTransmission)(void);
        /**<
         * @brief Gracefully finish any currently transmitting waveforms.
         *
         * @returns true on success, false otherwise.
         *
         */

    bool (*IsTransmitInProgress)(void);
        /**<
         * @brief Checks to see if there is an IR transmit operation currently
         *        running.
         *
         * @returns true when transmitting, false otherwise.
         *
         */

};

TYPEDEF_LTLIBRARY_INTERFACE(ILTDriverIrTxDeviceUnit, 1);

struct ILTDriverIrTxDeviceUnitApi {

    INHERIT_INTERFACE_BASE

    void (*TransmitIRViaPWMStream)(u32 irCarrierFreq, float dutyCycle, u16 dataFrame[], u16 dataLength);
        /**<
         * @brief Transmit the specified array of PWM data using the platform's
         *        PWM hardware.
         *        This function will block until the IR waveform has been sent.
         *
         * @param irCarrierFreq - The IR carrier frequency to use for this transmission.
         * @param dutyCycle     - The duty cycle to use for this transmission (usually 0.33).
         * @param dataFrame     - The array of data frames to send via PWM.
         * @param dataLength    - The length of the dataFrame array.
         *
         */

    void (*TransmitNECViaHWBlock)(u32 necData);
        /**<
         * @brief Transmit the specified NEC format IR code using the platform's
         *        HW IR block (not via PWM).
         *        This function will block until the IR code has been sent.
         *
         * @param necData - the 32-bit NEC IR code to transmit.
         *
         */

};

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_IRTX_LTDEVICEIRTX_H */

