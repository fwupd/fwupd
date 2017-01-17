/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
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
#include <stdlib.h>
#include <unistd.h>
#include <glib-object.h>
#include "synapticsmst-common.h"
#include "synapticsmst-device.h"

#define UNIT_SIZE		32
#define MAX_WAIT_TIME 		3  /* unit : second */

gint g_fd = 0;
guint8 g_layer = 0;
guint8 g_remain_layer = 0;
guint8 g_rad = 0;

static guint8
synapticsmst_common_aux_node_read (gint offset, gint *buf, gint length)
{
	if (lseek (g_fd, offset, SEEK_SET) != offset)
		return DPCD_SEEK_FAIL;

	if (read (g_fd, buf, length) != length)
		return DPCD_ACCESS_FAIL;

	return DPCD_SUCCESS;
}

static guint8
synapticsmst_common_aux_node_write (gint offset, gint *buf, gint length)
{
	if (lseek (g_fd, offset, SEEK_SET) != offset)
		return DPCD_SEEK_FAIL;

	if (write (g_fd, buf, length) != length)
		return DPCD_ACCESS_FAIL;

	return DPCD_SUCCESS;
}

gint
synapticsmst_common_open_aux_node (const gchar* filename)
{
	guint8 byte[4];

	g_fd = open (filename, O_RDWR);

	/* can't open aux node, try use sudo to get the permission */
	if (g_fd == -1)
		return -1;

	if (synapticsmst_common_aux_node_read (REG_RC_CAP, (gint *)byte, 1) == DPCD_SUCCESS) {
		if (byte[0] & 0x04) {
			synapticsmst_common_aux_node_read (REG_VENDOR_ID, (gint *)byte, 3);
			if (byte[0] == 0x90 && byte[1] == 0xCC && byte[2] == 0x24)
				return 1;
		}
	}

	g_fd = 0;
	return 0;
}

void
synapticsmst_common_close_aux_node (void)
{
	close (g_fd);
}

void
synapticsmst_common_config_connection (guint8 layer, guint rad)
{
	g_layer = layer;
	g_remain_layer = g_layer;
	g_rad = rad;
}

guint8
synapticsmst_common_read_dpcd (gint offset, gint *buf, gint length)
{
	if (g_layer && g_remain_layer) {
		guint8 rc, node;

		g_remain_layer--;
		node = (g_rad >> g_remain_layer * 2) & 0x03;
		rc =  synapticsmst_common_rc_get_command (UPDC_READ_FROM_TX_DPCD + node, length, offset, (guint8 *)buf);
		g_remain_layer++;
		return rc;
	}
	return synapticsmst_common_aux_node_read (offset, buf, length);
}

guint8
synapticsmst_common_write_dpcd (gint offset, gint *buf, gint length)
{
	if (g_layer && g_remain_layer) {
		guint8 rc, node;

		g_remain_layer--;
		node = (g_rad >> g_remain_layer * 2) & 0x03;
		rc =  synapticsmst_common_rc_set_command (UPDC_WRITE_TO_TX_DPCD + node, length, offset, (guint8 *)buf);
		g_remain_layer++;
		return rc;
	} else {
		return synapticsmst_common_aux_node_write (offset, buf, length);
	}
}

guint8
synapticsmst_common_rc_set_command (gint rc_cmd, gint length, gint offset, guint8 *buf)
{
	guint8 rc = 0;
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
			/* write data */
			rc = synapticsmst_common_write_dpcd (REG_RC_DATA, (gint *)buf, cur_length);
			if (rc)
				break;

			/* write offset */
			rc = synapticsmst_common_write_dpcd (REG_RC_OFFSET, &cur_offset, 4);
			if (rc)
				break;

			/* write length */
			rc = synapticsmst_common_write_dpcd (REG_RC_LEN, &cur_length, 4);
			if (rc)
				break;
		}

		/* send command */
		cmd = 0x80 | rc_cmd;
		rc = synapticsmst_common_write_dpcd (REG_RC_CMD, &cmd, 1);
		if (rc)
			break;

		/* wait command complete */
		clock_gettime (CLOCK_REALTIME, &t_spec);
		deadline = t_spec.tv_sec + MAX_WAIT_TIME;

		do {
			rc = synapticsmst_common_read_dpcd (REG_RC_CMD, &readData, 2);
			clock_gettime (CLOCK_REALTIME, &t_spec);
			if (t_spec.tv_sec > deadline) {
				rc = -1;
			}
		} while (rc == 0 && readData & 0x80);

		if (rc)
			break;
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

