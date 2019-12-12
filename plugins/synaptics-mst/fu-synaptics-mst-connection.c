/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-mst-connection.h"
#include "fu-synaptics-mst-common.h"

#define UNIT_SIZE		32
#define MAX_WAIT_TIME 		3  /* unit : second */

struct _FuSynapticsMstConnection {
	GObject		 parent_instance;
	gint		 fd;		/* not owned by the connection */
	guint8		 layer;
	guint8		 remain_layer;
	guint8		 rad;
};

G_DEFINE_TYPE (FuSynapticsMstConnection, fu_synaptics_mst_connection, G_TYPE_OBJECT)

static void
fu_synaptics_mst_connection_init (FuSynapticsMstConnection *self)
{
}

static void
fu_synaptics_mst_connection_class_init (FuSynapticsMstConnectionClass *klass)
{
}

static gboolean
fu_synaptics_mst_connection_aux_node_read (FuSynapticsMstConnection *self,
					  guint32 offset, guint8 *buf,
					  gint length, GError **error)
{
	if (lseek (self->fd, offset, SEEK_SET) != offset) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to lseek to 0x%x on layer:%u, rad:0x%x",
			     offset, self->layer, self->rad);
		return FALSE;
	}

	if (read (self->fd, buf, length) != length) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to read 0x%x bytes on layer:%u, rad:0x%x",
			     (guint) length, self->layer, self->rad);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_synaptics_mst_connection_aux_node_write (FuSynapticsMstConnection *self,
					    guint32 offset, const guint8 *buf,
					    gint length, GError **error)
{
	if (lseek (self->fd, offset, SEEK_SET) != offset) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to lseek to 0x%x on layer:%u, rad:0x%x",
			     offset, self->layer, self->rad);
		return FALSE;
	}

	if (write (self->fd, buf, length) != length) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to write 0x%x bytes on layer:%u, rad:0x%x",
			     (guint) length, self->layer, self->rad);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_synaptics_mst_connection_bus_read (FuSynapticsMstConnection *self,
				      guint32 offset,
				      guint8 *buf,
				      guint32 length, GError **error)
{
	return fu_synaptics_mst_connection_aux_node_read (self, offset, buf,
							 length, error);
}

static gboolean
fu_synaptics_mst_connection_bus_write (FuSynapticsMstConnection *self,
				       guint32 offset,
				       const guint8 *buf,
				       guint32 length, GError **error)
{
	return fu_synaptics_mst_connection_aux_node_write (self, offset, buf,
							  length, error);
}

FuSynapticsMstConnection *
fu_synaptics_mst_connection_new (gint fd, guint8 layer, guint rad)
{
	FuSynapticsMstConnection *self = g_object_new (FU_TYPE_SYNAPTICS_MST_CONNECTION, NULL);
	self->fd = fd;
	self->layer = layer;
	self->remain_layer = layer;
	self->rad = rad;
	return self;
}

gboolean
fu_synaptics_mst_connection_read (FuSynapticsMstConnection *self,
				  guint32 offset, guint8 *buf,
				  guint32 length, GError **error)
{
	if (self->layer && self->remain_layer) {
		guint8 node;
		gboolean result;

		self->remain_layer--;
		node = (self->rad >> self->remain_layer * 2) & 0x03;
		result =  fu_synaptics_mst_connection_rc_get_command (self,
								      UPDC_READ_FROM_TX_DPCD + node,
								      length, offset, (guint8 *)buf,
								      error);
		self->remain_layer++;
		return result;
	}

	return fu_synaptics_mst_connection_bus_read (self, offset, buf, length, error);
}

gboolean
fu_synaptics_mst_connection_write (FuSynapticsMstConnection *self,
				  guint32 offset,
				  const guint8 *buf,
				  guint32 length, GError **error)
{
	if (self->layer && self->remain_layer) {
		guint8 node;
		gboolean result;

		self->remain_layer--;
		node = (self->rad >> self->remain_layer * 2) & 0x03;
		result =  fu_synaptics_mst_connection_rc_set_command (self,
								      UPDC_WRITE_TO_TX_DPCD + node,
								      length, offset, (guint8 *)buf,
								      error);
		self->remain_layer++;
		return result;
	}

	return fu_synaptics_mst_connection_bus_write (self, offset, buf, length, error);
}

