/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-synaptics-mst-common.h"
#include "fu-synaptics-mst-connection.h"

#define UNIT_SIZE     32
#define MAX_WAIT_TIME 3 /* unit : second */

struct _FuSynapticsMstConnection {
	GObject parent_instance;
	gint fd; /* not owned by the connection */
	guint8 layer;
	guint8 remain_layer;
	guint8 rad;
};

G_DEFINE_TYPE(FuSynapticsMstConnection, fu_synaptics_mst_connection, G_TYPE_OBJECT)

static void
fu_synaptics_mst_connection_init(FuSynapticsMstConnection *self)
{
}

static void
fu_synaptics_mst_connection_class_init(FuSynapticsMstConnectionClass *klass)
{
}

static gboolean
fu_synaptics_mst_connection_aux_node_read(FuSynapticsMstConnection *self,
					  guint32 offset,
					  guint8 *buf,
					  gsize bufsz,
					  GError **error)
{
	g_autofree gchar *title = g_strdup_printf("read@0x%x", offset);
	if (lseek(self->fd, offset, SEEK_SET) != offset) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "failed to lseek to 0x%x on layer:%u, rad:0x%x",
			    offset,
			    self->layer,
			    self->rad);
		return FALSE;
	}

	if (read(self->fd, buf, bufsz) != (gssize)bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "failed to read 0x%x bytes on layer:%u, rad:0x%x",
			    (guint)bufsz,
			    self->layer,
			    self->rad);
		return FALSE;
	}
	fu_dump_raw(G_LOG_DOMAIN, title, buf, bufsz);

	return TRUE;
}

static gboolean
fu_synaptics_mst_connection_aux_node_write(FuSynapticsMstConnection *self,
					   guint32 offset,
					   const guint8 *buf,
					   gsize bufsz,
					   GError **error)
{
	g_autofree gchar *title = g_strdup_printf("write@0x%x", offset);
	fu_dump_raw(G_LOG_DOMAIN, title, buf, bufsz);
	if (lseek(self->fd, offset, SEEK_SET) != offset) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "failed to lseek to 0x%x on layer:%u, rad:0x%x",
			    offset,
			    self->layer,
			    self->rad);
		return FALSE;
	}

	if (write(self->fd, buf, bufsz) != (gssize)bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "failed to write 0x%x bytes on layer:%u, rad:0x%x",
			    (guint)bufsz,
			    self->layer,
			    self->rad);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_synaptics_mst_connection_bus_read(FuSynapticsMstConnection *self,
				     guint32 offset,
				     guint8 *buf,
				     gsize bufsz,
				     GError **error)
{
	return fu_synaptics_mst_connection_aux_node_read(self, offset, buf, bufsz, error);
}

static gboolean
fu_synaptics_mst_connection_bus_write(FuSynapticsMstConnection *self,
				      guint32 offset,
				      const guint8 *buf,
				      gsize bufsz,
				      GError **error)
{
	return fu_synaptics_mst_connection_aux_node_write(self, offset, buf, bufsz, error);
}

FuSynapticsMstConnection *
fu_synaptics_mst_connection_new(gint fd, guint8 layer, guint rad)
{
	FuSynapticsMstConnection *self = g_object_new(FU_TYPE_SYNAPTICS_MST_CONNECTION, NULL);
	self->fd = fd;
	self->layer = layer;
	self->remain_layer = layer;
	self->rad = rad;
	return self;
}

gboolean
fu_synaptics_mst_connection_read(FuSynapticsMstConnection *self,
				 guint32 offset,
				 guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
	if (self->layer && self->remain_layer) {
		guint8 node;
		gboolean result;

		self->remain_layer--;
		node = (self->rad >> self->remain_layer * 2) & 0x03;
		result = fu_synaptics_mst_connection_rc_get_command(self,
								    UPDC_READ_FROM_TX_DPCD + node,
								    offset,
								    (guint8 *)buf,
								    bufsz,
								    error);
		self->remain_layer++;
		return result;
	}

	return fu_synaptics_mst_connection_bus_read(self, offset, buf, bufsz, error);
}

