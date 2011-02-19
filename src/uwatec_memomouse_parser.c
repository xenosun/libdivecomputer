/* 
 * libdivecomputer
 * 
 * Copyright (C) 2008 Jef Driesen
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

#include <stdlib.h>

#include <libdivecomputer/uwatec_memomouse.h>
#include <libdivecomputer/utils.h>

#include "parser-private.h"
#include "array.h"

typedef struct uwatec_memomouse_parser_t uwatec_memomouse_parser_t;

struct uwatec_memomouse_parser_t {
	parser_t base;
	unsigned int devtime;
	dc_ticks_t systime;
};

static dc_status_t uwatec_memomouse_parser_set_data (parser_t *abstract, const unsigned char *data, unsigned int size);
static dc_status_t uwatec_memomouse_parser_get_datetime (parser_t *abstract, dc_datetime_t *datetime);
static dc_status_t uwatec_memomouse_parser_get_field (parser_t *abstract, parser_field_type_t type, unsigned int flags, void *value);
static dc_status_t uwatec_memomouse_parser_samples_foreach (parser_t *abstract, sample_callback_t callback, void *userdata);
static dc_status_t uwatec_memomouse_parser_destroy (parser_t *abstract);

static const parser_backend_t uwatec_memomouse_parser_backend = {
	PARSER_TYPE_UWATEC_MEMOMOUSE,
	uwatec_memomouse_parser_set_data, /* set_data */
	uwatec_memomouse_parser_get_datetime, /* datetime */
	uwatec_memomouse_parser_get_field, /* fields */
	uwatec_memomouse_parser_samples_foreach, /* samples_foreach */
	uwatec_memomouse_parser_destroy /* destroy */
};


static int
parser_is_uwatec_memomouse (parser_t *abstract)
{
	if (abstract == NULL)
		return 0;

    return abstract->backend == &uwatec_memomouse_parser_backend;
}


dc_status_t
uwatec_memomouse_parser_create (parser_t **out, unsigned int devtime, dc_ticks_t systime)
{
	if (out == NULL)
		return DC_STATUS_INVALIDARGS;

	// Allocate memory.
	uwatec_memomouse_parser_t *parser = (uwatec_memomouse_parser_t *) malloc (sizeof (uwatec_memomouse_parser_t));
	if (parser == NULL) {
		WARNING ("Failed to allocate memory.");
		return DC_STATUS_NOMEMORY;
	}

	// Initialize the base class.
	parser_init (&parser->base, &uwatec_memomouse_parser_backend);

	// Set the default values.
	parser->devtime = devtime;
	parser->systime = systime;

	*out = (parser_t*) parser;

	return DC_STATUS_SUCCESS;
}


static dc_status_t
uwatec_memomouse_parser_destroy (parser_t *abstract)
{
	if (! parser_is_uwatec_memomouse (abstract))
		return DC_STATUS_INVALIDARGS;

	// Free memory.	
	free (abstract);

	return DC_STATUS_SUCCESS;
}


static dc_status_t
uwatec_memomouse_parser_set_data (parser_t *abstract, const unsigned char *data, unsigned int size)
{
	if (! parser_is_uwatec_memomouse (abstract))
		return DC_STATUS_INVALIDARGS;

	return DC_STATUS_SUCCESS;
}


static dc_status_t
uwatec_memomouse_parser_get_datetime (parser_t *abstract, dc_datetime_t *datetime)
{
	uwatec_memomouse_parser_t *parser = (uwatec_memomouse_parser_t *) abstract;

	if (abstract->size < 11 + 4)
		return DC_STATUS_DATAFORMAT;

	unsigned int timestamp = array_uint32_le (abstract->data + 11);

	dc_ticks_t ticks = parser->systime - (parser->devtime - timestamp) / 2;

	if (!dc_datetime_localtime (datetime, ticks))
		return DC_STATUS_DATAFORMAT;

	return DC_STATUS_SUCCESS;
}


