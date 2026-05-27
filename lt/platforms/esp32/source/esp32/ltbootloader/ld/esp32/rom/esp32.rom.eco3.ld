/*
ESP32 ECO3 ROM address table
Secure Boot Version 2 API's imported from the ROM
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
*/
PROVIDE ( ets_secure_boot_verify_signature = 0x4006543c);
PROVIDE ( ets_secure_boot_verify_boot_bootloader = 0x400655ec);
PROVIDE ( ets_use_secure_boot_v2 = 0x4000f8d4);
PROVIDE ( ets_rsa_pss_verify = 0x40065310);
PROVIDE ( ets_mgf1_sha256 = 0x400651a8);
PROVIDE ( ets_emsa_pss_verify = 0x4006520c);
