#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>

typedef struct {
	uint32_t flags;
	uint32_t functions;
	uint32_t decorations;
	int32_t input_mode;
	uint32_t status;
} MotifWmHints;

#define MWM_HINTS_DECORATIONS (1L << 1)

int main() {
	xcb_connection_t *connection = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(connection)) {
		fprintf(stderr, "Error: Cannot open display.\n");
		return -1;
	}

	const xcb_setup_t *setup = xcb_get_setup(connection);
	xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(setup);
	xcb_screen_t *screen = screen_iter.data;

	xcb_window_t window = xcb_generate_id(connection);
	uint32_t value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	uint32_t value_list[] = {screen->black_pixel, XCB_EVENT_MASK_EXPOSURE};

	xcb_create_window(
		connection,
		XCB_COPY_FROM_PARENT,
		window,
		screen->root,
		100, 100, 400, 300,   // window geometry
		0,                    // border_width
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual,
		value_mask,
		value_list
	);

	// set _MOTIF_WM_HINTS property
	MotifWmHints hints = {0};
	/* Motif window hints */
	// #define MWM_HINTS_FUNCTIONS           (1L << 0)
	// #define MWM_HINTS_DECORATIONS         (1L << 1)
	// #define MWM_HINTS_INPUT_MODE          (1L << 2)
	// #define MWM_HINTS_STATUS              (1L << 3)
	hints.flags = MWM_HINTS_DECORATIONS;
	//bit       decorations displayed
	//---       ---------------------
	// 0        disable decorations
	// 1        all decorations
	// 2        border around the window
	// 4        resizeh, handles to resize by dragging
	// 8        title bar, showing WM_NAME
	// 16        menu, drop-down menu of the "functions" above
	// 32        minimize button, to iconify
	// 64        maximize button, to full-screen
	hints.decorations = 0; // disable decorations

	xcb_atom_t atom_motif_hints;
	{
		xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, 0, strlen("_MOTIF_WM_HINTS"), "_MOTIF_WM_HINTS");
		xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, NULL);
		if (!reply) {
			fprintf(stderr, "Error: Cannot get _MOTIF_WM_HINTS atom.\n");
			xcb_disconnect(connection);
			return -1;
		}
		atom_motif_hints = reply->atom;
		free(reply);
	}

	xcb_change_property(
		connection,
		XCB_PROP_MODE_REPLACE,
		window,
		atom_motif_hints,
		atom_motif_hints,
		32,
		sizeof(hints) / 4,
		&hints
	);

	xcb_map_window(connection, window);
	xcb_flush(connection);

	while (1) {
		xcb_generic_event_t *event = xcb_wait_for_event(connection);
		if (!event) {
			fprintf(stderr, "Error: Failed to wait for event.\n");
			break;
		}
		free(event);
	}

	xcb_disconnect(connection);
	return 0;
}
