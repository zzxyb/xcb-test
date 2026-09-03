#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <xcb/sync.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#define WINDOW_WIDTH 480
#define WINDOW_HEIGHT 300
#define DEFAULT_FRAMES 120
#define DEFAULT_INTERVAL_MS 16
#define STARTUP_DELAY_MS 100
#define DRAIN_DELAY_MS 1000
#define FRAME_WAIT_TIMEOUT_MS 1000
#define MAX_TRACKED_FRAMES 1024

struct atoms {
	xcb_atom_t wm_protocols;
	xcb_atom_t wm_delete_window;
	xcb_atom_t utf8_string;
	xcb_atom_t net_wm_name;
	xcb_atom_t net_supported;
	xcb_atom_t net_wm_sync_request;
	xcb_atom_t net_wm_sync_request_counter;
	xcb_atom_t net_wm_frame_drawn;
	xcb_atom_t net_wm_frame_timings;
};

struct options {
	uint64_t frames;
	uint64_t interval_ms;
	bool wait_for_drawn;
	bool resize;
};

struct frame_record {
	uint64_t serial;
	uint64_t sent_at;
	bool drawn;
	bool timings;
};

struct pending_sync_request {
	bool valid;
	bool extended;
	uint64_t value;
};

struct app {
	xcb_connection_t *connection;
	xcb_screen_t *screen;
	struct atoms atoms;
	xcb_window_t window;
	xcb_gcontext_t white_gc;
	xcb_gcontext_t black_gc;
	xcb_sync_counter_t basic_counter;
	xcb_sync_counter_t extended_counter;
	uint64_t extended_value;
	bool extended_value_valid;
	struct pending_sync_request pending_sync;

	struct frame_record records[MAX_TRACKED_FRAMES];
	size_t next_record;
	uint64_t frames_sent;
	uint64_t frames_drawn;
	uint64_t frames_timings;
	uint64_t matched_drawn;
	uint64_t matched_timings;
	uint64_t validation_errors;

	bool waiting_for_drawn;
	uint64_t waiting_serial;
	uint64_t waiting_since;
	bool stop;
};

static volatile sig_atomic_t stop_requested;

static void handle_signal(int signal_number) {
	(void)signal_number;
	stop_requested = 1;
}

static uint64_t monotonic_usec(void) {
	struct timespec time;
	if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
		return 0;
	}

	return (uint64_t)time.tv_sec * UINT64_C(1000000) +
		(uint64_t)time.tv_nsec / UINT64_C(1000);
}

static uint64_t join_u64(uint32_t low, uint32_t high) {
	return ((uint64_t)high << 32) | low;
}

static xcb_sync_int64_t sync_value(uint64_t value) {
	return (xcb_sync_int64_t){
		.hi = (int32_t)(uint32_t)(value >> 32),
		.lo = (uint32_t)value,
	};
}

static bool parse_u64(const char *text, uint64_t *value) {
	char *end = NULL;
	unsigned long long parsed;

	if (text == NULL || text[0] == '\0' || text[0] == '-') {
		return false;
	}

	errno = 0;
	parsed = strtoull(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' ||
			(uint64_t)parsed != parsed) {
		return false;
	}

	*value = (uint64_t)parsed;
	return true;
}

static void print_usage(const char *program) {
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Create an X11 window using EWMH extended frame synchronization and\n"
		"print/validate _NET_WM_FRAME_DRAWN and _NET_WM_FRAME_TIMINGS messages.\n"
		"\n"
		"Options:\n"
		"  -n, --frames N       Number of application frames (default: %u, 0 = forever)\n"
		"  -i, --interval MS    Delay between frames (default: %u)\n"
		"  -w, --wait-drawn     Send the next frame after FRAME_DRAWN (1s timeout)\n"
		"  -r, --resize         Request alternating sizes while animating\n"
		"  -h, --help           Show this help\n",
		program, DEFAULT_FRAMES, DEFAULT_INTERVAL_MS);
}

