#
# GAMES app module for DreamShell
# Copyright (C) 2024 Maniac Vera
# http://www.dc-swat.ru
#

APP_NAME = games_menu
TARGET_NAME = app_$(APP_NAME)
OBJS = module.o utils.o menu.o system_menu.o preset.o
DBG_LIBS = -lds -lisofs -lisoldr -lwave
EXPORTS_FILE = exports.txt

KOS_CFLAGS += -I./

VER_MAJOR = 1
VER_MINOR = 0
VER_MICRO = 1

all: rm-elf

include ../../../sdk/Makefile.loadable

rm-elf:
	-rm -f $(TARGET)

install: $(TARGET)
	-rm $(DS_BUILD)/apps/$(APP_NAME)/modules/$(TARGET)
	cp $(TARGET) $(DS_BUILD)/apps/$(APP_NAME)/modules/$(TARGET)


module.o: next_library.h next_layout.h app_menu.h
menu.o: next_layout.h app_menu.h
system_menu.o preset.o: app_menu.h
