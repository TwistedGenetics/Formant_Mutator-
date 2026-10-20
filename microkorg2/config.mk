##############################################################################
# Configuration for Makefile (logue SDK 2.1, microKORG 2)
#

PROJECT := formant_mutator
PROJECT_TYPE := osc

##############################################################################
# Sources
#

# C sources (LUT sources as in Korg's waves/vox examples; units must embed them)
CSRC = header.c \
       $(realpath $(PLATFORM_COMMON_PATH)/dsp/tanpi_lut.c) \
       $(realpath $(PLATFORM_COMMON_PATH)/dsp/midi_to_hz_lut.c)

# C++ sources
CXXSRC = unit.cc

ACSRC =
ACXXSRC =
ASMSRC =
ASMXSRC =

##############################################################################
# Include Paths
#

# Shared DSP engine and parameter definitions (same code as the NTS-1 mkII unit)
UINCDIR = $(PROJECT_ROOT)/../formant_mutator

##############################################################################
# Library Paths
#

ULIBDIR =

##############################################################################
# Libraries
#

ULIBS  = -lm
ULIBS += -lc

##############################################################################
# Optimisation
#
# Korg's Makefile defaults to -Os (size). The DSP inner loop runs for every
# voice on every sample, so build for speed instead (unit stays far below
# the 48 KB limit). OPTIM is the Makefile's documented override.

OPTIM = -O3

##############################################################################
# Macros
#

UDEFS =