static bool intern_atom(xcb_connection_t *connection, const char *name,
		xcb_atom_t *atom) {
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(
		connection, 0, (uint16_t)strlen(name), name);
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
		connection, cookie, NULL);
	if (reply == NULL || reply->atom == XCB_ATOM_NONE) {
		fprintf(stderr, "Error: cannot intern atom %s.\n", name);
		free(reply);
		return false;
	}

	*atom = reply->atom;
	free(reply);
	return true;
}

static bool intern_atoms(struct app *app) {
	struct {
		const char *name;
		xcb_atom_t *atom;
	} names[] = {
		{"WM_PROTOCOLS", &app->atoms.wm_protocols},
		{"WM_DELETE_WINDOW", &app->atoms.wm_delete_window},
		{"UTF8_STRING", &app->atoms.utf8_string},
		{"_NET_WM_NAME", &app->atoms.net_wm_name},
		{"_NET_SUPPORTED", &app->atoms.net_supported},
		{"_NET_WM_SYNC_REQUEST", &app->atoms.net_wm_sync_request},
		{"_NET_WM_SYNC_REQUEST_COUNTER", &app->atoms.net_wm_sync_request_counter},
		{"_NET_WM_FRAME_DRAWN", &app->atoms.net_wm_frame_drawn},
		{"_NET_WM_FRAME_TIMINGS", &app->atoms.net_wm_frame_timings},
	};

	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		if (!intern_atom(app->connection, names[i].name, names[i].atom)) {
			return false;
		}
	}
	return true;
}

static bool root_supports_atom(struct app *app, xcb_atom_t wanted) {
	xcb_get_property_cookie_t cookie = xcb_get_property(
		app->connection, 0, app->screen->root, app->atoms.net_supported,
		XCB_ATOM_ATOM, 0, UINT32_MAX);
	xcb_generic_error_t *error = NULL;
	xcb_get_property_reply_t *reply = xcb_get_property_reply(
		app->connection, cookie, &error);
	if (reply == NULL || error != NULL) {
		fprintf(stderr, "Warning: cannot read _NET_SUPPORTED.\n");
		free(error);
		free(reply);
		return false;
	}

	bool supported = false;
	if (reply->type != XCB_ATOM_ATOM && reply->type != XCB_ATOM_NONE) {
		fprintf(stderr, "Warning: _NET_SUPPORTED has an unexpected type.\n");
	} else if (reply->format != 32) {
		fprintf(stderr, "Warning: _NET_SUPPORTED has an unexpected format.\n");
	} else {
		const xcb_atom_t *atoms = xcb_get_property_value(reply);
		size_t atoms_len = (size_t)xcb_get_property_value_length(reply) /
			sizeof(xcb_atom_t);
		for (size_t i = 0; i < atoms_len; i++) {
			if (atoms[i] == wanted) {
				supported = true;
				break;
			}
		}
	}

	free(reply);
	return supported;
}

static bool request_succeeded(struct app *app, xcb_void_cookie_t cookie,
		const char *request) {
	xcb_generic_error_t *error = xcb_request_check(app->connection, cookie);
	if (error == NULL) {
		return true;
	}

	fprintf(stderr, "Error: X request %s failed (error code %u).\n",
		request, error->error_code);
	free(error);
	return false;
}

static bool query_counter(struct app *app, xcb_sync_counter_t counter,
		uint64_t *value) {
	xcb_sync_query_counter_cookie_t cookie = xcb_sync_query_counter(
		app->connection, counter);
	xcb_generic_error_t *error = NULL;
	xcb_sync_query_counter_reply_t *reply = xcb_sync_query_counter_reply(
		app->connection, cookie, &error);
	if (reply == NULL || error != NULL) {
		free(error);
		free(reply);
		return false;
	}

	/* The extended counter is unsigned in the EWMH protocol. */
	if (reply->counter_value.hi < 0) {
		free(reply);
		return false;
	}

	*value = join_u64(reply->counter_value.lo,
		reply->counter_value.hi);
	free(reply);
	return true;
}

static struct frame_record *find_record(struct app *app, uint64_t serial) {
	for (size_t i = 0; i < MAX_TRACKED_FRAMES; i++) {
		struct frame_record *record = &app->records[i];
		if (record->serial == serial && record->sent_at != 0) {
			return record;
		}
	}
	return NULL;
}

