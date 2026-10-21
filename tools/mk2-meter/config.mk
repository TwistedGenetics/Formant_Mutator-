##############################################################################
# FMmeter - FMutator with a CPU load meter in the MODE display (diagnostic)
# Same DSP and glue as microkorg2/ (unit.cc is shared), built with -DFM_LOADMETER.
#

PROJECT := fmmeter
PROJECT_TYPE := osc

CSRC = header.c \
       $(realpath $(PLATFORM_COMMON_PATH)/dsp/tanpi_lut.c) \
       $(realpath $(PLATFORM_COMMON_PATH)/dsp/midi_to_hz_lut.c)
CXXSRC = unit.cc
ACSRC =
ACXXSRC =
ASMSRC =
ASMXSRC =
UINCDIR = $(PROJECT_ROOT)/../../formant_mutator
ULIBDIR =
ULIBS  = -lm
ULIBS += -lc
OPTIM = -O3
UDEFS = -DFM_LOADMETER
