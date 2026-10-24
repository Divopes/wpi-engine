# Сборка: make            — 32-битный wpi.exe (работает на x86 и x64, XP..11)
#         make wpi64.exe  — 64-битная версия
# Linux: apt install gcc-mingw-w64-i686 gcc-mingw-w64-x86-64
# Windows: MSYS2 (pacman -S mingw-w64-i686-gcc make), затем make

CC32     ?= i686-w64-mingw32-gcc
WINDRES32 ?= i686-w64-mingw32-windres
CC64     ?= x86_64-w64-mingw32-gcc
WINDRES64 ?= x86_64-w64-mingw32-windres

SRC     = src/main.c src/config.c src/install.c src/json.c
HDR     = src/wpi.h src/json.h
CFLAGS  = -std=c99 -Os -Wall -Wextra -municode -mwindows \
          -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -D__USE_MINGW_ANSI_STDIO=0 \
          -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables
LDFLAGS = -s -static-libgcc -Wl,--gc-sections
LIBS    = -lcomctl32 -lshell32 -ladvapi32

BUILD = build

all: $(BUILD)/wpi.exe

$(BUILD)/wpi.exe: $(SRC) $(HDR) $(BUILD)/wpi32.res
	$(CC32) $(CFLAGS) $(SRC) $(BUILD)/wpi32.res -o $@ $(LDFLAGS) $(LIBS)

$(BUILD)/wpi64.exe: $(SRC) $(HDR) $(BUILD)/wpi64.res
	$(CC64) $(CFLAGS) $(SRC) $(BUILD)/wpi64.res -o $@ $(LDFLAGS) $(LIBS)

wpi64.exe: $(BUILD)/wpi64.exe

$(BUILD)/wpi32.res: src/wpi.rc src/wpi.manifest | $(BUILD)
	$(WINDRES32) -O coff src/wpi.rc $@

$(BUILD)/wpi64.res: src/wpi.rc src/wpi.manifest | $(BUILD)
	$(WINDRES64) -O coff src/wpi.rc $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all clean wpi64.exe
