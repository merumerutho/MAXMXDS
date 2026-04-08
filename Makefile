#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

GAME_TITLE     := MAXMXDS
GAME_SUBTITLE1 := xm / mod dj tool
GAME_SUBTITLE2 := based on maxmod

export TARGET	:=	$(shell basename $(GAME_TITLE))
export TOPDIR	:=	$(CURDIR)

NITRO_FILES	:= ./source/data

include $(DEVKITARM)/ds_rules

SRCDIR  := source
MAXMOD  := $(SRCDIR)/lib

# Song conversion
SONGS_DIR   := songs
DATA_DIR    := $(SRCDIR)/data
NATIVE_DIR  := release/data
MAS_SCRIPT  := $(SRCDIR)/scripts/mas.py

.PHONY: maxmod_ds7 maxmod_ds9 build_arm7 build_arm9 \
        songs emulator native clean clean_data clean_native

#---------------------------------------------------------------------------------
# Main targets
#
#   make songs    — convert tracker files to .mas in source/data/
#   make emulator — build .nds with songs embedded in NitroFS (for emulators)
#   make native   — build .nds (empty NitroFS) + songs in release/data/ (flashcarts)
#   make clean    — remove all build artifacts and converted songs
#---------------------------------------------------------------------------------
all: help

help:
	@echo ""
	@echo "  MAXMXDS build targets"
	@echo "  ---------------------"
	@echo "  make emulator  - convert songs + build .nds with NitroFS (for emulators)"
	@echo "  make native    - convert songs + build .nds + release/data/ (for flashcarts)"
	@echo "  make songs     - convert tracker files in songs/ only (no build)"
	@echo "  make clean     - remove all build artifacts and converted songs"
	@echo ""

#---------------------------------------------------------------------------------
# Song conversion targets
#---------------------------------------------------------------------------------
songs: clean_data clean_native
	@echo "Converting songs to $(DATA_DIR)/ ..."
	@python $(MAS_SCRIPT) -i $(SONGS_DIR) -o $(DATA_DIR) > /dev/null 2>&1

songs_native: clean_native
	@echo "Converting songs to $(NATIVE_DIR)/ ..."
	@python $(MAS_SCRIPT) -i $(SONGS_DIR) -o $(NATIVE_DIR) > /dev/null 2>&1

#---------------------------------------------------------------------------------
# maxmod DS7 and DS9 can build in parallel
maxmod_ds7:
	@$(MAKE) -C $(MAXMOD) -f maxmod.mak SYSTEM=DS7

maxmod_ds9:
	@$(MAKE) -C $(MAXMOD) -f maxmod.mak SYSTEM=DS9

#---------------------------------------------------------------------------------
# arm7 needs libmm7; arm9 needs libmm9
build_arm7: maxmod_ds7
	@$(MAKE) -C $(SRCDIR)/arm7

build_arm9: maxmod_ds9
	@$(MAKE) -C $(SRCDIR)/arm9

#---------------------------------------------------------------------------------
# Emulator build: songs embedded in NitroFS inside the .nds
emulator: songs build_arm7 build_arm9
	ndstool	-c release/$(TARGET).nds \
		-7 $(SRCDIR)/arm7/$(TARGET).elf \
		-9 $(SRCDIR)/arm9/$(TARGET).elf \
		-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1);$(GAME_SUBTITLE2)" \
		$(_ADDFILES)

#---------------------------------------------------------------------------------
# Native build: .nds with empty data/, songs go to release/data/ for flashcart SD
native: clean_data songs_native build_arm7 build_arm9
	@mkdir -p $(DATA_DIR)
	ndstool	-c release/$(TARGET).nds \
		-7 $(SRCDIR)/arm7/$(TARGET).elf \
		-9 $(SRCDIR)/arm9/$(TARGET).elf \
		-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1);$(GAME_SUBTITLE2)" \
		$(_ADDFILES)

#---------------------------------------------------------------------------------
# Cleanup
clean_data:
	@rm -rf $(DATA_DIR)/

clean_native:
	@rm -rf $(NATIVE_DIR)/

clean: clean_data clean_native
	$(MAKE) -C $(MAXMOD) -f maxmod.mak SYSTEM=DS7 clean
	$(MAKE) -C $(MAXMOD) -f maxmod.mak SYSTEM=DS9 clean
	$(MAKE) -C $(SRCDIR)/arm9 clean
	$(MAKE) -C $(SRCDIR)/arm7 clean
	rm -rf $(MAXMOD)/build
	rm -f release/$(TARGET).nds $(TARGET).arm7 $(TARGET).arm9
