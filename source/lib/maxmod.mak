#---------------------------------------------------------------------------------
# maxmod.mak - devkitARM build shim for BlocksDS maxmod
#
# Lives in source/lib/, builds sources from maxmod/ without modifying it.
# Injects devkitpro_compat.h via -include so no library source files need editing.
#
# Usage (same interface as the original maxmod_nds/maxmod.mak):
#   make -C lib -f maxmod.mak SYSTEM=DS7
#   make -C lib -f maxmod.mak SYSTEM=DS9
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

ifeq ($(SYSTEM),)
$(error "Please specify SYSTEM: DS7, DS9")
endif

include $(DEVKITARM)/ds_rules

# Path to the pristine BlocksDS maxmod clone (CURDIR is correct in both passes
# for MMDIR since SOURCEDIRS / INCLUDES are only used in the outer pass).
MMDIR		:= $(CURDIR)/maxmod

#---------------------------------------------------------------------------------
# Per-system source directories and output library
#---------------------------------------------------------------------------------
ifeq ($(SYSTEM),DS7)
SOURCEDIRS	:= $(MMDIR)/source/core $(MMDIR)/source/ds/common $(MMDIR)/source/ds/arm7
BUILD		:= maxmod_build_ds7
TARGET		:= $(TOPDIR)/build/libmm7.a
DEFS		:= -D__NDS__ -DARM7
ARCH		:= -mcpu=arm7tdmi -mtune=arm7tdmi -mthumb -mthumb-interwork
GOODSYSTEM	:= YES
endif

ifeq ($(SYSTEM),DS9)
SOURCEDIRS	:= $(MMDIR)/source/ds/common $(MMDIR)/source/ds/arm9
BUILD		:= maxmod_build_ds9
TARGET		:= $(TOPDIR)/build/libmm9.a
DEFS		:= -D__NDS__ -DARM9
ARCH		:= -mthumb-interwork -march=armv5te -mtune=arm946e-s
GOODSYSTEM	:= YES
endif

ifneq ($(GOODSYSTEM),YES)
$(error "Invalid SYSTEM, valid systems are: DS7, DS9")
endif

INCLUDES	:= $(MMDIR)/include $(MMDIR)/source

#---------------------------------------------------------------------------------
# Compiler / assembler flags
# -include $(COMPAT_HDR) force-includes the devkitPro compat shim before every
# translation unit, bridging BlocksDS and devkitPro libnds differences.
#---------------------------------------------------------------------------------
CFLAGS		:= -g -Wall -O2 -fomit-frame-pointer \
		   -ffunction-sections -fdata-sections \
		   -include $(COMPAT_HDR) \
		   $(DEFS) $(ARCH) $(INCLUDE)

ASFLAGS		:= -g -x assembler-with-cpp \
		   -ffunction-sections -fdata-sections \
		   $(DEFS) $(ARCH) $(INCLUDE)

#---------------------------------------------------------------------------------
# Recurse into BUILD directory
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export TOPDIR	:= $(CURDIR)
export DEPSDIR	:= $(CURDIR)/$(BUILD)
export COMPAT_HDR := $(CURDIR)/../include/devkitpro_compat.h

export VPATH	:= $(SOURCEDIRS)

CFILES		:= $(foreach dir,$(SOURCEDIRS),$(notdir $(wildcard $(dir)/*.c)))
SFILES		:= $(foreach dir,$(SOURCEDIRS),$(notdir $(wildcard $(dir)/*.s)))

export OFILES	:= $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE	:= $(foreach dir,$(INCLUDES),-I$(dir)) \
		   -I$(LIBNDS)/include \
		   -I$(CURDIR)/$(BUILD)

export LD	:= $(CC)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@[ -d build ] || mkdir -p build
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/maxmod.mak

clean:
	@echo clean maxmod $(SYSTEM) ...
	@rm -fr $(BUILD)

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS	:= $(OFILES:.o=.d)

$(TARGET): $(OFILES)
	@rm -f "$(TARGET)"
	@$(AR) -rc "$(TARGET)" $(OFILES)
	@echo built ... maxmod $(SYSTEM) lib

-include $(DEPENDS)

endif