guint8
synapticsmst_common_rc_get_command (gint rc_cmd, gint length, gint offset, guint8 *buf)
{
	guint8 rc = 0;
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
			/* write offset */
			rc = synapticsmst_common_write_dpcd (REG_RC_OFFSET, &cur_offset, 4);
			if (rc)
				break;

			/* write length */
			rc = synapticsmst_common_write_dpcd (REG_RC_LEN, &cur_length, 4);
			if (rc)
				break;
		}

		/* send command */
		cmd = 0x80 | rc_cmd;
		rc = synapticsmst_common_write_dpcd (REG_RC_CMD, &cmd, 1);
		if (rc)
			break;

		/* wait command complete */
		clock_gettime (CLOCK_REALTIME, &t_spec);
		deadline = t_spec.tv_sec + MAX_WAIT_TIME;

		do {
			rc = synapticsmst_common_read_dpcd (REG_RC_CMD, &readData, 2);
			clock_gettime (CLOCK_REALTIME, &t_spec);
			if (t_spec.tv_sec > deadline) {
				rc = -1;
			}
		} while (rc == 0 && readData & 0x80);

		if (rc)
			break;
		else if (readData & 0xFF00) {
			rc = (readData >> 8) & 0xFF;
			break;
		}

		if (cur_length) {
			rc = synapticsmst_common_read_dpcd (REG_RC_DATA, (gint *)buf, cur_length);
			if (rc)
				break;
		}

		buf += cur_length;
		cur_offset += cur_length;
		data_need -= cur_length;
	}

	return rc;
}

guint8
synapticsmst_common_rc_special_get_command (gint rc_cmd,
					    gint cmd_length,
					    gint cmd_offset,
					    guint8 *cmd_data,
					    gint length,
					    guint8 *buf)
{
	guint8 rc = 0;
	gint readData = 0;
	gint cmd;
	long deadline;
	struct timespec t_spec;

	if (cmd_length) {
		/* write cmd data */
		if (cmd_data != NULL) {
			rc = synapticsmst_common_write_dpcd (REG_RC_DATA, (gint *)cmd_data, cmd_length);
			if (rc)
				return rc;
		}

		/* write offset */
		rc = synapticsmst_common_write_dpcd (REG_RC_OFFSET, &cmd_offset, 4);
		if (rc)
			return rc;

		/* write length */
		rc = synapticsmst_common_write_dpcd (REG_RC_LEN, &cmd_length, 4);
		if (rc)
			return rc;
	}

	/* send command */
	cmd = 0x80 | rc_cmd;
	rc = synapticsmst_common_write_dpcd (REG_RC_CMD, &cmd, 1);
	if (rc)
		return rc;

	/* wait command complete */
	clock_gettime (CLOCK_REALTIME, &t_spec);
	deadline = t_spec.tv_sec + MAX_WAIT_TIME;
	do {
		rc = synapticsmst_common_read_dpcd (REG_RC_CMD, &readData, 2);
		clock_gettime (CLOCK_REALTIME, &t_spec);
		if (t_spec.tv_sec > deadline)
			return -1;
	} while (readData & 0x80);

	if (rc)
		return rc;
	else if (readData & 0xFF00) {
		rc = (readData >> 8) & 0xFF;
		return rc;
	}

	if (length) {
		rc = synapticsmst_common_read_dpcd (REG_RC_DATA, (gint *)buf, length);
		if (rc)
			return rc;
	}

	return rc;
}

guint8
synapticsmst_common_enable_remote_control (void)
{
	const gchar *sc = "PRIUS";
	guint8 tmp_layer = g_layer;
	guint8 rc = 0;

	for (gint i = 0; i <= tmp_layer; i++) {
		synapticsmst_common_config_connection (i, g_rad);
		rc = synapticsmst_common_rc_set_command (UPDC_ENABLE_RC, 5, 0, (guint8*)sc);
		if (rc)
			break;
	}

	synapticsmst_common_config_connection (tmp_layer, g_rad);
	return rc;
}

guint8
synapticsmst_common_disable_remote_control (void)
{
	guint8 tmp_layer = g_layer;
	guint8 rc = 0;

	for (gint i = tmp_layer; i >= 0; i--) {
		synapticsmst_common_config_connection (i, g_rad);
		rc = synapticsmst_common_rc_set_command (UPDC_DISABLE_RC, 0, 0, (guint8*)NULL);
		if (rc)
			break;
	}

	synapticsmst_common_config_connection (tmp_layer, g_rad);
	return rc;
}
