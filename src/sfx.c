/* DreamShell ##version##

   sfx.c
   DreamShell sound FX
   Copyright (C) 2024-2025 SWAT
   Copyright (C) 2025 megavolt85
*/

#include <stdlib.h>
#include <string.h>

#include <kos/thread.h>
#include <dc/sound/stream.h>
#include <dc/sound/sfxmgr.h>
#include <zlib/zlib.h>

#include <sfx.h>
#include <utils.h>
#include <settings.h>

/* Filename of raw ADPCM file in DS/sfx/ directory 
   or filename of raw ADPCM packed to gzipped file (.gz extension) in /rd directory
*/
static char *stream_sfx_name[DS_SFX_LAST_STREAM] = {
	"startup.raw.gz"
};

/* Filename of wav file (without extension) in DS/sfx directory */
static char *sys_sfx_name[DS_SFX_LAST - DS_SFX_LAST_STREAM] = {
	"click",
	"click2",
	"screenshot",
	"move",
	"chpage",
	"slide",
	"error",
	"success",
	"coin"
};

static sfxhnd_t sys_sfx_hnd[DS_SFX_LAST - DS_SFX_LAST_STREAM] = {
	SFXHND_INVALID,
	SFXHND_INVALID,
	SFXHND_INVALID,
	SFXHND_INVALID,
	SFXHND_INVALID,
	SFXHND_INVALID,
	SFXHND_INVALID,
	SFXHND_INVALID,
	SFXHND_INVALID
};

typedef struct {
	uint8_t *data;
	size_t size;
	size_t position;
} sfx_stream_t;

static void *snd_stream_callback(snd_stream_hnd_t hnd, int req, int *done) {

	sfx_stream_t *stream = snd_stream_get_userdata(hnd);
	if(!stream || req <= 0 || stream->position >= stream->size) {
		*done = 0;
		return NULL;
	}
	size_t remaining = stream->size - stream->position;
	size_t count = (size_t)req < remaining ? (size_t)req : remaining;
	void *result = stream->data + stream->position;
	stream->position += count;
	*done = (int)count;
	return result;
}

static void *snd_stream_thread(void *params) {
	snd_stream_hnd_t hnd = (snd_stream_hnd_t)params;
	sfx_stream_t *stream = snd_stream_get_userdata(hnd);

	while(1) {
		if(snd_stream_poll(hnd) < 0) {
			break;
		}
		thd_sleep(50);
	}

	snd_stream_destroy(hnd);
	free(stream->data);
	free(stream);
	return NULL;
}

static void *load_raw_gz(const char *filename, size_t *sz) {
	gzFile fp;
	void *data = NULL;
	size_t size;
	
	size = gzip_get_file_size(filename);
	
	if(size == 0 || size > (2 << 20) || (size & 7)) {
		return data;
	}
	fp = gzopen(filename, "r");

	if(fp == NULL) {
		return data;
	}
	data = aligned_alloc(32, (size + 31) & ~(size_t)31);

	if(data == NULL) {
		gzclose(fp);
		return data;
	}
	if(gzread(fp, data, size) != size) {
		free(data);
		data = NULL;
	}
	gzclose(fp);
	
	*sz = size;
	
	return data;
}

static void *load_raw_adpcm(const char *filename, size_t *sz) {
	file_t fp;
	void *data = NULL;
	size_t size;
	
	size = FileSize(filename);
	
	if(size == 0 || size > (2 << 20) || (size & 7)) {
		return data;
	}
	fp = fs_open(filename, O_RDONLY);

	if(fp == FILEHND_INVALID) {
		return data;
	}
	data = aligned_alloc(32, (size + 31) & ~(size_t)31);

	if(data == NULL) {
		fs_close(fp);
		return data;
	}
	if(fs_read(fp, data, size) != size) {
		free(data);
		data = NULL;
	}
	fs_close(fp);
	
	*sz = size;
	
	return data;
}

int ds_sfx_is_enabled(ds_sfx_t sfx) {
	Settings_t *settings = GetSettings();

	if(!settings || settings->audio.volume == 0) {
		return 0;
	}

	switch(sfx) {
		case DS_SFX_STARTUP:
			return settings->audio.startup_enabled;
		case DS_SFX_CLICK:
			return settings->audio.sfx_enabled && settings->audio.click_enabled;
		case DS_SFX_CLICK2:
			return settings->audio.sfx_enabled && settings->audio.hover_enabled;
		case DS_SFX_COIN:
			return settings->audio.sfx_enabled && settings->audio.click_enabled;
		default:
			return settings->audio.sfx_enabled;
	}
}

