#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>

enum {
	xsettings_type_integer = 0,
	xsettings_type_string = 1,
	xsettings_type_color = 2,
	xsettings_byte_order_lsb = 0,
	xsettings_byte_order_msb = 1,
};

static bool has_bytes(size_t length, size_t offset, size_t needed) {
	return offset <= length && needed <= length - offset;
}

static size_t align4(size_t value) {
	return (value + 3U) & ~(size_t)3U;
}

static uint16_t read_u16(const uint8_t *data, bool lsb_first) {
	if (lsb_first) {
		return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
	}
	return ((uint16_t)data[0] << 8U) | (uint16_t)data[1];
}

static uint32_t read_u32(const uint8_t *data, bool lsb_first) {
	if (lsb_first) {
		return (uint32_t)data[0] |
			((uint32_t)data[1] << 8U) |
			((uint32_t)data[2] << 16U) |
			((uint32_t)data[3] << 24U);
	}
	return ((uint32_t)data[0] << 24U) |
		((uint32_t)data[1] << 16U) |
		((uint32_t)data[2] << 8U) |
		(uint32_t)data[3];
}

static void print_escaped_string(const uint8_t *data, size_t length) {
	putchar('"');
	for (size_t i = 0; i < length; ++i) {
		uint8_t ch = data[i];
		if (ch == '"' || ch == '\\') {
			printf("\\%c", ch);
		} else if (ch >= 0x20 && ch <= 0x7e) {
			putchar(ch);
		} else {
			printf("\\x%02" PRIx8, ch);
		}
	}
	putchar('"');
}

static bool parse_xsettings(const uint8_t *data, size_t length) {
	if (!has_bytes(length, 0, 12)) {
		fprintf(stderr, "Error: XSettings property is too short.\n");
		return false;
	}

	if (data[0] != xsettings_byte_order_lsb &&
		data[0] != xsettings_byte_order_msb) {
		fprintf(stderr, "Error: Invalid XSettings byte order: %" PRIu8 ".\n",
			data[0]);
		return false;
	}

	bool lsb_first = data[0] == xsettings_byte_order_lsb;
	uint32_t serial = read_u32(data + 4, lsb_first);
	uint32_t setting_count = read_u32(data + 8, lsb_first);
	size_t offset = 12;

	printf("XSettings data:\n");
	printf("  byte order: %s\n", lsb_first ? "LSBFirst" : "MSBFirst");
	printf("  serial: %" PRIu32 "\n", serial);
	printf("  settings: %" PRIu32 "\n", setting_count);

	for (uint32_t i = 0; i < setting_count; ++i) {
		if (!has_bytes(length, offset, 4)) {
			fprintf(stderr, "Error: Truncated setting header at index %" PRIu32 ".\n",
				i);
			return false;
		}

		uint8_t type = data[offset];
		uint16_t name_len = read_u16(data + offset + 2, lsb_first);
		offset += 4;

		if (!has_bytes(length, offset, name_len)) {
			fprintf(stderr, "Error: Truncated setting name at index %" PRIu32 ".\n",
				i);
			return false;
		}

		const uint8_t *name = data + offset;
		size_t padded_name_len = align4(name_len);
		if (!has_bytes(length, offset, padded_name_len)) {
			fprintf(stderr, "Error: Truncated setting name padding at index %" PRIu32 ".\n",
				i);
			return false;
		}
		offset += padded_name_len;

		if (!has_bytes(length, offset, 4)) {
			fprintf(stderr, "Error: Truncated setting serial at index %" PRIu32 ".\n",
				i);
			return false;
		}

		uint32_t last_change_serial = read_u32(data + offset, lsb_first);
		offset += 4;

		printf("  [%03" PRIu32 "] %.*s\n", i, name_len, (const char *)name);
		printf("        last change serial: %" PRIu32 "\n", last_change_serial);

		switch (type) {
		case xsettings_type_integer:
			if (!has_bytes(length, offset, 4)) {
				fprintf(stderr, "Error: Truncated integer value at index %" PRIu32 ".\n",
					i);
				return false;
			}
			printf("        type: integer\n");
			printf("        value: %" PRId32 "\n",
				(int32_t)read_u32(data + offset, lsb_first));
			offset += 4;
			break;
		case xsettings_type_string: {
			if (!has_bytes(length, offset, 4)) {
				fprintf(stderr, "Error: Truncated string length at index %" PRIu32 ".\n",
					i);
				return false;
			}
			uint32_t value_len = read_u32(data + offset, lsb_first);
			offset += 4;

			if (!has_bytes(length, offset, value_len)) {
				fprintf(stderr, "Error: Truncated string value at index %" PRIu32 ".\n",
					i);
				return false;
			}

			printf("        type: string\n");
			printf("        value: ");
			print_escaped_string(data + offset, value_len);
			putchar('\n');

			size_t padded_value_len = align4(value_len);
			if (!has_bytes(length, offset, padded_value_len)) {
				fprintf(stderr,
					"Error: Truncated string value padding at index %" PRIu32 ".\n",
					i);
				return false;
			}
			offset += padded_value_len;
			break;
		}
		case xsettings_type_color:
			if (!has_bytes(length, offset, 8)) {
				fprintf(stderr, "Error: Truncated color value at index %" PRIu32 ".\n",
					i);
				return false;
			}
			printf("        type: color\n");
			printf("        value: red=%" PRIu16 ", green=%" PRIu16
				", blue=%" PRIu16 ", alpha=%" PRIu16 "\n",
				read_u16(data + offset, lsb_first),
				read_u16(data + offset + 2, lsb_first),
				read_u16(data + offset + 4, lsb_first),
				read_u16(data + offset + 6, lsb_first));
			offset += 8;
			break;
		default:
			fprintf(stderr, "Error: Unknown setting type %" PRIu8
				" at index %" PRIu32 ".\n", type, i);
			return false;
		}
	}

	return true;
}