gboolean
fu_synaptics_mst_connection_rc_set_command (FuSynapticsMstConnection *self,
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
			if (!fu_synaptics_mst_connection_write (self,
							        REG_RC_DATA,
							        buf, cur_length,
							        error)) {
				g_prefix_error (error, "failure writing data register: ");
				return FALSE;
			}

			/* write offset */
			if (!fu_synaptics_mst_connection_write (self,
							        REG_RC_OFFSET,
							        (guint8 *)&cur_offset, 4,
							        error)) {
				g_prefix_error (error, "failure writing offset register: ");
				return FALSE;
			}

			/* write length */
			if (!fu_synaptics_mst_connection_write (self,
							        REG_RC_LEN,
							        (guint8 *)&cur_length, 4,
							        error)) {
				g_prefix_error (error, "failure writing length register: ");
				return FALSE;
			}
		}

		/* send command */
		cmd = 0x80 | rc_cmd;
		if (!fu_synaptics_mst_connection_write (self,
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
			if (!fu_synaptics_mst_connection_read (self,
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
fu_synaptics_mst_connection_rc_get_command (FuSynapticsMstConnection *self,
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
			if (!fu_synaptics_mst_connection_write (self,
							        REG_RC_OFFSET,
							        (guint8 *)&cur_offset, 4,
							        error)) {
				g_prefix_error (error, "failed to write offset: ");
				return FALSE;
			}

			/* write length */
			if (!fu_synaptics_mst_connection_write (self,
							        REG_RC_LEN,
							        (guint8 *)&cur_length, 4,
							        error)) {
				g_prefix_error (error, "failed to write length: ");
				return FALSE;
			}
		}

		/* send command */
		cmd = 0x80 | rc_cmd;
		if (!fu_synaptics_mst_connection_write (self,
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
			if (!fu_synaptics_mst_connection_read (self,
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
			if (!fu_synaptics_mst_connection_read (self,
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
fu_synaptics_mst_connection_rc_special_get_command (FuSynapticsMstConnection *self,
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
			if (!fu_synaptics_mst_connection_write (self,
							        REG_RC_DATA,
							        cmd_data,
							        cmd_length,
							        error)) {
				g_prefix_error (error, "Failed to write command data: ");
				return FALSE;
			}
		}

		/* write offset */
		if (!fu_synaptics_mst_connection_write (self,
						        REG_RC_OFFSET,
						        (guint8 *)&cmd_offset, 4,
						        error)) {
			g_prefix_error (error, "failed to write offset: ");
			return FALSE;
		}

		/* write length */
		if (!fu_synaptics_mst_connection_write (self,
						        REG_RC_LEN,
						        (guint8 *)&cmd_length, 4,
						        error)) {
			g_prefix_error (error, "failed to write length: ");
			return FALSE;
		}
	}

	/* send command */
	cmd = 0x80 | rc_cmd;
	if (!fu_synaptics_mst_connection_write (self,
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
		if (!fu_synaptics_mst_connection_read (self,
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
		if (!fu_synaptics_mst_connection_read (self,
						       REG_RC_DATA,
						       buf, length,
						       error)) {
			g_prefix_error (error, "failed to read length: ");
		}
	}

	return TRUE;
}

gboolean
fu_synaptics_mst_connection_enable_rc (FuSynapticsMstConnection *self, GError **error)
{
	const gchar *sc = "PRIUS";

	for (gint i = 0; i <= self->layer; i++) {
		g_autoptr(FuSynapticsMstConnection) connection_tmp = NULL;
		connection_tmp = fu_synaptics_mst_connection_new (self->fd, i, self->rad);
		if (!fu_synaptics_mst_connection_rc_set_command (connection_tmp,
								 UPDC_ENABLE_RC,
								 5, 0, (guint8*)sc,
								 error)) {
			g_prefix_error (error, "failed to enable remote control: ");
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
fu_synaptics_mst_connection_disable_rc (FuSynapticsMstConnection *self, GError **error)
{
	for (gint i = self->layer; i >= 0; i--) {
		g_autoptr(FuSynapticsMstConnection) connection_tmp = NULL;
		connection_tmp = fu_synaptics_mst_connection_new (self->fd, i, self->rad);
		if (!fu_synaptics_mst_connection_rc_set_command (connection_tmp,
								 UPDC_DISABLE_RC,
								 0, 0, NULL,
								 error)) {
			g_prefix_error (error, "failed to disable remote control: ");
			return FALSE;
		}
	}

	return TRUE;
}
