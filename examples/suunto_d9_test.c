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

#include <stdio.h>	// fopen, fwrite, fclose

#include <libdivecomputer/suunto_d9.h>
#include <libdivecomputer/utils.h>

#include "common.h"

dc_status_t
test_dump_sdm (const char* name)
{
	device_t *device = NULL;

	message ("suunto_d9_device_open\n");
	dc_status_t rc = suunto_d9_device_open (&device, name, 0);
	if (rc != DC_STATUS_SUCCESS) {
		WARNING ("Error opening serial port.");
		return rc;
	}

	message ("device_version\n");
	unsigned char version[SUUNTO_D9_VERSION_SIZE] = {0};
	rc = device_version (device, version, sizeof (version));
	if (rc != DC_STATUS_SUCCESS) {
		WARNING ("Cannot identify computer.");
		device_close (device);
		return rc;
	}

	message ("device_foreach\n");
	rc = device_foreach (device, NULL, NULL);
	if (rc != DC_STATUS_SUCCESS) {
		WARNING ("Cannot read dives.");
		device_close (device);
		return rc;
	}

	message ("device_close\n");
	rc = device_close (device);
	if (rc != DC_STATUS_SUCCESS) {
		WARNING ("Cannot close device.");
		return rc;
	}

	return DC_STATUS_SUCCESS;
}


dc_status_t
test_dump_memory (const char* name, const char* filename)
{
	device_t *device = NULL;

	message ("suunto_d9_device_open\n");
	dc_status_t rc = suunto_d9_device_open (&device, name, 0);
	if (rc != DC_STATUS_SUCCESS) {
		WARNING ("Error opening serial port.");
		return rc;
	}

	message ("device_version\n");
	unsigned char version[SUUNTO_D9_VERSION_SIZE] = {0};
	rc = device_version (device, version, sizeof (version));
	if (rc != DC_STATUS_SUCCESS) {
		WARNING ("Cannot identify computer.");
		device_close (device);
		return rc;
	}

	dc_buffer_t *buffer = dc_buffer_new (0);

	message ("device_dump\n");
	rc = device_dump (device, buffer);
	if (rc != DC_STATUS_SUCCESS) {
		WARNING ("Cannot read memory.");
		dc_buffer_free (buffer);
		device_close (device);
		return rc;
	}

	message ("Dumping data\n");
	FILE* fp = fopen (filename, "wb");
	if (fp != NULL) {
		fwrite (dc_buffer_get_data (buffer), sizeof (unsigned char), dc_buffer_get_size (buffer), fp);
		fclose (fp);
	}

	dc_buffer_free (buffer);

	message ("device_close\n");
	rc = device_close (device);
	if (rc != DC_STATUS_SUCCESS) {
		WARNING ("Cannot close device.");
		return rc;
	}

	return DC_STATUS_SUCCESS;
}


int main(int argc, char *argv[])
{
	message_set_logfile ("D9.LOG");

#ifdef _WIN32
	const char* name = "COM1";
#else
	const char* name = "/dev/ttyS0";
#endif

	if (argc > 1) {
		name = argv[1];
	}

	message ("DEVICE=%s\n", name);

	dc_status_t a = test_dump_memory (name, "D9.DMP");
	dc_status_t b = test_dump_sdm (name);

	message ("\nSUMMARY\n");
	message ("-------\n");
	message ("test_dump_memory: %s\n", errmsg (a));
	message ("test_dump_sdm:    %s\n", errmsg (b));

	message_set_logfile (NULL);

	return 0;
}