static struct frame_record *add_record(struct app *app, uint64_t serial,
		uint64_t sent_at) {
	struct frame_record *record = &app->records[app->next_record];
	*record = (struct frame_record){
		.serial = serial,
		.sent_at = sent_at,
	};
	app->next_record = (app->next_record + 1) % MAX_TRACKED_FRAMES;
	return record;
}

static void report_validation_error(struct app *app, const char *message,
		uint64_t serial) {
	fprintf(stderr, "Validation error for serial %" PRIu64 ": %s\n",
		serial, message);
	app->validation_errors++;
}

static void handle_frame_drawn(struct app *app,
		const xcb_client_message_event_t *event) {
	uint64_t serial = join_u64(event->data.data32[0], event->data.data32[1]);
	uint64_t timestamp = join_u64(event->data.data32[2], event->data.data32[3]);
	uint64_t now = monotonic_usec();
	struct frame_record *record = find_record(app, serial);

	app->frames_drawn++;
	if (event->format != 32) {
		report_validation_error(app, "FRAME_DRAWN format is not 32", serial);
	}
	if (event->data.data32[4] != 0) {
		report_validation_error(app,
			"FRAME_DRAWN data[4] must be zero", serial);
	}
	if ((serial & 1) != 0) {
		report_validation_error(app,
			"FRAME_DRAWN serial is odd; expected an ended frame", serial);
	}
	if (timestamp == 0) {
		report_validation_error(app, "FRAME_DRAWN timestamp is zero", serial);
	}
	if (record == NULL) {
		fprintf(stderr, "Warning: FRAME_DRAWN for an unknown serial %" PRIu64
			" (possibly the initial map frame).\n", serial);
	} else if (record->drawn) {
		report_validation_error(app, "duplicate FRAME_DRAWN", serial);
	} else {
		record->drawn = true;
		app->matched_drawn++;
		if (app->waiting_for_drawn && serial >= app->waiting_serial) {
			app->waiting_for_drawn = false;
		}
	}

	int64_t age = 0;
	if (now != 0 && timestamp <= now) {
		age = (int64_t)(now - timestamp);
	} else if (now != 0) {
		age = -(int64_t)(timestamp - now);
	}
	fprintf(stdout, "FRAME_DRAWN serial=%" PRIu64 " timestamp=%" PRIu64
		" age_us=%" PRId64 "%s\n", serial, timestamp, age,
		record == NULL ? " [unmatched]" : "");
}

static void handle_frame_timings(struct app *app,
		const xcb_client_message_event_t *event) {
	uint64_t serial = join_u64(event->data.data32[0], event->data.data32[1]);
	int32_t presentation_offset = (int32_t)event->data.data32[2];
	uint32_t refresh_interval = event->data.data32[3];
	uint32_t frame_delay = event->data.data32[4];
	struct frame_record *record = find_record(app, serial);

	app->frames_timings++;
	if (event->format != 32) {
		report_validation_error(app, "FRAME_TIMINGS format is not 32", serial);
	}
	if (record == NULL) {
		fprintf(stderr, "Warning: FRAME_TIMINGS for an unknown serial %" PRIu64
			".\n", serial);
	} else {
		if (!record->drawn) {
			report_validation_error(app,
				"FRAME_TIMINGS arrived before FRAME_DRAWN", serial);
		}
		if (record->timings) {
			report_validation_error(app, "duplicate FRAME_TIMINGS", serial);
		} else {
			record->timings = true;
			app->matched_timings++;
		}
	}
	if ((frame_delay & UINT32_C(0x80000000)) != 0 &&
			frame_delay != UINT32_C(0x80000000)) {
		report_validation_error(app,
			"FRAME_TIMINGS frame_delay uses a reserved high-bit value", serial);
	}

	fprintf(stdout, "FRAME_TIMINGS serial=%" PRIu64
		" presentation_offset_us=%" PRId32 " refresh_interval_us=%" PRIu32
		" frame_delay_us=%" PRIu32 "%s\n", serial, presentation_offset,
		refresh_interval, frame_delay, record == NULL ? " [unmatched]" : "");
}

