/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

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

struct _SynapticsMSTConnection {
	gint		 fd;		/* not owned by the connection */
	guint8		 layer;
	guint8		 remain_layer;
	guint8		 rad;
};

guint8
synapticsmst_common_aux_node_read (SynapticsMSTConnection *connection,
				   gint offset, gint *buf, gint length)
{
	if (lseek (connection->fd, offset, SEEK_SET) != offset)
		return DPCD_SEEK_FAIL;

	if (read (connection->fd, buf, length) != length)
		return DPCD_ACCESS_FAIL;

	return DPCD_SUCCESS;
}

static guint8
synapticsmst_common_aux_node_write (SynapticsMSTConnection *connection,
				    gint offset, const gint *buf, gint length)
{
	if (lseek (connection->fd, offset, SEEK_SET) != offset)
		return DPCD_SEEK_FAIL;

	if (write (connection->fd, buf, length) != length)
		return DPCD_ACCESS_FAIL;

	return DPCD_SUCCESS;
}

void
synapticsmst_common_free (SynapticsMSTConnection *connection)
{
	g_free (connection);
}

SynapticsMSTConnection *
synapticsmst_common_new (gint fd, guint8 layer, guint rad)
{
	SynapticsMSTConnection *connection = g_new0 (SynapticsMSTConnection, 1);
	connection->fd = fd;
	connection->layer = layer;
	connection->remain_layer = layer;
	connection->rad = rad;
	return connection;
}

guint8
synapticsmst_common_read_dpcd (SynapticsMSTConnection *connection,
			       gint offset, gint *buf, gint length)
{
	if (connection->layer && connection->remain_layer) {
		guint8 rc, node;

		connection->remain_layer--;
		node = (connection->rad >> connection->remain_layer * 2) & 0x03;
		rc =  synapticsmst_common_rc_get_command (connection,
							  UPDC_READ_FROM_TX_DPCD + node,
							  length, offset, (guint8 *)buf);
		connection->remain_layer++;
		return rc;
	}
	return synapticsmst_common_aux_node_read (connection, offset, buf, length);
}

guint8
synapticsmst_common_write_dpcd (SynapticsMSTConnection *connection,
				gint offset,
				const gint *buf,
				gint length)
{
	if (connection->layer && connection->remain_layer) {
		guint8 rc, node;

		connection->remain_layer--;
		node = (connection->rad >> connection->remain_layer * 2) & 0x03;
		rc =  synapticsmst_common_rc_set_command (connection,
							  UPDC_WRITE_TO_TX_DPCD + node,
							  length, offset, (guint8 *)buf);
		connection->remain_layer++;
		return rc;
	}
	return synapticsmst_common_aux_node_write (connection, offset, buf, length);
}

