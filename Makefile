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
MAXMOD_SRC := $(MAXMOD)/maxmod

# Song conversion
SONGS_DIR   := songs
DATA_DIR    := $(SRCDIR)/data
NATIVE_DIR  := release/data
MAS_SCRIPT  := $(SRCDIR)/scripts/mas.py

.PHONY: maxmod_ds7 maxmod_ds9 build_arm7 build_arm9 \
        songs emulator native clean clean_data clean_native \
        ensure_release ensure_maxmod

#---------------------------------------------------------------------------------
# Main targets
#
#   make songs    — convert tracker files to .mas in source/data/
#   make emulator — build .nds with songs embedded in NitroFS (for emulators)
#   make native   — build .nds (empty NitroFS) + songs in release/data/ (flashcarts)
#   make clean    — remove all build artifacts and converted songs
#---------------------------------------------------------------------------------
all: help

#---------------------------------------------------------------------------------
# Ensure release/ directory exists
#---------------------------------------------------------------------------------
ensure_release:
	@mkdir -p release

#---------------------------------------------------------------------------------
# Ensure maxmod lib is present (submodule or wget fallback)
#---------------------------------------------------------------------------------
ensure_maxmod:
	@if [ ! -d $(MAXMOD_SRC)/source ]; then \
		echo "maxmod not found, fetching..."; \
		if git rev-parse --is-inside-work-tree > /dev/null 2>&1; then \
			echo "Attempting git submodule update..."; \
			git submodule update --init $(MAXMOD_SRC); \
		else \
			echo "Not a git repo, downloading via wget..."; \
			mkdir -p tmp && \
			wget -q -O tmp/maxmod.zip https://github.com/merumerutho/maxmod/archive/refs/heads/master.zip && \
			unzip -qo tmp/maxmod.zip -d tmp/maxmod_tmp && \
			mkdir -p $(MAXMOD) && \
			mv tmp/maxmod_tmp/maxmod-master $(MAXMOD_SRC) && \
			rm -rf tmp; \
		fi; \
	fi

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
maxmod_ds7: ensure_maxmod
	@$(MAKE) -C $(MAXMOD) -f maxmod.mak SYSTEM=DS7

maxmod_ds9: ensure_maxmod
	@$(MAKE) -C $(MAXMOD) -f maxmod.mak SYSTEM=DS9

#---------------------------------------------------------------------------------
# arm7 needs libmm7; arm9 needs libmm9
build_arm7: maxmod_ds7
	@$(MAKE) -C $(SRCDIR)/arm7

build_arm9: maxmod_ds9
	@$(MAKE) -C $(SRCDIR)/arm9

#---------------------------------------------------------------------------------
# Emulator build: songs embedded in NitroFS inside the .nds
emulator: songs ensure_release build_arm7 build_arm9
	ndstool	-c release/$(TARGET).nds \
		-7 $(SRCDIR)/arm7/$(TARGET).elf \
		-9 $(SRCDIR)/arm9/$(TARGET).elf \
		-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1);$(GAME_SUBTITLE2)" \
		$(_ADDFILES)

#---------------------------------------------------------------------------------
# Native build: .nds with empty data/, songs go to release/data/ for flashcart SD
native: clean_data songs_native ensure_release build_arm7 build_arm9
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
	rm -rf $(MAXMOD)/build $(MAXMOD)/maxmod_build_ds7 $(MAXMOD)/maxmod_build_ds9
	rm -f release/$(TARGET).nds $(TARGET).arm7 $(TARGET).arm9