static void acknowledge_pending_sync(struct app *app, bool forced) {
	struct pending_sync_request *pending = &app->pending_sync;
	if (!pending->valid) {
		return;
	}
	if (forced) {
		fprintf(stderr,
			"Warning: acknowledging _NET_WM_SYNC_REQUEST without a later "
			"ConfigureNotify.\n");
	}

	if (!pending->extended) {
		xcb_sync_set_counter(app->connection, app->basic_counter,
			sync_value(pending->value));
		printf("SYNC_REQUEST basic counter -> %" PRIu64 "\n", pending->value);
	} else {
		uint64_t value = pending->value;
		if (!app->extended_value_valid || app->extended_value >= value) {
			value = app->extended_value;
		}
		if ((value & 1) == 0) {
			if (value > UINT64_MAX - 2) {
				fprintf(stderr, "Error: extended counter would overflow.\n");
				app->stop = true;
				pending->valid = false;
				return;
			}
			value += 2;
		} else {
			value++;
		}
		xcb_sync_set_counter(app->connection, app->extended_counter,
			sync_value(value));
		app->extended_value = value;
		app->extended_value_valid = true;
		printf("SYNC_REQUEST extended counter %" PRIu64 " -> %" PRIu64
			"\n", pending->value, value);
	}
	xcb_flush(app->connection);
	pending->valid = false;
}

static void handle_wm_protocols(struct app *app,
		const xcb_client_message_event_t *event) {
	xcb_atom_t protocol = event->data.data32[0];
	if (protocol == app->atoms.wm_delete_window) {
		printf("WM requested window close.\n");
		app->stop = true;
		return;
	}
	if (protocol != app->atoms.net_wm_sync_request) {
		return;
	}

	uint64_t value = join_u64(event->data.data32[2], event->data.data32[3]);
	uint32_t extended = event->data.data32[4];
	if (extended > 1) {
		report_validation_error(app,
			"_NET_WM_SYNC_REQUEST data[4] is neither 0 nor 1", value);
		extended = 0;
	}
	if (value == 0) {
		report_validation_error(app,
			"_NET_WM_SYNC_REQUEST counter value is zero", value);
	}

	app->pending_sync = (struct pending_sync_request){
		.valid = true,
		.extended = extended == 1,
		.value = value,
	};
	fprintf(stdout, "SYNC_REQUEST %s value=%" PRIu64 "\n",
		extended == 1 ? "extended" : "basic", value);
}

static void handle_event(struct app *app, xcb_generic_event_t *generic_event) {
	uint8_t response_type = generic_event->response_type & UINT8_C(0x7f);
	switch (response_type) {
	case 0: {
		xcb_generic_error_t *error = (xcb_generic_error_t *)generic_event;
		fprintf(stderr, "X server error: code %u, major opcode %u.\n",
			error->error_code, error->major_code);
		app->validation_errors++;
		break;
	}
	case XCB_CLIENT_MESSAGE: {
		xcb_client_message_event_t *event =
			(xcb_client_message_event_t *)generic_event;
		if (event->window != app->window) {
			break;
		}
		if (event->type == app->atoms.net_wm_frame_drawn) {
			handle_frame_drawn(app, event);
		} else if (event->type == app->atoms.net_wm_frame_timings) {
			handle_frame_timings(app, event);
		} else if (event->type == app->atoms.wm_protocols) {
			handle_wm_protocols(app, event);
		}
		break;
	}
	case XCB_CONFIGURE_NOTIFY:
		acknowledge_pending_sync(app, false);
		break;
	case XCB_MAP_NOTIFY:
		if (app->extended_value_valid) {
			uint64_t value = 0;
			if (query_counter(app, app->extended_counter, &value)) {
				app->extended_value = value;
				printf("Extended counter after map: %" PRIu64 "%s\n", value,
					(value & 1) != 0 ? " (odd/frozen)" : "");
			} else {
				fprintf(stderr,
					"Warning: cannot query the extended counter after map.\n");
			}
		}
		break;
	case XCB_DESTROY_NOTIFY:
		app->stop = true;
		break;
	default:
		break;
	}
}