guint8
synapticsmst_common_rc_set_command (SynapticsMSTConnection *connection,
				    gint rc_cmd,
				    gint length,
				    gint offset,
				    const guint8 *buf)
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
			rc = synapticsmst_common_write_dpcd (connection, REG_RC_DATA, (gint *)buf, cur_length);
			if (rc)
				break;

			/* write offset */
			rc = synapticsmst_common_write_dpcd (connection,
							     REG_RC_OFFSET,
							     &cur_offset, 4);
			if (rc)
				break;

			/* write length */
			rc = synapticsmst_common_write_dpcd (connection,
							     REG_RC_LEN,
							     &cur_length, 4);
			if (rc)
				break;
		}

		/* send command */
		cmd = 0x80 | rc_cmd;
		rc = synapticsmst_common_write_dpcd (connection,
						     REG_RC_CMD,
						     &cmd, 1);
		if (rc)
			break;

		/* wait command complete */
		clock_gettime (CLOCK_REALTIME, &t_spec);
		deadline = t_spec.tv_sec + MAX_WAIT_TIME;

		do {
			rc = synapticsmst_common_read_dpcd (connection,
							    REG_RC_CMD,
							    &readData, 2);
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
synapticsmst_common_rc_get_command (SynapticsMSTConnection *connection,
				    gint rc_cmd,
				    gint length,
				    gint offset,
				    guint8 *buf)
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
		} else {
			cur_length = data_need;
		}

		if (cur_length) {
			/* write offset */
			rc = synapticsmst_common_write_dpcd (connection,
							     REG_RC_OFFSET,
							     &cur_offset, 4);
			if (rc)
				break;

			/* write length */
			rc = synapticsmst_common_write_dpcd (connection,
							     REG_RC_LEN,
							     &cur_length, 4);
			if (rc)
				break;
		}

		/* send command */
		cmd = 0x80 | rc_cmd;
		rc = synapticsmst_common_write_dpcd (connection,
						     REG_RC_CMD,
						     &cmd, 1);
		if (rc)
			break;

		/* wait command complete */
		clock_gettime (CLOCK_REALTIME, &t_spec);
		deadline = t_spec.tv_sec + MAX_WAIT_TIME;

		do {
			rc = synapticsmst_common_read_dpcd (connection,
							    REG_RC_CMD,
							    &readData, 2);
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
			rc = synapticsmst_common_read_dpcd (connection,
							    REG_RC_DATA,
							    (gint *)buf,
							    cur_length);
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
synapticsmst_common_rc_special_get_command (SynapticsMSTConnection *connection,
					    gint rc_cmd,
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
			rc = synapticsmst_common_write_dpcd (connection,
							     REG_RC_DATA,
							     (gint *)cmd_data,
							     cmd_length);
			if (rc)
				return rc;
		}

		/* write offset */
		rc = synapticsmst_common_write_dpcd (connection,
						     REG_RC_OFFSET,
						     &cmd_offset, 4);
		if (rc)
			return rc;

		/* write length */
		rc = synapticsmst_common_write_dpcd (connection,
						     REG_RC_LEN,
						     &cmd_length, 4);
		if (rc)
			return rc;
	}

	/* send command */
	cmd = 0x80 | rc_cmd;
	rc = synapticsmst_common_write_dpcd (connection, REG_RC_CMD, &cmd, 1);
	if (rc)
		return rc;

	/* wait command complete */
	clock_gettime (CLOCK_REALTIME, &t_spec);
	deadline = t_spec.tv_sec + MAX_WAIT_TIME;
	do {
		rc = synapticsmst_common_read_dpcd (connection,
						    REG_RC_CMD,
						    &readData, 2);
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
		rc = synapticsmst_common_read_dpcd (connection,
						    REG_RC_DATA,
						    (gint *)buf, length);
		if (rc)
			return rc;
	}

	return rc;
}

guint8
synapticsmst_common_enable_remote_control (SynapticsMSTConnection *connection)
{
	const gchar *sc = "PRIUS";
	guint8 rc = 0;

	for (gint i = 0; i <= connection->layer; i++) {
		g_autoptr(SynapticsMSTConnection) connection_tmp = NULL;
		connection_tmp = synapticsmst_common_new (connection->fd, i, connection->rad);
		rc = synapticsmst_common_rc_set_command (connection_tmp,
							 UPDC_ENABLE_RC,
							 5, 0, (guint8*)sc);
		/* if we fail, try to disable and enable one more time */
		if (rc) {
			g_debug ("Failed to enable remote control, retrying");
			synapticsmst_common_disable_remote_control (connection_tmp);
			rc = synapticsmst_common_rc_set_command (connection_tmp,
								 UPDC_ENABLE_RC,
								 5, 0, (guint8*)sc);
			if (rc)
				break;
		}
	}
	return rc;
}

guint8
synapticsmst_common_disable_remote_control (SynapticsMSTConnection *connection)
{
	guint8 rc = 0;

	for (gint i = connection->layer; i >= 0; i--) {
		g_autoptr(SynapticsMSTConnection) connection_tmp = NULL;
		connection_tmp = synapticsmst_common_new (connection->fd, i, connection->rad);
		rc = synapticsmst_common_rc_set_command (connection_tmp,
							 UPDC_DISABLE_RC,
							 0, 0, NULL);
		if (rc)
			break;
	}
	return rc;
}
