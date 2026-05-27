################################################################################
# LTUtilityMicrophone.mk
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR     := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/media/microphone
LT_PROJECT_SOURCE_SUBDIRS += agc ns

LT_PROJECT_SOURCE_FILES   += LTMediaMicrophone.c

LT_PROJECT_SOURCE_FILES   += agc/LTMediaMicrophoneAGC.c
LT_PROJECT_SOURCE_FILES   += agc/analog_agc.c
LT_PROJECT_SOURCE_FILES   += agc/digital_agc.c
LT_PROJECT_SOURCE_FILES   += agc/dot_product_with_scale.c
LT_PROJECT_SOURCE_FILES   += agc/resample_by_2.c
LT_PROJECT_SOURCE_FILES   += agc/division_operations.c
LT_PROJECT_SOURCE_FILES   += agc/copy_set_operations.c
LT_PROJECT_SOURCE_FILES   += agc/spl_sqrt.c

LT_PROJECT_SOURCE_FILES   += ns/LTMediaMicrophoneNS.c
LT_PROJECT_SOURCE_FILES   += ns/noise_suppression_x.c
LT_PROJECT_SOURCE_FILES   += ns/nsx_core.c
LT_PROJECT_SOURCE_FILES   += ns/nsx_core_c.c
LT_PROJECT_SOURCE_FILES   += ns/min_max_operations.c
LT_PROJECT_SOURCE_FILES   += ns/real_fft.c
LT_PROJECT_SOURCE_FILES   += ns/complex_fft.c
LT_PROJECT_SOURCE_FILES   += ns/spl_sqrt_floor.c
LT_PROJECT_SOURCE_FILES   += ns/energy.c
LT_PROJECT_SOURCE_FILES   += ns/complex_bit_reverse.c
LT_PROJECT_SOURCE_FILES   += ns/get_scaling_square.c

include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#