static bool send_frame(struct app *app, const struct options *options) {
	static const xcb_sync_int64_t one = {.hi = 0, .lo = 1};
	static const xcb_sync_int64_t three = {.hi = 0, .lo = 3};
	uint64_t sent_at = monotonic_usec();
	if (!app->extended_value_valid || app->extended_value > UINT64_MAX - 4) {
		fprintf(stderr, "Error: extended counter is unavailable or would overflow.\n");
		return false;
	}

	uint64_t start = app->extended_value + 1;
	uint64_t end = app->extended_value + 4;
	if ((app->extended_value & 1) != 0) {
		fprintf(stderr,
			"Warning: extended counter was odd; completing it before a new "
			"frame.\n");
		xcb_sync_change_counter(app->connection, app->extended_counter, one);
		app->extended_value++;
		start = app->extended_value + 1;
		end = app->extended_value + 4;
	}

	xcb_sync_change_counter(app->connection, app->extended_counter, one);
	xcb_gcontext_t gc = (app->frames_sent & 1) == 0 ?
		app->white_gc : app->black_gc;
	xcb_rectangle_t rectangle = {
		.x = 0,
		.y = 0,
		.width = WINDOW_WIDTH,
		.height = WINDOW_HEIGHT,
	};
	xcb_poly_fill_rectangle(app->connection, app->window, gc, 1,
		&rectangle);
	xcb_sync_change_counter(app->connection, app->extended_counter, three);
	app->extended_value = end;
	app->frames_sent++;
	add_record(app, end, sent_at);

	if (options->resize && app->frames_sent % 30 == 0) {
		uint32_t dimensions[2] = {
			app->frames_sent % 60 == 0 ? WINDOW_WIDTH : WINDOW_WIDTH + 80,
			app->frames_sent % 60 == 0 ? WINDOW_HEIGHT : WINDOW_HEIGHT + 50,
		};
		xcb_configure_window(app->connection, app->window,
			XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, dimensions);
		printf("Configure request size=%" PRIu32 "x%" PRIu32 "\n",
			dimensions[0], dimensions[1]);
	}

	if (!xcb_flush(app->connection)) {
		fprintf(stderr, "Error: failed to flush frame requests.\n");
		return false;
	}
	printf("FRAME_SENT serial=%" PRIu64 " (counter %" PRIu64 " -> %" PRIu64
		")\n", end, start, end);
	return true;
}

static int poll_timeout_ms(uint64_t now, uint64_t deadline) {
	if (deadline <= now) {
		return 0;
	}

	uint64_t delta_us = deadline - now;
	uint64_t delta_ms = (delta_us + UINT64_C(999)) / UINT64_C(1000);
	if (delta_ms > INT_MAX) {
		return INT_MAX;
	}
	return (int)delta_ms;
}