static xcb_atom_t intern_atom(xcb_connection_t *connection, const char *name) {
	xcb_intern_atom_cookie_t cookie =
		xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name);
	xcb_intern_atom_reply_t *reply =
		xcb_intern_atom_reply(connection, cookie, NULL);

	if (reply == NULL) {
		return XCB_ATOM_NONE;
	}

	xcb_atom_t atom = reply->atom;
	free(reply);
	return atom;
}

int main(void) {
	int screen_number = 0;
	xcb_connection_t *connection = xcb_connect(NULL, &screen_number);
	if (xcb_connection_has_error(connection)) {
		fprintf(stderr, "Error: Cannot open X display.\n");
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	char selection_name[32];
	int selection_name_len = snprintf(
		selection_name,
		sizeof(selection_name),
		"_XSETTINGS_S%d",
		screen_number
	);
	if (selection_name_len < 0 ||
		(size_t)selection_name_len >= sizeof(selection_name)) {
		fprintf(stderr, "Error: Failed to build XSettings selection name.\n");
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	xcb_atom_t selection_atom = intern_atom(connection, selection_name);
	if (selection_atom == XCB_ATOM_NONE) {
		fprintf(stderr, "Error: Failed to intern %s.\n", selection_name);
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	xcb_generic_error_t *error = NULL;
	xcb_get_selection_owner_reply_t *owner_reply =
		xcb_get_selection_owner_reply(
			connection,
			xcb_get_selection_owner(connection, selection_atom),
			&error
		);
	if (owner_reply == NULL) {
		fprintf(stderr, "Error: Failed to query XSettings owner%s.\n",
			error == NULL ? "" : " (X11 protocol error)");
		free(error);
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	xcb_window_t owner = owner_reply->owner;
	free(owner_reply);

	if (owner == XCB_NONE) {
		printf("No XSettings owner for %s.\n", selection_name);
		xcb_disconnect(connection);
		return EXIT_SUCCESS;
	}

	printf("XSettings owner of %s: 0x%08" PRIx32 "\n", selection_name, owner);

	const char *property_name = "_XSETTINGS_SETTINGS";
	xcb_atom_t property_atom = intern_atom(connection, property_name);
	if (property_atom == XCB_ATOM_NONE) {
		fprintf(stderr, "Error: Failed to intern %s.\n", property_name);
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	error = NULL;
	xcb_get_property_reply_t *property_reply =
		xcb_get_property_reply(
			connection,
			xcb_get_property(
				connection,
				0,
				owner,
				property_atom,
				property_atom,
				0,
				(1024U * 1024U) / 4U
			),
			&error
		);
	if (property_reply == NULL) {
		fprintf(stderr, "Error: Failed to query %s%s.\n", property_name,
			error == NULL ? "" : " (X11 protocol error)");
		free(error);
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	if (property_reply->type == XCB_ATOM_NONE) {
		printf("No %s property on owner window.\n", property_name);
		free(property_reply);
		xcb_disconnect(connection);
		return EXIT_SUCCESS;
	}

	if (property_reply->format != 8) {
		fprintf(stderr, "Error: Unexpected %s format: %" PRIu8 ".\n",
			property_name, property_reply->format);
		free(property_reply);
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	if (property_reply->bytes_after != 0) {
		fprintf(stderr, "Error: %s is larger than the read buffer.\n",
			property_name);
		free(property_reply);
		xcb_disconnect(connection);
		return EXIT_FAILURE;
	}

	int value_length = xcb_get_property_value_length(property_reply);
	const uint8_t *value =
		(const uint8_t *)xcb_get_property_value(property_reply);
	bool ok = parse_xsettings(value, (size_t)value_length);

	free(property_reply);
	xcb_disconnect(connection);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
