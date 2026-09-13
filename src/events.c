/****************************
 * DreamShell ##version##   *
 * events.c                 *
 * DreamShell events        *
 * (c)2007-2026 SWAT        *
 * http://www.dc-swat.ru    *
 ***************************/


#include "ds.h"
#include "video.h"
#include "events.h"
#include "console.h"

static Item_list_t *events;

static void FreeEvent(void *event) {
    free(event);
}


int InitEvents() {

	if((events = listMake()) == NULL) {
		return -1;
	}

	return 0;
}

void ShutdownEvents() {
	if (events) listDestroy(events, (listFreeItemFunc *) FreeEvent);
    events = NULL;
}


Item_list_t *GetEventList() {
	return events;
}


Event_t *GetEventById(uint32 id) {

	Item_t *i = listGetItemById(events, id);

	if(i != NULL) {
		return (Event_t *) i->data;
	} else {
		return NULL;
	}
}


Event_t *GetEventByName(const char *name) {

	Item_t *i = listGetItemByName(events, name);

	if(i != NULL) {
		return (Event_t *) i->data;
	} else {
		return NULL;
	}
}


Event_t *AddEvent(const char *name, int type, int prio, Event_func *event, void *param) {

	Event_t *e;
	Item_t *i;

	if(!event || !name || GetEventByName(name)) {
		return NULL;
	}

	e = (Event_t *)calloc(1, sizeof(Event_t));
	if(e == NULL) return NULL;

	e->name = name;
	e->event = event;
	e->active = 1;
	e->type = type;
	e->param = param;
	e->prio = prio;

	if(type == EVENT_TYPE_VIDEO) {
		LockVideo();
	}

	if((i = listAddItem(events, LIST_ITEM_EVENT, e->name, e, sizeof(Event_t))) == NULL) {
		FreeEvent(e);
		e = NULL;
	} else {
		e->id = i->id;
	}

	if(type == EVENT_TYPE_VIDEO) {
		UnlockVideo();
	}
	return e;
}


int RemoveEvent(Event_t *e) {
    if (!events || !e) return -1;

	int rv = 0;
    int video = e->type == EVENT_TYPE_VIDEO;

	if(video) {
		LockVideo();
	}
	Item_t *i = listGetItemById(events, e->id);

	if(!i)
		rv = -1;
	else
		listRemoveItem(events, i, (listFreeItemFunc *) FreeEvent);

	if(video) {
		UnlockVideo();
	}
	return rv;
}


int SetEventActive(Event_t *e, int is_active) {
    if (!events || !e) return -1;
	if(e->type == EVENT_TYPE_VIDEO) {
		LockVideo();
	}

	e->active = is_active;

	if(e->type == EVENT_TYPE_VIDEO) {
		UnlockVideo();
	}
	return is_active;
}


/* Modal overlays receive input before app handlers, regardless of load order.
 * A handler consumes an event by clearing its type (SDL_NOEVENT). */
void ProcessInputEvents(SDL_Event *event) {
    if (!events || !event) return;
    for (int prio = EVENT_PRIO_OVERLAY; prio >= EVENT_PRIO_DEFAULT; --prio) {
        Item_t *c = listGetItemFirst(events);
        while (c != NULL && event->type != SDL_NOEVENT) {
            Item_t *next = listGetItemNext(c);
            Event_t *e = (Event_t *)c->data;
            if (e->type == EVENT_TYPE_INPUT && e->active && e->prio == prio)
                e->event(e, event, EVENT_ACTION_UPDATE);
            c = next;
        }
    }
}

static void process_video_events(int action, int prio) {
    if (!events) return;
	Event_t *e;
	Item_t *c, *n;

	c = listGetItemFirst(events);

	while(c != NULL) {
		n = listGetItemNext(c);
		e = (Event_t *)c->data;

		if(e->type == EVENT_TYPE_VIDEO && e->active && e->prio == prio) {
			e->event(e, e->param, action);
		}

		c = n;
	}
}

void ProcessVideoEventsRender() {
	process_video_events(EVENT_ACTION_RENDER, EVENT_PRIO_DEFAULT);
	process_video_events(EVENT_ACTION_RENDER, EVENT_PRIO_OVERLAY);
}

void ProcessVideoEventsRenderPost() {
	process_video_events(EVENT_ACTION_RENDER_POST, EVENT_PRIO_DEFAULT);
	process_video_events(EVENT_ACTION_RENDER_POST, EVENT_PRIO_OVERLAY);
}

void ProcessVideoEventsUpdate(VideoEventUpdate_t *area) {
	(void)area;
	LockVideo();
	process_video_events(EVENT_ACTION_UPDATE, EVENT_PRIO_DEFAULT);
	process_video_events(EVENT_ACTION_UPDATE, EVENT_PRIO_OVERLAY);
	UnlockVideo();
}