static bool create_window(struct app *app) {
	uint32_t window_values[] = {
		app->screen->black_pixel,
		XCB_EVENT_MASK_EXPOSURE |
			XCB_EVENT_MASK_STRUCTURE_NOTIFY |
			XCB_EVENT_MASK_PROPERTY_CHANGE |
			XCB_EVENT_MASK_KEY_PRESS,
	};
	app->window = xcb_generate_id(app->connection);
	xcb_create_window(app->connection, XCB_COPY_FROM_PARENT, app->window,
		app->screen->root, 100, 100, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, app->screen->root_visual,
		XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, window_values);

	const char wm_name[] = "xcb-frame-sync\0XCB Frame Sync\0";
	xcb_change_property(app->connection, XCB_PROP_MODE_REPLACE, app->window,
		XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof(wm_name) - 1, wm_name);
	const char title[] = "XCB extended frame sync test";
	xcb_change_property(app->connection, XCB_PROP_MODE_REPLACE, app->window,
		XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, sizeof(title) - 1, title);
	xcb_change_property(app->connection, XCB_PROP_MODE_REPLACE, app->window,
		app->atoms.net_wm_name, app->atoms.utf8_string, 8,
		(uint32_t)strlen(title), title);

	xcb_atom_t protocols[] = {
		app->atoms.wm_delete_window,
		app->atoms.net_wm_sync_request,
	};
	xcb_change_property(app->connection, XCB_PROP_MODE_REPLACE, app->window,
		app->atoms.wm_protocols, XCB_ATOM_ATOM, 32,
		sizeof(protocols) / sizeof(protocols[0]), protocols);

	xcb_sync_counter_t counters[] = {
		app->basic_counter,
		app->extended_counter,
	};
	xcb_change_property(app->connection, XCB_PROP_MODE_REPLACE, app->window,
		app->atoms.net_wm_sync_request_counter, XCB_ATOM_CARDINAL, 32,
		sizeof(counters) / sizeof(counters[0]), counters);

	uint32_t white = app->screen->white_pixel;
	uint32_t black = app->screen->black_pixel;
	app->white_gc = xcb_generate_id(app->connection);
	app->black_gc = xcb_generate_id(app->connection);
	if (!request_succeeded(app, xcb_create_gc_checked(app->connection,
			app->white_gc, app->window, XCB_GC_FOREGROUND, &white),
			"create white GC") ||
		!request_succeeded(app, xcb_create_gc_checked(app->connection,
			app->black_gc, app->window, XCB_GC_FOREGROUND, &black),
			"create black GC")) {
		return false;
	}

	app->extended_value = 0;
	app->extended_value_valid = true;
	xcb_map_window(app->connection, app->window);
	if (!xcb_flush(app->connection)) {
		fprintf(stderr, "Error: failed to map/flush the test window.\n");
		return false;
	}
	return true;
}

static void destroy_resources(struct app *app) {
	if (app->connection == NULL) {
		return;
	}
	if (app->window != XCB_WINDOW_NONE) {
		xcb_destroy_window(app->connection, app->window);
	}
	if (app->white_gc != XCB_NONE) {
		xcb_free_gc(app->connection, app->white_gc);
	}
	if (app->black_gc != XCB_NONE) {
		xcb_free_gc(app->connection, app->black_gc);
	}
	if (app->basic_counter != XCB_NONE) {
		xcb_sync_destroy_counter(app->connection, app->basic_counter);
	}
	if (app->extended_counter != XCB_NONE) {
		xcb_sync_destroy_counter(app->connection, app->extended_counter);
	}
	xcb_flush(app->connection);
	xcb_disconnect(app->connection);
	app->connection = NULL;
}

static bool initialize_app(struct app *app) {
	int screen_number = 0;
	app->connection = xcb_connect(NULL, &screen_number);
	if (app->connection == NULL || xcb_connection_has_error(app->connection)) {
		fprintf(stderr, "Error: cannot open the X display.\n");
		return false;
	}

	const xcb_query_extension_reply_t *sync_extension =
		xcb_get_extension_data(app->connection, &xcb_sync_id);
	if (sync_extension == NULL || !sync_extension->present) {
		fprintf(stderr, "Error: the XSync extension is unavailable.\n");
		return false;
	}
	xcb_sync_initialize_cookie_t sync_cookie = xcb_sync_initialize(
		app->connection, XCB_SYNC_MAJOR_VERSION, XCB_SYNC_MINOR_VERSION);
	xcb_generic_error_t *sync_error = NULL;
	xcb_sync_initialize_reply_t *sync_reply = xcb_sync_initialize_reply(
		app->connection, sync_cookie, &sync_error);
	if (sync_reply == NULL || sync_error != NULL) {
		fprintf(stderr, "Error: cannot initialize XSync.\n");
		free(sync_error);
		free(sync_reply);
		return false;
	}
	fprintf(stdout, "XSync version: %u.%u\n", sync_reply->major_version,
		sync_reply->minor_version);
	free(sync_reply);

	const xcb_setup_t *setup = xcb_get_setup(app->connection);
	xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
	for (int i = 0; i < screen_number && iterator.rem > 0; i++) {
		xcb_screen_next(&iterator);
	}
	app->screen = iterator.data;
	if (app->screen == NULL || !intern_atoms(app)) {
		return false;
	}

	bool frame_drawn_supported = root_supports_atom(
		app, app->atoms.net_wm_frame_drawn);
	bool frame_timings_supported = root_supports_atom(
		app, app->atoms.net_wm_frame_timings);
	fprintf(stdout, "_NET_SUPPORTED: _NET_WM_FRAME_DRAWN=%s, "
		"_NET_WM_FRAME_TIMINGS=%s\n", frame_drawn_supported ? "yes" : "no",
		frame_timings_supported ? "yes" : "no");
	if (!frame_drawn_supported || !frame_timings_supported) {
		fprintf(stderr,
			"Warning: the WM does not advertise both extended frame timing "
			"messages.\n");
	}

	app->basic_counter = xcb_generate_id(app->connection);
	app->extended_counter = xcb_generate_id(app->connection);
	xcb_sync_int64_t zero = {.hi = 0, .lo = 0};
	if (!request_succeeded(app, xcb_sync_create_counter_checked(app->connection,
			app->basic_counter, zero), "create basic counter") ||
		!request_succeeded(app, xcb_sync_create_counter_checked(app->connection,
			app->extended_counter, zero), "create extended counter")) {
		return false;
	}
	fprintf(stdout, "Counters: basic=0x%" PRIx32 " extended=0x%" PRIx32
		" (extended initial value=0)\n", app->basic_counter,
		app->extended_counter);

	return create_window(app);
}

