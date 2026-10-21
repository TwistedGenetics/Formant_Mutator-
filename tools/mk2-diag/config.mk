##############################################################################
# FMdiag - microKORG 2 diagnostic oscillator (see README in this folder)
#

PROJECT := fmdiag
PROJECT_TYPE := osc

CSRC = header.c \
       $(realpath $(PLATFORM_COMMON_PATH)/dsp/tanpi_lut.c) \
       $(realpath $(PLATFORM_COMMON_PATH)/dsp/midi_to_hz_lut.c)
CXXSRC = unit.cc
ACSRC =
ACXXSRC =
ASMSRC =
ASMXSRC =
UINCDIR =
ULIBDIR =
ULIBS  = -lm
ULIBS += -lc
UDEFS =
