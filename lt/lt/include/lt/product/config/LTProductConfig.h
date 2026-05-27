/******************************************************************************
 * lt/include/lt/product/config/LTProductConfig.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_PRODUCT_CONFIG_LTPRODUCTCONFIG_H
#define LT_INCLUDE_LT_PRODUCT_CONFIG_LTPRODUCTCONFIG_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

typedef_LTLIBRARY_ROOT_INTERFACE(LTProductConfig, 1) {

    u32 (* GetLibraryConfigSection)(const char *libraryName);
        /**< gets an identifier representing the section in the product config tree reserved for the specified library name
          *
          *  call %GetLibrarySection() to return an identifier for the section in the product config tree reserved
          *  for the named library
          *
          *  @param libraryName the name of the library to get the product config tree section for
          *  @return an identifier representing the section or 0 if no section exists in the tree for the specified library name
          *
          *  @note The return value is only valid while the LTProductConfig library is loaded.
          *
          */

    const char * (*ReadString)(u32 section, const char *key);
        /**< reads a string from a section in the product config tree
          *
          *  @param section the product config tree section to read the string from
          *  @param key the key name of the variable to read
          *  @return the string value of the variable or NULL if no such variable exists
          *  @note the returned string is only valid while the LTProductConfig library is loaded
          *        either leave the library open, copy the string, or use and abandon the return value before closing the library
          */

    s64 (*ReadInteger)(u32 section, const char *key);
        /**< reads an integer from a section in the product config tree
          *
          *  @param section the product config tree section to read the integer from
          *  @param key the key name of the integer variable to read
          *  @return the value of the variable or 0 if no such integer variable exists
          */

    bool (*IsIntegerZero)(u32 section, const char *key);
        /**< disambiguates return value from ReadInteger
          *
          *  ReadInteger will return 0 if the key is in the product config and is 0,
          *  and also to indicate the integer key was not in the product config.
          *  Usually this is fine, but sometimes knowing the difference between 0 and not present is important.
          *  This function enables that, if ReadInteger returns 0 then call IsIntegerZero - it will return true
          *  if the integer key is present with value 0, false if the key is not found or is non-zero
          *
          *  @param section the product config tree section to read the integer from
          *  @param key the key name of the integer variable to read
          *  @return true if the variable exists and is zero, false otherwise
          */

    const u8 * (*ReadBinary)(u32 section, const char *key, u32 *sizeToSet);
        /**< reads binary data from the product config tree
          *
          *  @param section the product config tree section to read the binary data from
          *  @param key the key name of the binary variable to read
          *  @param sizeToSet a pointer to a u32 that will receive the size of the binary data or NULL if size information is not needed
          *  @return a pointer to the binary data in the device tree or NULL if the requested binary data variable does not exist
          *  @note the returned pointer is only valid while the LTProductConfig library is loaded
          *        either leave the library open, copy the data, or use the data and abandon the returned pointer before closing the library
          */
    
    bool (*ReadFirstItemInArray)(u32 section, const char *key, LTResourceValue *value);
    bool (*ReadNextItemInArray)(LTResourceValue *value);
        /**< reads array data from the product config tree
          *
          *  @param section the product config tree section to read the array data from
          *  @param key     the key name of the array to read
          *  @param value   the resource value of the item in array
          *  @return  true on success
          *           false on failure or no more items in array
          * 
          *  @note  Usage example:
          *
          *         u32 section = productConfig->GetLibraryConfigSection("library");
          *         LTResourceValue value;
          *         if (productConfig->ReadFirstItemInArray(section, "array_key", &value)) {
          *             ...;  // process the first item (value) in the array
          *             while (productConfig->ReadNextItemInArray(&value)) {
          *                 ...;  // process each following item (value), in the array
          *             }
          *         }
          */

} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_PRODUCT_CONFIG_LTPRODUCTCONFIG_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  05-Apr-23   trajan      created
 */