static dc_status_t
uwatec_memomouse_parser_get_field (parser_t *abstract, parser_field_type_t type, unsigned int flags, void *value)
{
	const unsigned char *data = abstract->data;
	unsigned int size = abstract->size;

	if (size < 18)
		return DC_STATUS_DATAFORMAT;

	unsigned int model = data[3];

	int is_nitrox = 0, is_oxygen = 0, is_air = 0;
	if ((model & 0xF0) == 0xF0)
		is_nitrox = 1;
	if ((model & 0xF0) == 0xA0)
		is_oxygen = 1;
	if ((model & 0xF0) % 4 == 0)
		is_air = 1;

	unsigned int header = 22;
	if (is_nitrox)
		header += 2;
	if (is_oxygen)
		header += 3;

	gasmix_t *gasmix = (gasmix_t *) value;

	if (value) {
		switch (type) {
		case FIELD_TYPE_DIVETIME:
			*((unsigned int *) value) = ((data[4] & 0x04 ? 100 : 0) + bcd2dec (data[5])) * 60;
			break;
		case FIELD_TYPE_MAXDEPTH:
			*((double *) value) = ((array_uint16_be (data + 6) & 0xFFC0) >> 6) * 10.0 / 64.0;
			break;
		case FIELD_TYPE_GASMIX_COUNT:
			*((unsigned int *) value) = 1;
			break;
		case FIELD_TYPE_GASMIX:
			gasmix->helium = 0.0;
			if (size >= header + 18) {
				if (is_oxygen)
					gasmix->oxygen = data[18 + 23] / 100.0;
				else if (is_nitrox)
					gasmix->oxygen = (data[18 + 23] & 0x0F ? 20.0 + 2 * (data[18 + 23] & 0x0F) : 21.0) / 100.0;
				else
					gasmix->oxygen = 0.21;
			} else {
				gasmix->oxygen = 0.21;
			}
			gasmix->nitrogen = 1.0 - gasmix->oxygen - gasmix->helium;
			break;
		default:
			return DC_STATUS_UNSUPPORTED;
		}
	}

	return DC_STATUS_SUCCESS;
}


static dc_status_t
uwatec_memomouse_parser_samples_foreach (parser_t *abstract, sample_callback_t callback, void *userdata)
{
	if (! parser_is_uwatec_memomouse (abstract))
		return DC_STATUS_INVALIDARGS;

	const unsigned char *data = abstract->data;
	unsigned int size = abstract->size;

	if (size < 18)
		return DC_STATUS_DATAFORMAT;

	unsigned int model = data[3];

	int is_nitrox = 0, is_oxygen = 0, is_air = 0;
	if ((model & 0xF0) == 0xF0)
		is_nitrox = 1;
	if ((model & 0xF0) == 0xA0)
		is_oxygen = 1;
	if ((model & 0xF0) % 4 == 0)
		is_air = 1;

	unsigned int header = 22;
	if (is_nitrox)
		header += 2;
	if (is_oxygen)
		header += 3;

	unsigned int time = 20;

	unsigned int offset = header + 18;
	while (offset + 2 <= size) {
		parser_sample_value_t sample = {0};

		unsigned int value = array_uint16_be (data + offset);
		unsigned int depth = (value & 0xFFC0) >> 6;
		unsigned int warnings = (value & 0x3F);
		offset += 2;

		// Time (seconds)
		sample.time = time;
		if (callback) callback (SAMPLE_TYPE_TIME, sample, userdata);

		// Depth (meters)
		sample.depth = depth * 10.0 / 64.0;
		if (callback) callback (SAMPLE_TYPE_DEPTH, sample, userdata);

		// Warnings
		for (unsigned int i = 0; i < 6; ++i) {
			if (warnings & (1 << i)) {
				sample.event.time = 0;
				sample.event.flags = 0;
				sample.event.value = 0;
				switch (i) {
				case 0: // Deco stop
					sample.event.type = SAMPLE_EVENT_DECOSTOP;
					break;
				case 1: // Remaining bottom time too short (Air series only)
					sample.event.type = SAMPLE_EVENT_RBT;
					break;
				case 2: // Ascent too fast
					sample.event.type = SAMPLE_EVENT_ASCENT;
					break;
				case 3: // Ceiling violation of deco stop
					sample.event.type = SAMPLE_EVENT_CEILING;
					break;
				case 4: // Work too hard (Air series only)
					sample.event.type = SAMPLE_EVENT_WORKLOAD;
					break;
				case 5: // Transmit error of air pressure (always 1 unless Air series)
					sample.event.type = SAMPLE_EVENT_TRANSMITTER;
					break;
				}
				if (callback) callback (SAMPLE_TYPE_EVENT, sample, userdata);
			}
		}

		if (time % 60 == 0) {
			sample.vendor.type = SAMPLE_VENDOR_UWATEC_ALADIN;
			sample.vendor.size = 0;
			sample.vendor.data = data + offset;
			
			// Decompression information.
			if (offset + 1 > size)
				return DC_STATUS_DATAFORMAT;
			sample.vendor.size++;
			offset++;

			// Oxygen percentage (O2 series only).
			if (is_oxygen) {
				if (offset + 1 > size)
					return DC_STATUS_DATAFORMAT;
				sample.vendor.size++;
				offset++;
			}

			if (callback) callback (SAMPLE_TYPE_VENDOR, sample, userdata);
		}

		time += 20;
	}

	return DC_STATUS_SUCCESS;
}
