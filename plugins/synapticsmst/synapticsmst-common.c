/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "synapticsmst-common.h"

#define UNIT_SIZE		32
#define MAX_WAIT_TIME 		3  /* unit : second */

struct _SynapticsMSTConnection {
	gint		 fd;		/* not owned by the connection */
	guint8		 layer;
	guint8		 remain_layer;
	guint8		 rad;
};

static gboolean
synapticsmst_common_aux_node_read (SynapticsMSTConnection *connection,
				   guint32 offset, guint8 *buf,
				   gint length, GError **error)
{
	if (lseek (connection->fd, offset, SEEK_SET) != offset) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "failed to lseek");
		return FALSE;
	}

	if (read (connection->fd, buf, length) != length) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "failed to read");
		return FALSE;
	}

	return TRUE;
}

static gboolean
synapticsmst_common_aux_node_write (SynapticsMSTConnection *connection,
				    guint32 offset, const guint8 *buf,
				    gint length, GError **error)
{
	if (lseek (connection->fd, offset, SEEK_SET) != offset) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "failed to lseek");
		return FALSE;
	}

	if (write (connection->fd, buf, length) != length) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "failed to write");
		return FALSE;
	}

	return TRUE;
}

static gboolean
synapticsmst_common_bus_read (SynapticsMSTConnection *connection,
			      guint32 offset,
			      guint8 *buf,
			      guint32 length, GError **error)
{
	return synapticsmst_common_aux_node_read (connection, offset, buf,
						  length, error);
}

static gboolean
synapticsmst_common_bus_write (SynapticsMSTConnection *connection,
			       guint32 offset,
			       const guint8 *buf,
			       guint32 length, GError **error)
{
	return synapticsmst_common_aux_node_write (connection, offset, buf,
						   length, error);
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

gboolean
synapticsmst_common_read (SynapticsMSTConnection *connection,
			  guint32 offset, guint8 *buf,
			  guint32 length, GError **error)
{
	if (connection->layer && connection->remain_layer) {
		guint8 node;
		gboolean result;

		connection->remain_layer--;
		node = (connection->rad >> connection->remain_layer * 2) & 0x03;
		result =  synapticsmst_common_rc_get_command (connection,
							      UPDC_READ_FROM_TX_DPCD + node,
							      length, offset, (guint8 *)buf,
							      error);
		connection->remain_layer++;
		return result;
	}

	return synapticsmst_common_bus_read (connection, offset, buf, length, error);
}

gboolean
synapticsmst_common_write (SynapticsMSTConnection *connection,
			   guint32 offset,
			   const guint8 *buf,
			   guint32 length, GError **error)
{
	if (connection->layer && connection->remain_layer) {
		guint8 node;
		gboolean result;

		connection->remain_layer--;
		node = (connection->rad >> connection->remain_layer * 2) & 0x03;
		result =  synapticsmst_common_rc_set_command (connection,
							      UPDC_WRITE_TO_TX_DPCD + node,
							      length, offset, (guint8 *)buf,
							      error);
		connection->remain_layer++;
		return result;
	}

	return synapticsmst_common_bus_write (connection, offset, buf, length, error);
}

gboolean
synapticsmst_common_rc_set_command (SynapticsMSTConnection *connection,
				    guint32 rc_cmd,
				    guint32 length,
				    guint32 offset,
				    const guint8 *buf,
				    GError **error)
{
	guint32 cur_offset = offset;
	guint32 cur_length;
	gint data_left = length;
	gint cmd;
	gint readData = 0;
	long deadline;
	struct timespec t_spec;

	do {
		if (data_left > UNIT_SIZE) {
			cur_length = UNIT_SIZE;
		} else {
			cur_length = data_left;
		}

		if (cur_length) {
			/* write data */
			if (!synapticsmst_common_write (connection,
							REG_RC_DATA,
							buf, cur_length,
							error)) {
				g_prefix_error (error, "failed to write data: ");
				return FALSE;
			}

			/* write offset */
			if (!synapticsmst_common_write (connection,
							REG_RC_OFFSET,
							(guint8 *)&cur_offset, 4,
							error)) {
				g_prefix_error (error, "failed to write offset: ");
				return FALSE;
			}

			/* write length */
			if (!synapticsmst_common_write (connection,
							REG_RC_LEN,
							(guint8 *)&cur_length, 4,
							error)) {
				g_prefix_error (error, "failed to write length: ");
				return FALSE;
			}
		}

		/* send command */
		cmd = 0x80 | rc_cmd;
		if (!synapticsmst_common_write (connection,
						REG_RC_CMD,
						(guint8 *)&cmd, 1,
						error)) {
			g_prefix_error (error, "failed to write command: ");
			return FALSE;
		}

		/* wait command complete */
		clock_gettime (CLOCK_REALTIME, &t_spec);
		deadline = t_spec.tv_sec + MAX_WAIT_TIME;

		do {
			if (!synapticsmst_common_read (connection,
						       REG_RC_CMD,
						       (guint8 *)&readData, 2,
						       error)) {
				g_prefix_error (error, "failed to read command: ");
				return FALSE;
			}
			clock_gettime (CLOCK_REALTIME, &t_spec);
			if (t_spec.tv_sec > deadline) {
				g_set_error_literal (error,
						     G_IO_ERROR,
						     G_IO_ERROR_INVALID_DATA,
						     "timeout exceeded");
				return FALSE;
			}
		} while (readData & 0x80);

		if (readData & 0xFF00) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "remote command failed: %d",
				     (readData >> 8) & 0xFF);

			return FALSE;
		}

		buf += cur_length;
		cur_offset += cur_length;
		data_left -= cur_length;
	} while (data_left);

	return TRUE;
}

