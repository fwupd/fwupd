/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Peichen Huang <peichenhuang@tw.synaptics.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 * Copyright (C) 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-kinetic-dp-common.h"
#include "fu-kinetic-dp-connection.h"

#define UNIT_SIZE     32
#define MAX_WAIT_TIME 3 /* unit : second */

struct _FuKineticDpConnection {
	GObject parent_instance;
	gint fd; /* not owned by the connection */
};

G_DEFINE_TYPE(FuKineticDpConnection, fu_kinetic_dp_connection, G_TYPE_OBJECT)

static void
fu_kinetic_dp_connection_init(FuKineticDpConnection *self)
{
}

static void
fu_kinetic_dp_connection_class_init(FuKineticDpConnectionClass *klass)
{
}

FuKineticDpConnection *
fu_kinetic_dp_connection_new(gint fd)
{
	FuKineticDpConnection *self = g_object_new(FU_TYPE_KINETIC_DP_CONNECTION, NULL);
	self->fd = fd;
	return self;
}

gboolean
fu_kinetic_dp_connection_read(FuKineticDpConnection *self,
			      guint32 offset,
			      guint8 *buf,
<<<<<<< HEAD
			      gssize length,
=======
			      guint32 length,
>>>>>>> kinetic-dp: Add a plugin to update Kinetic's DisplayPort converter
			      GError **error)
{
	g_return_val_if_fail(self != NULL, FALSE);

	if (lseek(self->fd, offset, SEEK_SET) != offset) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "Failed to lseek to 0x%x",
			    offset);
		return FALSE;
	}

	if (read(self->fd, buf, length) != length) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "Failed to read 0x%x bytes",
			    (guint)length);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_kinetic_dp_connection_write(FuKineticDpConnection *self,
			       guint32 offset,
			       const guint8 *buf,
<<<<<<< HEAD
			       gssize length,
			       GError **error)
{
	gssize bytes_wrote;
=======
			       guint32 length,
			       GError **error)
{
	guint32 bytes_wrote;
>>>>>>> kinetic-dp: Add a plugin to update Kinetic's DisplayPort converter
	g_return_val_if_fail(self != NULL, FALSE);

	if (lseek(self->fd, offset, SEEK_SET) != offset) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "Failed to lseek to 0x%x",
			    offset);
		return FALSE;
	}
	bytes_wrote = write(self->fd, buf, length);
	if (bytes_wrote != length) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "Failed to write %d bytes, only wrote %d bytes",
			    (gint32)length,
			    (gint32)bytes_wrote);
		return FALSE;
	}
	return TRUE;
}
