#
# Launch App for DreamShell
# Copyright (C) 2026 SWAT
#

SUBDIRS = modules

include ../../sdk/Makefile.cfg

APP_NAME = launch_app
APP_DIR = $(DS_BUILD)/apps/$(APP_NAME)
DEPS = modules/app_$(APP_NAME).klf

all: install

$(DEPS): $(wildcard modules/*.c modules/*.h)
	cd modules && make

clean:
	cd modules && make clean && cd ../

MUSIC_TRACKS = music/menu.wav music/neon-circuit.wav music/orbital-drift.wav music/midnight-vector.wav music/chrome-horizon.wav

$(MUSIC_TRACKS) &: ../../utils/generate_menu_music.py
	$(PYTHON) ../../utils/generate_menu_music.py --directory music

install: app.xml catalog.xml $(MUSIC_TRACKS) $(DEPS)
	-mkdir -p $(APP_DIR)
	-mkdir -p $(APP_DIR)/modules
	-mkdir -p $(APP_DIR)/scripts
	-mkdir -p $(APP_DIR)/images
	-mkdir -p $(APP_DIR)/music
	cp modules/app_$(APP_NAME).klf $(APP_DIR)/modules/app_$(APP_NAME).klf
	cp app.xml $(APP_DIR)/app.xml
	cp catalog.xml $(APP_DIR)/catalog.xml
	cp images/*.png $(APP_DIR)/images
	cp $(MUSIC_TRACKS) music/README.md $(APP_DIR)/music/
