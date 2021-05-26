/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-kinetic-mst-connection.h"
#include "fu-kinetic-mst-common.h"

#define UNIT_SIZE       32
#define MAX_WAIT_TIME   3  /* unit : second */

struct _FuKineticMstConnection {
    GObject     parent_instance;
    gint        fd;        /* not owned by the connection */
    //guint8      layer;
    //guint8      remain_layer;
    //guint8      rad;
};

G_DEFINE_TYPE (FuKineticMstConnection, fu_kinetic_mst_connection, G_TYPE_OBJECT)

static void
fu_kinetic_mst_connection_init (FuKineticMstConnection *self)
{
}

static void
fu_kinetic_mst_connection_class_init (FuKineticMstConnectionClass *klass)
{
}

static gboolean
fu_kinetic_mst_connection_aux_node_read (FuKineticMstConnection *self,
                                         guint32 offset, guint8 *buf,
                                         gint length, GError **error)
{
	if (lseek(self->fd, offset, SEEK_SET) != offset)
	{
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "failed to lseek to 0x%x",
                    offset);
		return FALSE;
	}

	if (read(self->fd, buf, length) != length)
	{
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "failed to read 0x%x bytes",
                    (guint) length);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_kinetic_mst_connection_aux_node_write (FuKineticMstConnection *self,
                                          guint32 offset, const guint8 *buf,
                                          gint length, GError **error)
{
	if (lseek (self->fd, offset, SEEK_SET) != offset)
	{
		g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "failed to lseek to 0x%x",
                    offset);
		return FALSE;
	}

	if (write (self->fd, buf, length) != length)
	{
		g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "failed to write 0x%x bytes",
                    (guint) length);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_kinetic_mst_connection_bus_read(FuKineticMstConnection *self,
               				       guint32 offset,
               				       guint8 *buf,
               				       guint32 length,
               				       GError **error)
{
	return fu_kinetic_mst_connection_aux_node_read(self, offset, buf, length, error);
}

static gboolean
fu_kinetic_mst_connection_bus_write(FuKineticMstConnection *self,
                                    guint32 offset,
                                    const guint8 *buf,
                                    guint32 length,
                                    GError **error)
{
	return fu_kinetic_mst_connection_aux_node_write(self, offset, buf, length, error);
}

FuKineticMstConnection *
fu_kinetic_mst_connection_new(gint fd)
{
	FuKineticMstConnection *self = g_object_new(FU_TYPE_KINETIC_MST_CONNECTION, NULL);

	self->fd = fd;
#if 0
	self->layer = layer;
	self->remain_layer = layer;
	self->rad = rad;
#endif

	return self;
}

gboolean
fu_kinetic_mst_connection_read(FuKineticMstConnection *self,
             				   guint32 offset,
             				   guint8 *buf,
             				   guint32 length,
             				   GError **error)
{
#if 0
	if (self->layer && self->remain_layer) {
		guint8 node;
		gboolean result;

		self->remain_layer--;
		node = (self->rad >> self->remain_layer * 2) & 0x03;
		result = fu_kinetic_mst_connection_rc_get_command(self,
                                                          UPDC_READ_FROM_TX_DPCD + node,
                                                          length, offset, (guint8 *)buf,
                                                          error);
		self->remain_layer++;
		return result;
	}
#endif

	return fu_kinetic_mst_connection_bus_read(self, offset, buf, length, error);
}

gboolean
fu_kinetic_mst_connection_write(FuKineticMstConnection *self,
            				    guint32 offset,
            				    const guint8 *buf,
            				    guint32 length,
            				    GError **error)
{
#if 0
	if (self->layer && self->remain_layer) {
		guint8 node;
		gboolean result;

		self->remain_layer--;
		node = (self->rad >> self->remain_layer * 2) & 0x03;
		result =  fu_kinetic_mst_connection_rc_set_command (self,
								      UPDC_WRITE_TO_TX_DPCD + node,
								      length, offset, (guint8 *)buf,
								      error);
		self->remain_layer++;
		return result;
	}
#endif

	return fu_kinetic_mst_connection_bus_write(self, offset, buf, length, error);
}

