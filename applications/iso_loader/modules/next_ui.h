/* DreamShell NeXT ISO Loader interface, TPMJB, 2026. */
static GUI_Widget *next_widget(const char *name) {
    return APP_GET_WIDGET(name);
}

static void next_status(const char *text) {
    if(self.status) GUI_LabelSetText(self.status, text ? text : "");
}

static void next_refresh(void) {
    char text[96];
    if(!self.summary) return;
    if(self.loading == 1) {
        GUI_LabelSetText(self.summary, "Reading game information...");
        return;
    }
    if(!self.isoldr || !self.isoldr->exec.size) {
        GUI_LabelSetText(self.summary, "Select a game to inspect it");
        return;
    }
    const char *profile = self.profile_mode ? "Baseline (unsaved)" :
        !strncmp(self.preset_source, "/presets_", 9) ? "Bundled game preset" :
        !strncmp(self.preset_source, "Automatic", 9) ? "Automatic defaults" : "Saved game preset";
    snprintf(text, sizeof(text), "%s | %s", self.isoldr->fs_dev, profile);
    GUI_LabelSetText(self.summary, text);
    next_status("Stick: cursor   D-pad: focus   A: select   B: back   Start: play");
}

void isoLoader_Dismiss(GUI_Widget *widget) {
    (void)widget;
    GUI_ScreenSetModalWidget(GUI_GetScreen(), NULL);
    GUI_WidgetSetFlags(self.message, WIDGET_HIDDEN);
    GUI_WidgetClearFlags(self.pages, WIDGET_HIDDEN);
    GUI_WidgetMarkChanged(self.app->body);
}

static void next_message(const char *text) {
    const char *p = text ? text : "No details available.";
    for(int i = 0; i < 6; ++i) {
        char name[24], line[76];
        size_t len = strlen(p), n = len < 70 ? len : 70;
        const char *nl = strchr(p, '\n');
        if(nl && (size_t)(nl - p) < n) n = nl - p;
        else if(len > n) {
            size_t space = n;
            while(space && p[space] != ' ') --space;
            if(space) n = space;
        }
        /* Do not cut a UTF-8 continuation sequence. */
        while(n && ((unsigned char)p[n] & 0xc0) == 0x80) --n;
        memcpy(line, p, n); line[n] = '\0';
        p += n;
        while(*p == ' ' || *p == '\n' || *p == '\r') ++p;
        snprintf(name, sizeof(name), "message-%d", i);
        GUI_LabelSetText(next_widget(name), line);
    }
    /* Pause drawing the page behind the modal so asynchronous cover/list
     * updates cannot paint over the error text. */
    GUI_WidgetSetFlags(self.pages, WIDGET_HIDDEN);
    GUI_WidgetClearFlags(self.filebrowser, WIDGET_PRESSED);
    GUI_ScreenSetJoySelectState(GUI_GetScreen(), 1);
    SDL_DC_EmulateMouse(SDL_TRUE);
    GUI_WidgetClearFlags(self.message, WIDGET_HIDDEN);
    GUI_WidgetMarkChanged(self.app->body);
    GUI_WidgetMarkChanged(self.message);
    GUI_ScreenSetModalWidget(GUI_GetScreen(), self.message);
}

static void next_report(const char *path, const isoldr_info_t *info, uint32 addr, const char *stage) {
    char target[NAME_MAX], temp[NAME_MAX], previous[NAME_MAX];
    int n = snprintf(self.launch_report, sizeof(self.launch_report),
        "DreamShell NeXT ISO Loader 2.0.1 / TPMJB\n"
        "Stage: %s\nImage: %s\nProfile: %s\nPreset: %s\n"
        "Loader address: %08lx\n",
        stage, path, self.profile_mode ? "Baseline (unsaved)" : "Game settings",
        self.preset_source, addr);
    if(info && n > 0 && n < (int)sizeof(self.launch_report)) {
        snprintf(self.launch_report + n, sizeof(self.launch_report) - n,
            "Device: %s, partition: %lu\nData track: %s\n"
            "Image type: %lu, sector bytes: %lu\n"
            "Executable: %s, bytes: %lu, FAD: %lu, address: %08lx\n"
            "Boot mode: %lu, binary type: %lu\n"
            "DMA: %lu, async: %lu, IRQ: %lu, CDDA: %08lx, VMU: %lu\n"
            "Heap: %08lx, fast boot: %lu, syscalls: %lu\n"
            "Executable CRC enabled: %s, expected CRC: %08lx\n"
            "This report records preparation. It does not prove the game started.\n",
            info->fs_dev, info->fs_part, info->image_file, info->image_type, info->sector_size,
            info->exec.file, info->exec.size, info->exec.lba, info->exec.addr,
            info->boot_mode, info->exec.type, info->use_dma, info->emu_async,
            info->use_irq, info->emu_cdda, info->emu_vmu, info->heap,
            info->fast_boot, info->syscalls,
            info->magic[10] == ISOLDR_VERIFY_MARKER ? "yes" : "no", info->boot_crc32);
    }
    if(snprintf(target, sizeof(target), "%s/apps/iso_loader/last-launch.txt", getenv("PATH")) >= sizeof(target) ||
       snprintf(temp, sizeof(temp), "%s/apps/iso_loader/last-launch.tmp", getenv("PATH")) >= sizeof(temp) ||
       snprintf(previous, sizeof(previous), "%s/apps/iso_loader/last-launch.previous.txt", getenv("PATH")) >= sizeof(previous))
        return;
    file_t fd = fs_open(temp, O_WRONLY | O_CREAT | O_TRUNC);
    if(fd == FILEHND_INVALID) {
        ds_printf("DS_WARNING: Launch report could not be saved; available in Details this session.\n");
        return;
    }
    size_t len = strlen(self.launch_report);
    int ok = fs_write(fd, self.launch_report, len) == (ssize_t)len;
    if(fs_close(fd) < 0) ok = 0;
    if(ok) {
        /* FatFs does not replace an existing rename destination. Rotate only
         * after the new report was completely written and closed. */
        int moved = 0;
        if(FileExists(target)) {
            if(!FileExists(previous) || fs_unlink(previous) == 0)
                moved = fs_rename(target, previous) == 0;
        }
        if(fs_rename(temp, target) == 0) return;
        if(moved) fs_rename(previous, target);
    }
    fs_unlink(temp);
    ds_printf("DS_WARNING: Launch report could not be saved; previous report retained.\n");
}