gboolean
synapticsmst_common_rc_get_command (SynapticsMSTConnection *connection,
				    guint32 rc_cmd,
				    guint32 length,
				    guint32 offset,
				    guint8 *buf,
				    GError **error)
{
	guint32 cur_offset = offset;
	guint32 cur_length;
	gint data_need = length;
	guint32 cmd;
	guint32 readData = 0;
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
			if (!synapticsmst_common_write (connection,
							REG_RC_OFFSET,
							(guint8 *)&cur_offset, 4,
							error)) {
				g_prefix_error (error, "failed to write offset: ");
				return FALSE;
			}

			/* write length */
			if (!synapticsmst_common_write (connection,
							REG_RC_LEN,
							(guint8 *)&cur_length, 4,
							error)) {
				g_prefix_error (error, "failed to write length: ");
				return FALSE;
			}
		}

		/* send command */
		cmd = 0x80 | rc_cmd;
		if (!synapticsmst_common_write (connection,
						REG_RC_CMD,
						(guint8 *)&cmd, 1,
						error)) {
			g_prefix_error (error, "failed to write command: ");
			return FALSE;
		}

		/* wait command complete */
		clock_gettime (CLOCK_REALTIME, &t_spec);
		deadline = t_spec.tv_sec + MAX_WAIT_TIME;

		do {
			if (!synapticsmst_common_read (connection,
						       REG_RC_CMD,
						       (guint8 *)&readData, 2,
						       error)) {
				g_prefix_error (error, "failed to read command: ");
				return FALSE;
			}
			clock_gettime (CLOCK_REALTIME, &t_spec);
			if (t_spec.tv_sec > deadline) {
				g_set_error_literal (error,
						     G_IO_ERROR,
						     G_IO_ERROR_INVALID_DATA,
						     "timeout exceeded");
				return FALSE;
			}
		} while (readData & 0x80);

		if (readData & 0xFF00) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "remote command failed: %u",
				     (readData >> 8) & 0xFF);

			return FALSE;
		}

		if (cur_length) {
			if (!synapticsmst_common_read (connection,
						       REG_RC_DATA,
						       buf,
						       cur_length,
						       error)) {
				g_prefix_error (error, "failed to read data: ");
				return FALSE;
			}
		}

		buf += cur_length;
		cur_offset += cur_length;
		data_need -= cur_length;
	}

	return TRUE;
}

