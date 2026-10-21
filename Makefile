# Context Menu Editor - build with MinGW-w64 gcc
#
#   make build              release build -> build/ContextMenuEditor.exe
#   make build DEBUG=1      unoptimised build with debug symbols
#   make clean              remove build/
#   make build CROSS=x86_64-w64-mingw32-    cross-compile from Linux/macOS
#
# On Windows with a MinGW-w64 toolchain use `make` or `mingw32-make`.

NAME    := ContextMenuEditor
BUILD   := build
SRC_DIR := src
RES_DIR := res

CROSS   ?=
CC      := $(CROSS)gcc
WINDRES := $(CROSS)windres

CFLAGS  += -Wall -Wextra -municode -MMD -MP -I$(SRC_DIR) -I$(RES_DIR)
LDFLAGS += -mwindows -municode -static
LDLIBS  := -lcomctl32 -lcomdlg32 -ladvapi32 -lshell32 -lshlwapi -luxtheme -lgdi32 -luser32

ifeq ($(DEBUG),1)
CFLAGS  += -O0 -g
else
CFLAGS  += -O2
LDFLAGS += -s
endif

SRCS   := $(wildcard $(SRC_DIR)/*.c)
OBJS   := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRCS)) $(BUILD)/app_rc.o
DEPS   := $(OBJS:.o=.d)
TARGET := $(BUILD)/$(NAME).exe

# Pick a directory-removal command that works from cmd.exe and from sh.
ifeq ($(shell echo %OS%),Windows_NT)
RMBUILD = if exist $(BUILD) rmdir /s /q $(BUILD)
else
RMBUILD = rm -rf $(BUILD)
endif

# The output directory shares its name with the `build` target, so create it here
# (a rule for it would be circular). `mkdir` without flags works in cmd.exe and sh.
ifneq ($(MAKECMDGOALS),clean)
ifeq ($(wildcard $(BUILD)),)
$(shell mkdir $(BUILD))
endif
endif

.DEFAULT_GOAL := build
.PHONY: build clean

build: $(TARGET)
	@echo Built $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/app_rc.o: $(RES_DIR)/app.rc $(RES_DIR)/app.manifest $(RES_DIR)/app.ico $(RES_DIR)/resource.h
	$(WINDRES) -I$(RES_DIR) -O coff -i $< -o $@

clean:
	$(RMBUILD)

-include $(DEPS)
