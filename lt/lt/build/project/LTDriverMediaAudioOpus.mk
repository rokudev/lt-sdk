################################################################################
# LTDriverMediaAudioOpus.mk - Opus audio codec driver
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

LT_PROJECT_SOURCE_DIR        := $(LT_ROOTS_BASE)/lt/source/lt/driver/media/audio/opus
OPUS_ROOT_DIR                := ${LT_ROOTS_BASE}/lt/thirdparty/opus

LT_PROJECT_SOURCE_FILES      += LTDriverMediaAudioOpus.c
LT_PROJECT_SOURCE_FILES      += Encoder.c Decoder.c

LT_PUBLIC_INCLUDE_FLAGS      += -I${OPUS_ROOT_DIR}/include

# # Treatment for nimble code
# LT_CFLAGS_GENERIC += -Wno-unused-parameter -Wno-sign-compare -Wno-old-style-declaration -Wno-pointer-arith

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   20-Nov-23   trajan      created
