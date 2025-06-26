#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>

#define WINDOW_WIDTH  400
#define WINDOW_HEIGHT 300

int main() {
	xcb_connection_t *conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(conn)) {
		fprintf(stderr, "Error: Cannot open display.\n");
		return -1;
	}

	const xcb_setup_t *setup = xcb_get_setup(conn);
	xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(setup);
	xcb_screen_t *screen = screen_iter.data;

	xcb_window_t window = xcb_generate_id(conn);
	uint32_t value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	uint32_t value_list[] = {
		screen->black_pixel,
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS
	};

	xcb_create_window(
		conn,
		XCB_COPY_FROM_PARENT,
		window,
		screen->root,
		0, 0,
		WINDOW_WIDTH, WINDOW_HEIGHT,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual,
		value_mask, value_list
	);

	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, strlen("_NET_WM_WINDOW_OPACITY"), "_NET_WM_WINDOW_OPACITY");
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, NULL);
	if (reply) {
		uint32_t opacity = 0.5 * 0xffffffff;
		xcb_change_property(conn,
			XCB_PROP_MODE_REPLACE,
			window,
			reply->atom,
			XCB_ATOM_CARDINAL,
			32,
			1,
			&opacity
		);
		free(reply);
	}

	xcb_map_window(conn, window);
	xcb_flush(conn);

	xcb_generic_event_t *event;
	while ((event = xcb_wait_for_event(conn))) {
		switch (event->response_type & ~0x80) {
			case XCB_EXPOSE:
				break;
			case XCB_KEY_PRESS:
				free(event);
				goto done;
		}
		free(event);
	}

done:
	xcb_disconnect(conn);
	return 0;
}
