/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <smbios_c/smbios.h>

#include <glib-object.h>

//#include <sys/types.h>
//#include <string.h>
#include "synapticsmst-common.h"

#define UNIT_SIZE		32
#define MAX_WAIT_TIME		3  // second

typedef enum {
	DPCD_SUCCESS = 0,
	DPCD_SEEK_FAIL,
	DPCD_ACCESS_FAIL,
} dpcd_return;

gint fd = 0;

guchar
synapticsmst_common_read_dpcd (gint offset, gint *buf, gint length)
{
	if (lseek (fd, offset, SEEK_SET) != offset) {
	//	g_debug ("dpcd read fail in finding address %05x\n", offset);
		return DPCD_SEEK_FAIL;
	}

	if (read (fd, buf, length) != length) {
	//	g_debug ("dpcd read fail reading from address %05x\n", offset);
		return DPCD_ACCESS_FAIL;
	}

	return DPCD_SUCCESS;
}

guchar
synapticsmst_common_write_dpcd (gint offset, gint *buf, gint length)
{
	if (lseek(fd, offset, SEEK_SET) != offset) {
	//	g_debug ("dpcd write fail in finding address %05x\n", offset);
		return DPCD_SEEK_FAIL;
	}

	if (write(fd, buf, length) != length) {
	//	g_debug ("dpcd write fail in writing to address %05x\n", offset);
		return DPCD_ACCESS_FAIL;
	}

	return DPCD_SUCCESS;
}


guchar
synapticsmst_common_open_aux_node (const gchar *filename)
{
	guchar byte[4];

	fd = open (filename, O_RDWR);
	if (synapticsmst_common_read_dpcd (REG_RC_CAP, (gint *)byte, 1) == DPCD_SUCCESS) {
		if (byte[0] & 0x04) {
			synapticsmst_common_read_dpcd (REG_VENDOR_ID, (gint *)byte, 3);
			if (byte[0] == 0x90 && byte[1] == 0xCC && byte[2] == 0x24) {
				return 1;
			}
		}
	}

	fd = 0;
	return 0;
}

void
synapticsmst_common_close_aux_node (void)
{
	close (fd);
}

guchar
synapticsmst_common_rc_set_command (gint rc_cmd, gint length, gint offset, guchar *buf)
{
	guchar rc = 0;
	gint cur_offset = offset;
	gint cur_length;
	gint data_left = length;
	gint cmd;
	gint readData = 0;
	long deadline;
	struct timespec t_spec;

	do{
		if (data_left > UNIT_SIZE) {
			cur_length = UNIT_SIZE;
		} else {
			cur_length = data_left;
		}

		if (cur_length) {
			// write data
			rc = synapticsmst_common_write_dpcd (REG_RC_DATA, (gint *)buf, cur_length);
			if (rc) {
				break;
			}

			//write offset
			rc = synapticsmst_common_write_dpcd (REG_RC_OFFSET, &cur_offset, 4);
			if (rc) {
				break;
			}

			// write length
			rc = synapticsmst_common_write_dpcd (REG_RC_LEN, &cur_length, 4);
			if (rc) {
				break;
			}
		}

		// send command
		cmd = 0x80 | rc_cmd;
		rc = synapticsmst_common_write_dpcd (REG_RC_CMD, &cmd, 1);
		if (rc) {
			break;
		}

		// wait command complete
		clock_gettime (CLOCK_REALTIME, &t_spec);
		deadline = t_spec.tv_sec + MAX_WAIT_TIME;

		do {
			rc = synapticsmst_common_read_dpcd (REG_RC_CMD, &readData, 2);
			clock_gettime (CLOCK_REALTIME, &t_spec);
			if (t_spec.tv_sec > deadline) {
				rc = -1;
			}
		} while (rc == 0 && readData & 0x80);

		if (rc) {
		//	g_debug ("checking result timeout");
			break;
		}
		else if (readData & 0xFF00) {
			rc = (readData >> 8) & 0xFF;
			break;
		}

		buf += cur_length;
		cur_offset += cur_length;
		data_left -= cur_length;
	} while (data_left);

	return rc;
}