static bool parse_options(int argc, char **argv, struct options *options,
		bool *show_help) {
	*options = (struct options){
		.frames = DEFAULT_FRAMES,
		.interval_ms = DEFAULT_INTERVAL_MS,
	};
	*show_help = false;

	static const struct option long_options[] = {
		{"frames", required_argument, NULL, 'n'},
		{"interval", required_argument, NULL, 'i'},
		{"wait-drawn", no_argument, NULL, 'w'},
		{"resize", no_argument, NULL, 'r'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0},
	};

	int option;
	while ((option = getopt_long(argc, argv, "n:i:wrh", long_options, NULL)) !=
			-1) {
		uint64_t value = 0;
		switch (option) {
		case 'n':
			if (!parse_u64(optarg, &value)) {
				fprintf(stderr, "Error: invalid frame count: %s\n", optarg);
				return false;
			}
			options->frames = value;
			break;
		case 'i':
			if (!parse_u64(optarg, &value)) {
				fprintf(stderr, "Error: invalid interval: %s\n", optarg);
				return false;
			}
			options->interval_ms = value;
			break;
		case 'w':
			options->wait_for_drawn = true;
			break;
		case 'r':
			options->resize = true;
			break;
		case 'h':
			print_usage(argv[0]);
			*show_help = true;
			return true;
		default:
			print_usage(argv[0]);
			return false;
		}
	}

	if (optind != argc) {
		fprintf(stderr, "Error: unexpected argument: %s\n", argv[optind]);
		return false;
	}
	return true;
}

static void print_summary(const struct app *app) {
	printf("\nSummary:\n"
		"  application frames sent: %" PRIu64 "\n"
		"  FRAME_DRAWN messages:    %" PRIu64 " (%" PRIu64 " matched)\n"
		"  FRAME_TIMINGS messages:  %" PRIu64 " (%" PRIu64 " matched)\n"
		"  validation errors:       %" PRIu64 "\n",
		app->frames_sent, app->frames_drawn, app->matched_drawn,
		app->frames_timings, app->matched_timings, app->validation_errors);
	if (app->matched_drawn > 0 && app->matched_timings > 0 &&
			app->validation_errors == 0) {
		printf("RESULT: PASS (both frame messages were received and matched)\n");
	} else {
		printf("RESULT: FAIL/INCOMPLETE (inspect WM support and wlroots logs)\n");
	}
}

