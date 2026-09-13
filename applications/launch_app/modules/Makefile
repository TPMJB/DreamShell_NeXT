#
# Launch App module for DreamShell
# Copyright (C) 2026 SWAT
#

APP_NAME = launch_app
TARGET_NAME = app_$(APP_NAME)
OBJS = module.o items.o scene.o manage.o
DBG_LIBS = -lds
EXPORTS_FILE = exports.txt

VER_MAJOR = 2
VER_MINOR = 0
VER_MICRO = 2

KOS_CFLAGS += -I./

all: rm-elf

include ../../../sdk/Makefile.loadable

rm-elf:
	-rm -f $(TARGET)

install: $(TARGET)
	-rm $(DS_BUILD)/apps/$(APP_NAME)/modules/$(TARGET)
	cp $(TARGET) $(DS_BUILD)/apps/$(APP_NAME)/modules/$(TARGET)