static int ds_sfx_get_volume(void) {
	int volume = GetVolumeFromSettings();
	return volume < 0 ? 230 : volume;
}

void ds_sfx_get_wav(char sfx_path[], ds_sfx_t sfx_sel) {
	if (sfx_sel >= 0 && sfx_sel < DS_SFX_LAST) {
		snprintf(sfx_path, NAME_MAX, "%s/sfx/%s.wav", getenv("PATH"), sys_sfx_name[sfx_sel]);
	}
	else {
		sfx_path[0] = '\0';
	}
}

static int ds_sfx_play_stream(ds_sfx_t sfx) {
	char sfx_path[NAME_MAX];
	
	if(sfx >= DS_SFX_LAST_STREAM) {
		return -1;
	}
	sfx_stream_t *stream = calloc(1, sizeof(*stream));
	if(!stream) {
		return -1;
	}

	int pos = strlen(stream_sfx_name[sfx]) - 3;

	if(!strncmp(&stream_sfx_name[sfx][pos], ".gz", 3)) {
		snprintf(sfx_path, NAME_MAX, "/rd/%s", stream_sfx_name[sfx]);
		stream->data = load_raw_gz(sfx_path, &stream->size);
	}
	else {
		snprintf(sfx_path, NAME_MAX, "%s/sfx/%s", getenv("PATH"), stream_sfx_name[sfx]);
		stream->data = load_raw_adpcm(sfx_path, &stream->size);
	}

	if(!stream->data) {
		free(stream);
		return -1;
	}

	snd_stream_hnd_t snd_stream_hnd = snd_stream_alloc(snd_stream_callback, SND_STREAM_BUFFER_MAX_ADPCM);

	if(snd_stream_hnd < 0) {
		free(stream->data);
		free(stream);
		return -1;
	}

	snd_stream_set_userdata(snd_stream_hnd, stream);
	snd_stream_start_adpcm(snd_stream_hnd, 44100, 1);
	snd_stream_volume(snd_stream_hnd, ds_sfx_get_volume());

	if(!thd_create(1, snd_stream_thread, (void *)snd_stream_hnd)) {
		snd_stream_destroy(snd_stream_hnd);
		free(stream->data);
		free(stream);
		return -1;
	}
	return 0;
}

void ds_sfx_preload(void) {
	ds_sfx_t init_sfx[] = {DS_SFX_CLICK, DS_SFX_CLICK2, DS_SFX_SLIDE};
	int init_count = sizeof(init_sfx) / sizeof(init_sfx[0]);

	for(int i = 0; i < init_count; i++) {
		if(!ds_sfx_is_enabled(init_sfx[i])) {
			continue;
		}

		int sfx_sel = init_sfx[i] - DS_SFX_LAST_STREAM;

		if(sys_sfx_hnd[sfx_sel] == SFXHND_INVALID) {
			char sfx_path[NAME_MAX];
			ds_sfx_get_wav(sfx_path, sfx_sel);
			sys_sfx_hnd[sfx_sel] = snd_sfx_load(sfx_path);
		}
	}
}

int ds_sfx_play(ds_sfx_t sfx) {
	if(sfx >= DS_SFX_LAST) {
		return -1;
	}

	if(!ds_sfx_is_enabled(sfx)) {
		return 0;
	}

	if (sfx < DS_SFX_LAST_STREAM) {
		return ds_sfx_play_stream(sfx);
	}

	int sfx_sel = sfx - DS_SFX_LAST_STREAM;

	if (sys_sfx_hnd[sfx_sel] == SFXHND_INVALID) {
		char sfx_path[NAME_MAX];
		ds_sfx_get_wav(sfx_path, sfx_sel);
		sys_sfx_hnd[sfx_sel] = snd_sfx_load(sfx_path);

		if (sys_sfx_hnd[sfx_sel] == SFXHND_INVALID) {
			return -1;
		}
	}

	snd_sfx_play(sys_sfx_hnd[sfx_sel], ds_sfx_get_volume(), 128);
	return 0;
}