guchar
synapticsmst_common_rc_get_command(gint rc_cmd, gint length, gint offset, guchar *buf)
{
	guchar rc = 0;
	gint cur_offset = offset;
	gint cur_length;
	gint data_need = length;
	gint cmd;
	gint readData = 0;
	long deadline;
	struct timespec t_spec;

	while (data_need) {
		if (data_need > UNIT_SIZE) {
			cur_length = UNIT_SIZE;
		}
		else {
			cur_length = data_need;
		}

		if (cur_length) {
			//write offset
			rc = synapticsmst_common_write_dpcd (REG_RC_OFFSET, &cur_offset, 4);
			if (rc) {
				break;
			}

			// write length
			rc = synapticsmst_common_write_dpcd (REG_RC_LEN, &cur_length, 4);
			if (rc) {
				break;
			}
		}

		// send command
		cmd = 0x80 | rc_cmd;
		rc = synapticsmst_common_write_dpcd (REG_RC_CMD, &cmd, 1);
		if (rc) {
			break;
		}

		// wait command complete
		clock_gettime (CLOCK_REALTIME, &t_spec);
		deadline = t_spec.tv_sec + MAX_WAIT_TIME;

		do {
			rc = synapticsmst_common_read_dpcd (REG_RC_CMD, &readData, 2);
			clock_gettime (CLOCK_REALTIME, &t_spec);
			if (t_spec.tv_sec > deadline) {
				rc = -1;
			}
		} while (rc == 0 && readData & 0x80);

		if (rc) {
		//	g_debug ("checking result timeout");
			break;
		}
		else if (readData & 0xFF00) {
			rc = (readData >> 8) & 0xFF;
			break;
		}

		if (cur_length) {
			rc = synapticsmst_common_read_dpcd (REG_RC_DATA, (gint *)buf, cur_length);
			if (rc) {
				break;
			}
		}

		buf += cur_length;
		cur_offset += cur_length;
		data_need -= cur_length;
	}

	return rc;
}

guchar
synapticsmst_common_rc_special_get_command (gint rc_cmd,
					   gint cmd_length,
					   gint cmd_offset,
					   guchar *cmd_data,
					   gint length,
					   guchar *buf)
{
	guchar rc = 0;
	gint readData = 0;
	gint cmd;
	long deadline;
	struct timespec t_spec;

	do {
		if (cmd_length) {
			// write cmd data
			if (cmd_data != NULL) {
				rc = synapticsmst_common_write_dpcd (REG_RC_DATA, (gint *)cmd_data, cmd_length);
				if (rc) {
					break;
				}
			}

			// write offset
			rc = synapticsmst_common_write_dpcd (REG_RC_OFFSET, &cmd_offset, 4);
			if (rc) {
				break;
			}

			// write length
			rc = synapticsmst_common_write_dpcd (REG_RC_LEN, &cmd_length, 4);
			if (rc) {
				break;
			}
		}

		// send command
		cmd = 0x80 | rc_cmd;
		rc = synapticsmst_common_write_dpcd (REG_RC_CMD, &cmd, 1);
		if (rc) {
			break;
		}

		// wait command complete
		clock_gettime (CLOCK_REALTIME, &t_spec);
		deadline = t_spec.tv_sec + MAX_WAIT_TIME;

		do {
			rc = synapticsmst_common_read_dpcd (REG_RC_CMD, &readData, 2);
			clock_gettime (CLOCK_REALTIME, &t_spec);
			if (t_spec.tv_sec > deadline) {
				rc = -1;
			}
		} while (rc == 0 && readData & 0x80);

		if (rc) {
		//	g_debug ("checking result timeout");
			break;
		}
		else if (readData & 0xFF00) {
			rc = (readData >> 8) & 0xFF;
			break;
		}

		if (length) {
			rc = synapticsmst_common_read_dpcd (REG_RC_DATA, (gint *)buf, length);
			if (rc) {
				break;
			}
		}
	} while (0);

	return rc;
}

gboolean synapticsmst_common_check_supported_system (GError **error)
{
	guint8 dell_supported = 0;
	struct smbios_struct *de_table;
	de_table = smbios_get_next_struct_by_type (0, 0xDE);
	smbios_struct_get_data (de_table, &(dell_supported), 0x00, sizeof(guint8));
	if (dell_supported != 0xDE) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "SynapticsMST: firmware updating not supported. (%x)",
			     dell_supported);
		return FALSE;
	}
	return TRUE;
}
