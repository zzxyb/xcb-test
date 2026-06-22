#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <xcb/randr.h>
#include <xcb/xcb.h>

int main(void) {
	int screen_number = 0;
	xcb_connection_t *connection = xcb_connect(NULL, &screen_number);
	if (xcb_connection_has_error(connection)) {
		fprintf(stderr, "Error: Cannot open X display.\n");
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	const xcb_query_extension_reply_t *randr =
		xcb_get_extension_data(connection, &xcb_randr_id);
	if (randr == NULL || randr->present == 0) {
		fprintf(stderr, "Error: The RandR extension is unavailable.\n");
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	const xcb_setup_t *setup = xcb_get_setup(connection);
	xcb_screen_iterator_t screen_iterator = xcb_setup_roots_iterator(setup);
	for (int i = 0; i < screen_number && screen_iterator.rem != 0; ++i) {
		xcb_screen_next(&screen_iterator);
	}

	if (screen_iterator.rem == 0) {
		fprintf(stderr, "Error: Cannot find X screen %d.\n", screen_number);
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	xcb_screen_t *screen = screen_iterator.data;
	xcb_generic_error_t *error = NULL;
	xcb_randr_get_output_primary_reply_t *primary_reply =
		xcb_randr_get_output_primary_reply(
			connection,
			xcb_randr_get_output_primary(connection, screen->root),
			&error
		);
	if (primary_reply == NULL) {
		fprintf(stderr, "Error: Failed to query the primary output%s.\n",
			error == NULL ? "" : " (X11 protocol error)");
		free(error);
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	xcb_randr_output_t output = primary_reply->output;
	free(primary_reply);
	if (output == XCB_NONE) {
		fprintf(stderr, "Error: No RandR primary output is configured.\n");
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	error = NULL;
	xcb_randr_get_output_info_reply_t *output_reply =
		xcb_randr_get_output_info_reply(
			connection,
			xcb_randr_get_output_info(connection, output, XCB_CURRENT_TIME),
			&error
		);
	if (output_reply == NULL) {
		fprintf(stderr, "Error: Failed to query primary output information%s.\n",
			error == NULL ? "" : " (X11 protocol error)");
		free(error);
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	if (output_reply->connection != XCB_RANDR_CONNECTION_CONNECTED ||
		output_reply->crtc == XCB_NONE) {
		fprintf(stderr, "Error: The primary output is not active.\n");
		free(output_reply);
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	error = NULL;
	xcb_randr_get_crtc_info_reply_t *crtc_reply =
		xcb_randr_get_crtc_info_reply(
			connection,
			xcb_randr_get_crtc_info(
				connection,
				output_reply->crtc,
				output_reply->timestamp
			),
			&error
		);
	if (crtc_reply == NULL) {
		fprintf(stderr, "Error: Failed to query primary CRTC information%s.\n",
			error == NULL ? "" : " (X11 protocol error)");
		free(error);
		free(output_reply);
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	const char *name = (const char *)xcb_randr_get_output_info_name(output_reply);
	printf("X11 primary screen:\n");
	printf("  output: %.*s (ID: 0x%08" PRIx32 ")\n",
		(int)output_reply->name_len, name, output);
	printf("  geometry: %" PRIu16 "x%" PRIu16 "%+" PRId16 "%+" PRId16 "\n",
		crtc_reply->width, crtc_reply->height, crtc_reply->x, crtc_reply->y);
	printf("  physical size: %" PRIu32 "x%" PRIu32 " mm\n",
		output_reply->mm_width, output_reply->mm_height);

	free(crtc_reply);
	free(output_reply);
	xcb_disconnect(connection);
	return EXIT_SUCCESS;
}
