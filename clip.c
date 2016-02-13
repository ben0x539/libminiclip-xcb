#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <xcb/xcb.h>
#include <xcb/xfixes.h>

typedef struct {
  xcb_connection_t* xcb;
  xcb_window_t w;
  xcb_atom_t clipboard;
  xcb_atom_t target;
  uint8_t xfixes_event;
} clip_t;

static int get_selection(clip_t* clip, char* out, size_t* size);
static int process_event(clip_t* clip, xcb_generic_event_t* e,
    char* out, size_t* size);
static int make_window(clip_t* clip, int screen_num);
static int register_clipboard_events(clip_t* clip,
    xcb_xfixes_query_version_cookie_t xfixes_version_cookie);


int clip_init(clip_t* clip) {
  int screen_num;
  xcb_xfixes_query_version_cookie_t xfixes_version_cookie;

  clip->xcb = xcb_connect(NULL, &screen_num);

  if (xcb_connection_has_error(clip->xcb))
    goto failure;

  xcb_prefetch_extension_data(clip->xcb, &xcb_xfixes_id);
  xfixes_version_cookie = xcb_xfixes_query_version(clip->xcb,
    XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION);

  if (make_window(clip, screen_num) == -1)
    goto failure;

  if (register_clipboard_events(clip, xfixes_version_cookie) == -1)
    goto failure;

  return 0;

failure:
  xcb_disconnect(clip->xcb);
  return -1;
}

void clip_uninit(clip_t* clip) {
  xcb_disconnect(clip->xcb);
}
int clip_poll(clip_t* clip, char* out, size_t* size) {
  xcb_generic_event_t* e;
  int done;

  done = 0;

  while (!done) {
    xcb_flush(clip->xcb);
    e = xcb_poll_for_event(clip->xcb);
    if (e == NULL)
      return 0;

    if (xcb_connection_has_error(clip->xcb))
      done = -1;
    else
      done = process_event(clip, e, out, size);

    free(e);
  }

  if (done < 0)
    return -1;

  return 0;
}

int clip_wait(clip_t* clip, char* out, size_t* size) {
  xcb_generic_event_t* e;
  int done;

  done = 0;

  while (!done) {
    xcb_flush(clip->xcb);
    e = xcb_wait_for_event(clip->xcb);
    if (e == NULL)
      return -1;

    if (xcb_connection_has_error(clip->xcb))
      done = -1;
    else
      done = process_event(clip, e, out, size);

    free(e);
  }

  if (done < 0)
    return -1;

  return 0;
}

int clip_check_error(clip_t* clip) {
  if (xcb_connection_has_error(clip->xcb))
    return -1;

  return 0;
}

int main(void) {
  clip_t clip;
  char buf[1024*1024*4];
  size_t size;

  if (clip_init(&clip)) {
    printf("init failure\n");
    return -1;
  }

  for (;;) {
    size = sizeof buf;
    if (clip_wait(&clip, buf, &size) == -1) {
      printf("failure");
      clip_uninit(&clip);
      return -1;
    }

    printf("saw: %.*s\n", (int) (size & INT_MAX), buf);
  }

  clip_uninit(&clip);

  return 0;
}

static int get_selection(clip_t* clip, char* out, size_t* size) {
  xcb_generic_error_t* err;
  xcb_get_property_cookie_t gpc;
  size_t actual_size;
  xcb_get_property_reply_t* gpr;
  xcb_get_atom_name_cookie_t ganc;

  gpc = xcb_get_property(clip->xcb, 1, clip->w, clip->clipboard,
      XCB_GET_PROPERTY_TYPE_ANY, 0, (*size + 3)/4);
  gpr = xcb_get_property_reply(clip->xcb, gpc, &err);
  if (err != NULL) {
    free(gpr);
    return -1;
  }

  actual_size = xcb_get_property_value_length(gpr);
  if (actual_size < *size)
    *size = actual_size;
  memcpy(out, xcb_get_property_value(gpr), *size);

  ganc = xcb_get_atom_name(clip->xcb, gpr->type);
  free(gpr);

  return 0;
}

static int process_event(clip_t* clip, xcb_generic_event_t* e,
    char* out, size_t* size) {
  xcb_xfixes_selection_notify_event_t* xfsne;
  xcb_selection_notify_event_t* sne;
  int owner_change;
  int resp;

  owner_change = clip->xfixes_event + XCB_XFIXES_SELECTION_NOTIFY;

  resp = e->response_type & 0x7f;
  if (resp == 0)
    return -1;

  if (resp == XCB_SELECTION_NOTIFY) {
    sne = (void*) e;
    if (sne->property != XCB_NONE) {
      if (get_selection(clip, out, size) == -1)
        return -1;
      return 1;
    }
  } else if (resp == owner_change) {
    xfsne = (void*) e;
    xcb_convert_selection(clip->xcb, clip->w, clip->clipboard, clip->target,
        clip->clipboard, xfsne->selection_timestamp);
  } else {
    /* Unknown event type, ignore it */
  }

  return 0;
}

static int make_window(clip_t* clip, int screen_num) {
  const xcb_setup_t* setup;
  int i;
  xcb_screen_iterator_t screens;
  xcb_screen_t* screen;

  setup = xcb_get_setup(clip->xcb);
  screens = xcb_setup_roots_iterator(setup);
  for (i = 0; i < screen_num; ++i)
      xcb_screen_next(&screens);
  screen = screens.data;

  clip->w = xcb_generate_id(clip->xcb);
  xcb_create_window(clip->xcb, 0, clip->w, screen->root,
      0, 0, 1, 1, 0,
      XCB_WINDOW_CLASS_INPUT_ONLY, 0,
      0, NULL /* mask, valwin */);

  return 0;
}

static int register_clipboard_events(clip_t* clip,
    xcb_xfixes_query_version_cookie_t xfixes_version_cookie) {
  xcb_intern_atom_cookie_t atom_cookie[2];
  const char* atoms[] = { "CLIPBOARD", "TEXT" };
  xcb_atom_t* atom_dests[2];
  int i;
  xcb_intern_atom_reply_t* atom_reply;
  xcb_generic_error_t* err;
  const xcb_query_extension_reply_t* xfixes;
  xcb_xfixes_query_version_reply_t* version;

  for (i = 0; i < sizeof(atom_cookie)/sizeof(atom_cookie[0]); ++i) {
    atom_cookie[i] =
      xcb_intern_atom(clip->xcb, 0, strlen(atoms[i]), atoms[i]);
  }

  atom_dests[0] = &clip->clipboard;
  atom_dests[1] = &clip->target;

  for (i = 0; i < sizeof(atom_cookie)/sizeof(atom_cookie[0]); ++i) {
    atom_reply = xcb_intern_atom_reply(clip->xcb, atom_cookie[i], &err);

    if (err) {
      free(atom_reply);
      free(err);
      return -1;
    }
    *atom_dests[i] = atom_reply->atom;
    free(atom_reply);
  }

  xfixes = xcb_get_extension_data(clip->xcb, &xcb_xfixes_id);
  if (!xfixes->present)
    return -1;
  clip->xfixes_event = xfixes->first_event;

  version =
    xcb_xfixes_query_version_reply(clip->xcb, xfixes_version_cookie, &err);

  if (err != NULL) {
    free(err);
    free(version);
    return -1;
  }
  free(version);

  xcb_xfixes_select_selection_input(clip->xcb, clip->w, clip->clipboard,
      XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER);

  return 0;
}