void isoLoader_Check(GUI_Widget *widget) {
    (void)widget;
    isoLoader_Run(self.btn_check);
}

void isoLoader_Details(GUI_Widget *widget) {
    (void)widget;
    if(!self.launch_report[0]) {
        char path[NAME_MAX];
        snprintf(path, sizeof(path), "%s/apps/iso_loader/last-launch.txt", getenv("PATH"));
        file_t fd = fs_open(path, O_RDONLY);
        if(fd != FILEHND_INVALID) {
            ssize_t n = fs_read(fd, self.launch_report, sizeof(self.launch_report)-1);
            if(n > 0) self.launch_report[n] = '\0';
            fs_close(fd);
        }
    }
    isoLoader_Dismiss(NULL);
    ds_printf("\n%s\n", self.launch_report[0] ? self.launch_report :
              "No launch report yet. Select a game and use Check or Play.");
    ShowConsole();
}

void isoLoader_Baseline(GUI_Widget *widget) {
    (void)widget;
    if(!self.loading && !self.isoldr && self.filename[0]) {
        char path[NAME_MAX];
        snprintf(path, sizeof(path), "%s/%s", GUI_FileManagerGetPath(self.filebrowser), self.filename);
        self.isoldr = isoldr_get_info(path, 0);
    }
    if(self.loading || !self.isoldr || !self.isoldr->exec.size || self.isoldr->image_type == IMAGE_TYPE_ROM_NAOMI) {
        next_status("Select a Dreamcast disc image and wait for its information."); return;
    }
    self.profile_mode = 1;
    GUI_WidgetSetEnabled(self.btn_run, 1);
    GUI_WidgetSetEnabled(self.btn_check, 1);
    GUI_WidgetSetState(self.preset, 0);
    GUI_WidgetSetState(self.dma, 0);
    GUI_WidgetSetState(self.alt_read, 0);
    GUI_WidgetSetState(self.irq, 0);
    GUI_WidgetSetState(self.low, 0);
    GUI_WidgetSetState(self.fastboot, 0);
    GUI_WidgetSetState(self.screenshot, 0);
    GUI_WidgetSetState(self.use_gpio, 0);
    GUI_WidgetSetState(self.alt_boot, 0);
    GUI_WidgetSetState(self.os_chk[0], 1);
    isoLoader_toggleOS(self.os_chk[0]);
    isoLoader_toggleAsync(self.async[8]);
    setModeCDDA(CDDA_MODE_DISABLED);
    isoLoader_toggleVMU(self.vmu_disabled);
    GUI_WidgetSetState(self.heap[0], 1);
    isoLoader_toggleHeap(self.heap[0]);
    GUI_WidgetSetState(self.boot_mode_chk[BOOT_MODE_DIRECT], 1);
    isoLoader_toggleBootMode(self.boot_mode_chk[BOOT_MODE_DIRECT]);
    GUI_TextEntrySetText(self.device, "auto");
    GUI_WidgetSetState(self.verify_boot, 1);
    for(int i = 0; self.memory_chk[i]; ++i) {
        const char *name = GUI_ObjectGetName(self.memory_chk[i]);
        if(!strcmp(name, "0x8ce00000") || strlen(name) < 8) {
            GUI_WidgetSetState(self.memory_chk[i], 1);
            isoLoader_toggleMemory(self.memory_chk[i]);
            if(strlen(name) < 8) GUI_TextEntrySetText(self.memory_text, "ce00000");
            break;
        }
    }
    for(int i = 0; i < 2; ++i) {
        self.pa[i] = self.pv[i] = 0;
        GUI_TextEntrySetText(self.wpa[i], "");
        GUI_TextEntrySetText(self.wpv[i], "");
    }
    next_refresh();
    next_status("Baseline selected: Direct, 8ce00000, no CDDA/VMU emulation. Preset kept.");
}