int main(int argc, char **argv) {
	struct options options;
	bool show_help = false;
	if (!parse_options(argc, argv, &options, &show_help)) {
		return EXIT_FAILURE;
	}
	if (show_help) {
		return EXIT_SUCCESS;
	}

	struct app app = {
		.window = XCB_WINDOW_NONE,
		.white_gc = XCB_NONE,
		.black_gc = XCB_NONE,
		.basic_counter = XCB_NONE,
		.extended_counter = XCB_NONE,
	};
	if (signal(SIGINT, handle_signal) == SIG_ERR ||
			signal(SIGTERM, handle_signal) == SIG_ERR) {
		fprintf(stderr, "Error: cannot install signal handler.\n");
		return EXIT_FAILURE;
	}
	if (!initialize_app(&app)) {
		destroy_resources(&app);
		return EXIT_FAILURE;
	}

	printf("Window: 0x%" PRIx32 "\n", app.window);
	printf("Send SIGINT to stop; --wait-drawn is useful for one-frame-at-a-time "
		"validation.\n");

	int xfd = xcb_get_file_descriptor(app.connection);
	if (xfd < 0) {
		fprintf(stderr, "Error: invalid XCB file descriptor.\n");
		app.stop = true;
	}

	uint64_t now = monotonic_usec();
	uint64_t next_frame_at = now + (uint64_t)STARTUP_DELAY_MS * UINT64_C(1000);
	uint64_t drain_until = 0;
	while (!app.stop && !stop_requested) {
		xcb_generic_event_t *event;
		while ((event = xcb_poll_for_event(app.connection)) != NULL) {
			handle_event(&app, event);
			free(event);
		}
		if (xcb_connection_has_error(app.connection)) {
			fprintf(stderr, "Error: X connection failed while running.\n");
			break;
		}

		now = monotonic_usec();
		bool done_sending = options.frames != 0 &&
			app.frames_sent >= options.frames;
		if (!done_sending) {
			if (app.pending_sync.valid && now >= next_frame_at) {
				acknowledge_pending_sync(&app, true);
			}
			bool frame_is_due = now >= next_frame_at;
			bool can_send = !options.wait_for_drawn || !app.waiting_for_drawn;
			if (frame_is_due && can_send && !app.stop) {
				if (!send_frame(&app, &options)) {
					app.stop = true;
				} else if (options.wait_for_drawn) {
					app.waiting_for_drawn = true;
					app.waiting_serial = app.extended_value;
					app.waiting_since = now;
				}
				next_frame_at = now + options.interval_ms * UINT64_C(1000);
			}
			if (options.wait_for_drawn && app.waiting_for_drawn &&
				now >= app.waiting_since +
					(uint64_t)FRAME_WAIT_TIMEOUT_MS * UINT64_C(1000)) {
				fprintf(stderr,
					"Warning: no matching FRAME_DRAWN for serial %" PRIu64
					" within %u ms; continuing.\n", app.waiting_serial,
					FRAME_WAIT_TIMEOUT_MS);
				app.waiting_for_drawn = false;
			}
		} else {
			if (drain_until == 0) {
				drain_until = now + (uint64_t)DRAIN_DELAY_MS * UINT64_C(1000);
			}
			if (now >= drain_until) {
				break;
			}
		}

		uint64_t deadline = done_sending ? drain_until : next_frame_at;
		if (options.wait_for_drawn && app.waiting_for_drawn) {
			uint64_t timeout_deadline = app.waiting_since +
				(uint64_t)FRAME_WAIT_TIMEOUT_MS * UINT64_C(1000);
			if (timeout_deadline < deadline) {
				deadline = timeout_deadline;
			}
		}
		struct pollfd pollfd = {
			.fd = xfd,
			.events = POLLIN,
		};
		int timeout = poll_timeout_ms(monotonic_usec(), deadline);
		int result = poll(&pollfd, 1, timeout);
		if (result < 0 && errno != EINTR) {
			fprintf(stderr, "Error: poll failed: %s\n", strerror(errno));
			break;
		}
	}

	if (stop_requested) {
		printf("Stopping on signal.\n");
	}
	print_summary(&app);
	destroy_resources(&app);
	return app.matched_drawn > 0 && app.matched_timings > 0 &&
		app.validation_errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
