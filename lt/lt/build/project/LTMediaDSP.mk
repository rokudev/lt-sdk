################################################################################
# LTMediaDSP - project makefile for LT Media DSP Utilities
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
################################################################################

# source dir and files
LT_PROJECT_SOURCE_DIR     := $(LT_PROJECT_SOURCE_DIR_BASE)/lt/media/dsp
LT_PROJECT_SOURCE_SUBDIRS += speex

LT_PROJECT_SOURCE_FILES   += LTMediaDSP.c
LT_PROJECT_SOURCE_FILES   += LTMediaDSPIntegerFFT.c
LT_PROJECT_SOURCE_FILES   += LTMediaDSPPWMRender.c
LT_PROJECT_SOURCE_FILES   += LTEchoCancel.c
LT_PROJECT_SOURCE_FILES   += LTResample.c
LT_PROJECT_SOURCE_FILES   += speex/mdf.c
LT_PROJECT_SOURCE_FILES   += speex/fftwrap.c
LT_PROJECT_SOURCE_FILES   += speex/kiss_fft.c
LT_PROJECT_SOURCE_FILES   += speex/kiss_fftr.c
LT_PROJECT_SOURCE_FILES   += speex/preprocess.c
LT_PROJECT_SOURCE_FILES   += speex/filterbank.c
LT_PROJECT_SOURCE_FILES   += speex/resample.c

# force the use of the generic KISS FFT as
# the platforms generally don't have NEON instructions
LT_CFLAGS_GENERIC += -DBUILD_KISS_FFT
LT_CFLAGS_GENERIC += -DUSE_KISS_FFT
LT_CFLAGS_GENERIC += -DFIXED_POINT

# make
include $(LT_PROJECT_RULES_MAKEFILE)

###############################################################################
#   LOG
###############################################################################
#   31-May-23   diocletian  created