static gboolean
fu_synaptics_mst_connection_write(FuSynapticsMstConnection *self,
				  guint32 offset,
				  const guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	if (self->layer && self->remain_layer) {
		guint8 node;
		gboolean result;

		self->remain_layer--;
		node = (self->rad >> self->remain_layer * 2) & 0x03;
		result = fu_synaptics_mst_connection_rc_set_command(self,
								    UPDC_WRITE_TO_TX_DPCD + node,
								    offset,
								    (guint8 *)buf,
								    bufsz,
								    error);
		self->remain_layer++;
		return result;
	}

	return fu_synaptics_mst_connection_bus_write(self, offset, buf, bufsz, error);
}

static gboolean
fu_synaptics_mst_connection_rc_send_command_and_wait(FuSynapticsMstConnection *self,
						     guint32 rc_cmd,
						     GError **error)
{
	guint32 cmd = 0x80 | rc_cmd;
	guint16 buf = 0;
	g_autoptr(GTimer) timer = g_timer_new();

	if (!fu_synaptics_mst_connection_write(self, REG_RC_CMD, (guint8 *)&cmd, 1, error)) {
		g_prefix_error(error, "failed to write command: ");
		return FALSE;
	}

	/* wait command complete */
	do {
		if (!fu_synaptics_mst_connection_read(self,
						      REG_RC_CMD,
						      (guint8 *)&buf,
						      sizeof(buf),
						      error)) {
			g_prefix_error(error, "failed to read command: ");
			return FALSE;
		}
		if (g_timer_elapsed(timer, NULL) > MAX_WAIT_TIME) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "timeout exceeded");
			return FALSE;
		}
	} while (buf & 0x80);

	if (buf & 0xFF00) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "remote command failed: %u",
			    (guint)(buf >> 8) & 0xFF);

		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_synaptics_mst_connection_rc_set_command(FuSynapticsMstConnection *self,
					   guint32 rc_cmd,
					   guint32 offset,
					   const guint8 *buf,
					   gsize bufsz,
					   GError **error)
{
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new(buf, bufsz, offset, 0x0, UNIT_SIZE);

	/* just sent command */
	if (chunks->len == 0) {
		g_debug("no data, just sending command 0x%x", rc_cmd);
		return fu_synaptics_mst_connection_rc_send_command_and_wait(self, rc_cmd, error);
	}

	/* read each chunk */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint32 cur_length = fu_chunk_get_data_sz(chk);
		guint32 cur_offset = fu_chunk_get_address(chk);

		g_debug("writing chunk of 0x%x bytes at offset 0x%x", cur_length, cur_offset);

		/* write data */
		if (!fu_synaptics_mst_connection_write(self,
						       REG_RC_DATA,
						       fu_chunk_get_data(chk),
						       fu_chunk_get_data_sz(chk),
						       error)) {
			g_prefix_error(error, "failure writing data register: ");
			return FALSE;
		}

		/* write offset */
		if (!fu_synaptics_mst_connection_write(self,
						       REG_RC_OFFSET,
						       (guint8 *)&cur_offset,
						       sizeof(cur_offset),
						       error)) {
			g_prefix_error(error, "failure writing offset register: ");
			return FALSE;
		}

		/* write length */
		if (!fu_synaptics_mst_connection_write(self,
						       REG_RC_LEN,
						       (guint8 *)&cur_length,
						       sizeof(cur_length),
						       error)) {
			g_prefix_error(error, "failure writing length register: ");
			return FALSE;
		}

		/* send command */
		g_debug("data, sending command 0x%x", rc_cmd);
		if (!fu_synaptics_mst_connection_rc_send_command_and_wait(self, rc_cmd, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_synaptics_mst_connection_rc_get_command(FuSynapticsMstConnection *self,
					   guint32 rc_cmd,
					   guint32 offset,
					   guint8 *buf,
					   gsize bufsz,
					   GError **error)
{
	g_autoptr(GPtrArray) chunks =
	    fu_chunk_array_mutable_new(buf, bufsz, offset, 0x0, UNIT_SIZE);

	/* just sent command */
	if (chunks->len == 0) {
		g_debug("no data, just sending command 0x%x", rc_cmd);
		return fu_synaptics_mst_connection_rc_send_command_and_wait(self, rc_cmd, error);
	}

	/* read each chunk */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint32 cur_length = fu_chunk_get_data_sz(chk);
		guint32 cur_offset = fu_chunk_get_address(chk);

		g_debug("reading chunk of 0x%x bytes at offset 0x%x", cur_length, cur_offset);

		/* write offset */
		if (!fu_synaptics_mst_connection_write(self,
						       REG_RC_OFFSET,
						       (guint8 *)&cur_offset,
						       sizeof(cur_offset),
						       error)) {
			g_prefix_error(error, "failed to write offset: ");
			return FALSE;
		}

		/* write length */
		if (!fu_synaptics_mst_connection_write(self,
						       REG_RC_LEN,
						       (guint8 *)&cur_length,
						       sizeof(cur_length),
						       error)) {
			g_prefix_error(error, "failed to write length: ");
			return FALSE;
		}

		/* send command */
		g_debug("data, sending command 0x%x", rc_cmd);
		if (!fu_synaptics_mst_connection_rc_send_command_and_wait(self, rc_cmd, error))
			return FALSE;

		/* read data */
		if (!fu_synaptics_mst_connection_read(self,
						      REG_RC_DATA,
						      fu_chunk_get_data_out(chk),
						      fu_chunk_get_data_sz(chk),
						      error)) {
			g_prefix_error(error, "failed to read data: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_synaptics_mst_connection_rc_special_get_command(FuSynapticsMstConnection *self,
						   guint32 rc_cmd,
						   guint32 cmd_offset,
						   guint8 *cmd_data,
						   gsize cmd_datasz,
						   guint8 *buf,
						   gsize bufsz,
						   GError **error)
{
	if (cmd_datasz > 0) {
		guint32 cmd_datasz32 = cmd_datasz;

		/* write cmd data */
		if (cmd_data != NULL) {
			if (!fu_synaptics_mst_connection_write(self,
							       REG_RC_DATA,
							       cmd_data,
							       cmd_datasz,
							       error)) {
				g_prefix_error(error, "Failed to write command data: ");
				return FALSE;
			}
		}

		/* write offset */
		if (!fu_synaptics_mst_connection_write(self,
						       REG_RC_OFFSET,
						       (guint8 *)&cmd_offset,
						       sizeof(cmd_offset),
						       error)) {
			g_prefix_error(error, "failed to write offset: ");
			return FALSE;
		}

		/* write length */
		if (!fu_synaptics_mst_connection_write(self,
						       REG_RC_LEN,
						       (guint8 *)&cmd_datasz32,
						       sizeof(cmd_datasz32),
						       error)) {
			g_prefix_error(error, "failed to write length: ");
			return FALSE;
		}
	}

	/* send command */
	g_debug("sending command 0x%x", rc_cmd);
	if (!fu_synaptics_mst_connection_rc_send_command_and_wait(self, rc_cmd, error))
		return FALSE;

	if (bufsz > 0) {
		if (!fu_synaptics_mst_connection_read(self, REG_RC_DATA, buf, bufsz, error)) {
			g_prefix_error(error, "failed to read length: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_synaptics_mst_connection_enable_rc(FuSynapticsMstConnection *self, GError **error)
{
	const gchar *sc = "PRIUS";

	for (gint i = 0; i <= self->layer; i++) {
		g_autoptr(FuSynapticsMstConnection) connection_tmp = NULL;
		connection_tmp = fu_synaptics_mst_connection_new(self->fd, i, self->rad);
		if (!fu_synaptics_mst_connection_rc_set_command(connection_tmp,
								UPDC_ENABLE_RC,
								0,
								(guint8 *)sc,
								5,
								error)) {
			g_prefix_error(error, "failed to enable remote control: ");
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
fu_synaptics_mst_connection_disable_rc(FuSynapticsMstConnection *self, GError **error)
{
	for (gint i = self->layer; i >= 0; i--) {
		g_autoptr(FuSynapticsMstConnection) connection_tmp = NULL;
		connection_tmp = fu_synaptics_mst_connection_new(self->fd, i, self->rad);
		if (!fu_synaptics_mst_connection_rc_set_command(connection_tmp,
								UPDC_DISABLE_RC,
								0,
								NULL,
								0,
								error)) {
			g_prefix_error(error, "failed to disable remote control: ");
			return FALSE;
		}
	}

	return TRUE;
}
