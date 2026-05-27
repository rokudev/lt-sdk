/*******************************************************************************
 * <lt/device/efuse/LTDeviceEfuse.h> LTDeviceEfuse
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DEVICE_EFUSE_LTDEVICEEFUSE_H
#define LT_INCLUDE_LT_DEVICE_EFUSE_LTDEVICEEFUSE_H

#include <lt/LTTypes.h>
#include <lt/system/shell/LTShell.h>


LT_EXTERN_C_BEGIN

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceEfuse, 1);

struct LTDeviceEfuse {
    INHERIT_DEVICE_LIBRARY_BASE
};

TYPEDEF_LTLIBRARY_INTERFACE(ILTDriverEfuseDeviceUnit, 1);

struct ILTDriverEfuseDeviceUnitApi {

    INHERIT_INTERFACE_BASE

    /* This first section is just for reading the whole EFUSE area.  Used typically
     * for dumping it all out from a factory command, useful for confirming the
     * correct state of the EFUSE
     */

    u16 (*GetNumberOfEfuseAreaBytes)(void);
        /**<
         * @brief Get the number of bytes in the EFUSE area on this platform.
         *
         * @returns The number of EFUSE bytes on the system.
         */

    bool (*GetEfuseByte)(u16 efuseOffset, u8 *data);
        /**<
         * @brief Get the single 8-bit byte located at the specified offset
         *
         * @param efuseOffset - The offset into the EFUSE area (ranging from 0 to GetNumberOfEfuseAreaBytes - 1)
         * @param data        - The pointer to the output byte
         *
         * @returns true if successful. false otherwise.
         */

    bool (*GetEfuseDword)(u16 efuseOffset, u32 *data);
        /**<
         * @brief Get the single 32-bit word located at the specified offset
         *
         * @param efuseOffset - The word offset into the EFUSE area (ranging from 0 to GetNumberOfEfuseAreaBytes / 4)
         * @param data        - The pointer to the output u32
         *
         * @returns true if successful. false otherwise.
         */

    /* This second section is for reading and writing the specific fields that are
     * defined within the EFUSE area on this platform.  Needed for getting these
     * values in from the EFUSE for Application use, and for setting them on the
     * factory line.
     */

    u16 (*GetNumberOfEfuseFields)(void);
        /**<
         * @brief Get the number of defined fields in the EFUSE on this platform.
         *
         * @returns The number of fields
         */

    char const * (*GetEfuseFieldName)(u16 efIndex);
        /**<
         * @brief Get the name string of the specified EFUSE field
         *
         * @param efIndex - The index of this EFUSE field (ranging from 0 to GetNumberOfEfuseFields - 1)
         *
         * @returns The string name of the EFUSE field (or NULL on error)
         */

    u8 (*GetEfuseFieldLength)(u16 efIndex);
        /**<
         * @brief Get the length in bytes, of the specified EFUSE field
         *
         * @param efIndex - The index of this EFUSE field (ranging from 0 to GetNumberOfEfuseFields - 1)
         *
         * @returns The length of the EFUSE field (or 0 on error)
         */

    bool (*GetEfuseFieldData)(u16 efIndex, void *inputData);
        /**<
         * @brief Get the data from (meaning read from) the specified EFUSE Field
         *
         * @param efIndex - The index of this EFUSE (ranging from 0 to GetNumberOfEfuses - 1)
         * @param inputData - The location that this EFUSE field should be written to.
         *                    Make sure this points to data of the correct length for this field.
         *
         * @returns Success
         */

    bool (*SetEfuseFieldData)(u16 efIndex, void const *outputData);
        /**<
         * @brief Set the data for (meaning write to) the specified EFUSE Field
         *
         * @param efIndex - The index of this EFUSE (ranging from 0 to GetNumberOfEfuses - 1)
         * @param outputData - The location containing the data to be written to this EFUSE field
         *
         * @returns Success
         */

    void (*SetRealEfuseMode)(bool enable);
        /**<
         * @brief Enable the real eFuse access. If this is disabled, the eFuse will be
         *       simulated in memory, and writes do not end up on the real eFuse.
         */

    bool (*GetRealEfuseMode)(void);
        /**<
         * @returns true if the real eFuse is read from and written to,
         *          false if the test mode is used.
         */

    s16 (*GetEfuseFieldIndexFromName)(char const *name);
        /**<
         * @brief Get the index of the EFUSE field by its string name
         *
         * @param name - The string name of the EFUSE field index to get
         *
         * @returns The index of the EFUSE field (or -1 on error)
         */

    u8 (*GetEfuseUnprogrammedValue)(void);
        /**<
         * @returns the value that a byte of efuse will contain if it has not been
         *          programmed yet (either 0x00 or 0xFF).
         */

    bool (*ValidateEfuseFields)(void);
        /**<
         * @returns true if all the required efuse fields (for this platform) have
         *          been set correctly.
         *          false if any of have not been set correctly.
         */

    bool (*DisableEfuseWriting)(void);
        /**<
         * @returns true if efuse writing (for this platform) has been disabled correctly.
         *          false if disabling efuse writing fails.
         */
};

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_EFUSE_LTDEVICEEFUSE_H */