void isoLoader_RestoreProfile(GUI_Widget *widget) {
    (void)widget;
    if(self.loading) return;
    self.profile_mode = 0;
    isoLoader_LoadPreset(NULL);
    GUI_WidgetSetState(self.preset, 0);
    next_refresh();
}

void isoLoader_Up(GUI_Widget *widget) {
    (void)widget;
    if(self.loading) { next_status("Wait for game information before changing folders."); return; }
    const char *current = GUI_FileManagerGetPath(self.filebrowser);
    if(!strcmp(current, "/")) { isoLoader_Exit(NULL); return; }
    GUI_FileManagerChangeDir(self.filebrowser, "..", -1);
    self.filename[0] = '\0';
    self.current_item = self.current_item_dir = -1;
    if(self.isoldr) { free(self.isoldr); self.isoldr = NULL; }
    GUI_WidgetSetEnabled(self.btn_run, 0);
    GUI_WidgetSetEnabled(self.btn_check, 0);
    setTitle("Select a game");
    highliteDevice();
    next_refresh();
}

/* Keep the native pointer and focus traversal on every page. Dreamcast SDL
 * emits both joystick A/B and mouse events, so activation must use the mouse
 * events once, on release; handling joystick A as well would click twice. */
static void next_input(void *event, void *param, int action) {
    (void)event;
    SDL_Event *e = param;
    if(action != EVENT_ACTION_UPDATE || !e || !(self.app->state & APP_STATE_OPENED) ||
       ConsoleIsVisible()) return;
    if(e->type == SDL_KEYDOWN && (e->key.keysym.sym == SDLK_F1 || e->key.keysym.sym == SDLK_PRINT)) return;
    if(GUI_ScreenGetFocusWidget(GUI_GetScreen())) {
        GUI_ScreenEvent(GUI_GetScreen(), e, 0, 0);
        e->type = SDL_NOEVENT; return;
    }
    if((e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) &&
       (e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT))) return;
    int key = (e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) ? e->key.keysym.sym : SDLK_UNKNOWN;
    int mouse = e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP;
    int back = (mouse && e->button.button == SDL_BUTTON_RIGHT) ||
        key == SDLK_ESCAPE || key == SDLK_BACKSPACE;
    int accept = (mouse && e->button.button == SDL_BUTTON_LEFT) ||
        key == SDLK_RETURN || key == SDLK_KP_ENTER;
    int released = e->type == SDL_MOUSEBUTTONUP || e->type == SDL_KEYUP;

    if(!(GUI_WidgetGetFlags(self.message) & WIDGET_HIDDEN)) {
        /* Consume the complete press/release pair inside the modal. Closing on
         * joystick-down would leak its synthesized mouse click to the page. */
        if(back || accept) {
            if(released) isoLoader_Dismiss(NULL);
        } else {
            GUI_ScreenEvent(GUI_GetScreen(), e, 0, 0);
        }
        e->type = SDL_NOEVENT; return;
    }
    if(back) {
        if(released) {
            if(GUI_CardStackGetIndex(self.pages) != 0) isoLoader_ShowGames(self.games);
            else isoLoader_Up(NULL);
        }
    } else if((e->type == SDL_JOYBUTTONDOWN && e->jbutton.button == SDL_DC_START) ||
              (e->type == SDL_KEYDOWN && key == SDLK_SPACE)) {
        isoLoader_Run(NULL);
    } else {
        /* Native D-pad focus reaches the device bar, tabs, checkboxes, list
         * and actions. X/Y + D-pad retain the file manager's row/page scrolling. */
        GUI_ScreenEvent(GUI_GetScreen(), e, 0, 0);
    }
    e->type = SDL_NOEVENT;
}

void isoLoader_Open(App_t *app) {
    (void)app;
    GUI_ScreenSetJoySelectState(GUI_GetScreen(), 1);
    SDL_DC_EmulateMouse(SDL_TRUE);
    if(self.input_event) {
        GUI_DisableInput();
        SetEventActive(self.input_event, 1);
    } else {
        GUI_EnableInput();
        next_status("Use the cursor and Play button; shortcuts are unavailable.");
        return;
    }
    next_refresh();
}
void isoLoader_Close(void) {
    if(self.input_event) SetEventActive(self.input_event, 0);
    GUI_ScreenSetModalWidget(GUI_GetScreen(), NULL);
    GUI_ScreenSetJoySelectState(GUI_GetScreen(), 1);
    SDL_DC_EmulateMouse(SDL_TRUE);
    GUI_EnableInput();
}
