#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/select.h>
#include <bits/types.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/damage.h>

int main() {
	xcb_connection_t *connection = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(connection)) {
		fprintf(stderr, "Error: Cannot open display.\n");
		return -1;
	}

	const xcb_setup_t *setup = xcb_get_setup(connection);
	xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(setup);
	xcb_screen_t *screen = screen_iter.data;

	const xcb_query_extension_reply_t *xcb_damage_extension_reply = xcb_get_extension_data(connection, &xcb_damage_id);
	if (xcb_damage_extension_reply) {
		uint8_t damage_event_id = xcb_damage_extension_reply->first_event;
		xcb_damage_query_version_cookie_t cookie = xcb_damage_query_version(connection, XCB_DAMAGE_MAJOR_VERSION, XCB_DAMAGE_MINOR_VERSION);
		xcb_generic_error_t* e = 0;
		xcb_damage_query_version_reply_t *reply = xcb_damage_query_version_reply(connection, cookie, &e);
		printf("XCB Damage extension version: %u.%u\n", reply->major_version, reply->minor_version);

		if (!reply) {
			fprintf(stderr, "Error: xcb_damage_query_version_reply_t return NULL. error code: %u\n", e->error_code);
			exit(EXIT_FAILURE);
		}

		free(reply);
		uint32_t handle = xcb_generate_id(connection);
		xcb_damage_create(connection, handle, screen->root, XCB_DAMAGE_REPORT_LEVEL_RAW_RECTANGLES);
		xcb_flush(connection);

		while (true) {
			int xfd = xcb_get_file_descriptor(connection);
			if (xfd <= 0) {
				fprintf(stderr, "invalid xcb file descriptor\n");
				exit(EXIT_FAILURE);
			}

			struct timeval tv = {
				.tv_sec = 0,
				.tv_usec = 200
			};

			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(xfd, &fds);

			int result = select(xfd + 1, &fds, NULL, NULL, &tv);
			if (result == 0) {
				continue;
			} else if (result < 0) {
				fprintf(stderr, "select failed with error: %s\n", strerror(result));
				exit(EXIT_FAILURE);
			}

			xcb_generic_event_t *event = NULL;
			while ((event = xcb_poll_for_event(connection))) {
			fprintf(stderr, "xcb damage event==========\n");
				if ((event->response_type & ~0x80) == (damage_event_id + XCB_DAMAGE_NOTIFY)) {
					xcb_damage_notify_event_t *damage_event =  (xcb_damage_notify_event_t *)event;
					fprintf(stdout, "xcb damage area: [%" PRIx16 ", %" PRIx16 ", %" PRIx16 ", %" PRIx16 "]\n",
						damage_event->area.x, damage_event->area.y, damage_event->area.width, damage_event->area.height);
					fprintf(stdout, "xcb damage geometry: [%" PRIx16 ", %" PRIx16 ", %" PRIx16 ", %" PRIx16 "]\n",
						damage_event->geometry.x, damage_event->geometry.y, damage_event->geometry.width, damage_event->geometry.height);
					free(event);
				}
			}
		}
	}
}
