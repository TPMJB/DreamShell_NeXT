#
# GD Ripper App for DreamShell
# Copyright (C) 2014 megavolt85
# http://www.dc-swat.ru
#

SUBDIRS = modules

include ../../sdk/Makefile.cfg

APP_NAME = gd_ripper
APP_DIR = $(DS_BUILD)/apps/$(APP_NAME)
DEPS = modules/app_$(APP_NAME).klf

all: install

$(DEPS): modules/module.c modules/verify.c modules/verify.h modules/checksum.c modules/checksum.h
	cd modules && make

clean:
	cd modules && make clean && cd ../

install: app.xml $(DEPS) redump.db tosec.db redump-db.README
	-mkdir -p $(APP_DIR)
	-mkdir -p $(APP_DIR)/modules
	cp modules/app_$(APP_NAME).klf $(APP_DIR)/modules/app_$(APP_NAME).klf
	cp app.xml $(APP_DIR)/app.xml
	cp redump.db tosec.db redump-db.README $(APP_DIR)
	cp -R images $(APP_DIR)

