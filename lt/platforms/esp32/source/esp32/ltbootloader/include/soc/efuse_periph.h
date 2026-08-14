// Copyright 2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "soc/efuse_reg.h"
/* Upstream also pulls in the register-struct view of the block here.  Only the
   esp32s3 side of this tree vendors it: the esp32 hal predates the struct headers
   and reaches the block through the _REG macros above, so there is nothing to
   include on that target. */
#if CONFIG_IDF_TARGET_ESP32S3
#include "soc/efuse_struct.h"
#endif