gboolean
synapticsmst_common_rc_special_get_command (SynapticsMSTConnection *connection,
					    guint32 rc_cmd,
					    guint32 cmd_length,
					    guint32 cmd_offset,
					    guint8 *cmd_data,
					    guint32 length,
					    guint8 *buf,
					    GError **error)
{
	guint32 readData = 0;
	guint32 cmd;
	long deadline;
	struct timespec t_spec;

	if (cmd_length) {
		/* write cmd data */
		if (cmd_data != NULL) {
			if (!synapticsmst_common_write (connection,
							REG_RC_DATA,
							cmd_data,
							cmd_length,
							error)) {
				g_prefix_error (error, "Failed to write command data: ");
				return FALSE;
			}
		}

		/* write offset */
		if (!synapticsmst_common_write (connection,
						REG_RC_OFFSET,
						(guint8 *)&cmd_offset, 4,
						error)) {
			g_prefix_error (error, "failed to write offset: ");
			return FALSE;
		}

		/* write length */
		if (!synapticsmst_common_write (connection,
						REG_RC_LEN,
						(guint8 *)&cmd_length, 4,
						error)) {
			g_prefix_error (error, "failed to write length: ");
			return FALSE;
		}
	}

	/* send command */
	cmd = 0x80 | rc_cmd;
	if (!synapticsmst_common_write (connection,
					REG_RC_CMD,
					(guint8 *)&cmd, 1,
					error)) {
		g_prefix_error (error, "failed to write command: ");
		return FALSE;
	}

	/* wait command complete */
	clock_gettime (CLOCK_REALTIME, &t_spec);
	deadline = t_spec.tv_sec + MAX_WAIT_TIME;
	do {
		if (!synapticsmst_common_read (connection,
						REG_RC_CMD,
						(guint8 *)&readData, 2,
						error)) {
			g_prefix_error (error, "failed to read command: ");
			return FALSE;
		}
		clock_gettime (CLOCK_REALTIME, &t_spec);
		if (t_spec.tv_sec > deadline) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_INVALID_DATA,
					     "timeout exceeded");
			return FALSE;

		}
	} while (readData & 0x80);

	if (readData & 0xFF00) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "remote command failed: %u",
			     (readData >> 8) & 0xFF);

		return FALSE;
	}

	if (length) {
		if (!synapticsmst_common_read (connection,
					       REG_RC_DATA,
					       buf, length,
					       error)) {
			g_prefix_error (error, "failed to read length: ");
		}
	}

	return TRUE;
}

gboolean
synapticsmst_common_enable_remote_control (SynapticsMSTConnection *connection,
					   GError **error)
{
	const gchar *sc = "PRIUS";

	for (gint i = 0; i <= connection->layer; i++) {
		g_autoptr(SynapticsMSTConnection) connection_tmp = synapticsmst_common_new (connection->fd, i, connection->rad);
		g_autoptr(GError) error_local = NULL;
		if (!synapticsmst_common_rc_set_command (connection_tmp,
							 UPDC_ENABLE_RC,
							 5, 0, (guint8*)sc,
							 &error_local)) {
			g_warning ("Failed to enable remote control in layer %d: %s, retrying",
				   i, error_local->message);

			if (!synapticsmst_common_disable_remote_control (connection_tmp, error))
				return FALSE;
			if (!synapticsmst_common_rc_set_command (connection_tmp,
								 UPDC_ENABLE_RC,
								 5, 0, (guint8*)sc,
								 error)) {
				g_prefix_error (error,
						"failed to enable remote control in layer %d: ",
						i);
				return FALSE;
			}
		}
	}

	return TRUE;
}

gboolean
synapticsmst_common_disable_remote_control (SynapticsMSTConnection *connection,
					    GError **error)
{
	for (gint i = connection->layer; i >= 0; i--) {
		g_autoptr(SynapticsMSTConnection) connection_tmp = synapticsmst_common_new (connection->fd, i, connection->rad);
		if (!synapticsmst_common_rc_set_command (connection_tmp,
							 UPDC_DISABLE_RC,
							 0, 0, NULL,
							 error)) {
			g_prefix_error (error,
					"failed to disable remote control in layer %d: ",
					i);
			return FALSE;
		}
	}

	return TRUE;
}
