/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>
#include <string.h>
#include <sys/errno.h>

#include <glib/gstdio.h>

#include "fu-superio-device.h"

#define FU_PLUGIN_SUPERIO_TIMEOUT	5 /* s */

#define KB_IBF			(1 << 1)  /* i/p buffer full */
#define KB_OBF			(1 << 0)  /* o/p buffer full */

struct _FuSuperioDevice {
	FuDevice		 parent_instance;
	gint			 fd;
	gchar			*chipset;
	guint16			 data_port;
	guint16			 cmd_port;
	guint16			 id;
	guint32			 size;
};

G_DEFINE_TYPE (FuSuperioDevice, fu_superio_device, FU_TYPE_DEVICE)

static void
fu_superio_device_to_string (FuDevice *device, GString *str)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	g_string_append (str, "  FuSuperioDevice:\n");
	g_string_append_printf (str, "    fd:\t\t\t%i\n", self->fd);
	g_string_append_printf (str, "    chipset:\t\t%s\n", self->chipset);
	g_string_append_printf (str, "    data-port:\t\t0x%04x\n", (guint) self->data_port);
	g_string_append_printf (str, "    cmd-port:\t\t0x%04x\n", (guint) self->cmd_port);
	g_string_append_printf (str, "    id:\t\t\t0x%04x\n", (guint) self->id);
	g_string_append_printf (str, "    size:\t\t0x%04x\n", (guint) self->size);
}

static gboolean
fu_superio_device_outb (FuSuperioDevice *self, guint16 port, guint8 data, GError **error)
{
	if (pwrite (self->fd, &data, 1, (goffset) port) != 1) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write to port %04x: %s",
			     (guint) port,
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_superio_device_inb (FuSuperioDevice *self, guint16 port, guint8 *data, GError **error)
{
	if (pread (self->fd, data, 1, (goffset) port) != 1) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to read from port %04x: %s",
			     (guint) port,
			     strerror (errno));
		return FALSE;
	}
	return TRUE;
}

static guint16
fu_superio_device_check_id (FuSuperioDevice *self, GError **error)
{
	guint8 msb;
	guint8 lsb;
	guint16 id_tmp;

	if (!fu_superio_device_outb (self, 0x2e, 0x20, error))
		return FALSE;
	if (!fu_superio_device_inb (self, 0x2f, &msb, error))
		return FALSE;
	if (!fu_superio_device_outb (self, 0x2e, 0x21, error))
		return FALSE;
	if (!fu_superio_device_inb (self, 0x2f, &lsb, error))
		return FALSE;

	/* check matches */
	id_tmp = ((guint16) msb << 8) | (guint16) lsb;
	if (self->id != id_tmp) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "SuperIO chip not supported, got %04x, expected %04x",
			     (guint) id_tmp, (guint) self->id);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_superio_device_wait_for (FuSuperioDevice *self, guint8 mask, gboolean set, GError **error)
{
	g_autoptr(GTimer) timer = g_timer_new ();
	do {
		guint8 status = 0x00;
		if (!fu_superio_device_inb (self, self->cmd_port, &status, error))
			return FALSE;
		if (g_timer_elapsed (timer, NULL) > FU_PLUGIN_SUPERIO_TIMEOUT)
			break;
		if (set && (status & mask) > 0)
			return TRUE;
		if (!set && (status & mask) == 0)
			return TRUE;
	} while (TRUE);
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_TIMED_OUT,
		     "timed out whilst waiting for 0x%02x:%i", mask, set);
	return FALSE;
}

static gboolean
fu_superio_device_ec_cmd (FuSuperioDevice *self, guint8 data, GError **error)
{
	if (!fu_superio_device_wait_for (self, KB_IBF, FALSE, error))
		return FALSE;
	return fu_superio_device_outb (self, self->cmd_port, data, error);
}

static gboolean
fu_superio_device_ec_read (FuSuperioDevice *self, guint8 *data, GError **error)
{
	if (!fu_superio_device_wait_for (self, KB_OBF, TRUE, error))
		return FALSE;
	return fu_superio_device_inb (self, self->data_port, data, error);
}

static gboolean
fu_superio_device_ec_write (FuSuperioDevice *self, guint8 data, GError **error)
{
	if (!fu_superio_device_wait_for (self, KB_IBF, FALSE, error))
		return FALSE;
	return fu_superio_device_outb (self, self->data_port, data, error);
}

static gboolean
fu_superio_device_ec_flush (FuSuperioDevice *self, GError **error)
{
	guint8 status = 0x00;
	g_autoptr(GTimer) timer = g_timer_new ();
	do {
		guint8 unused = 0;
		if (!fu_superio_device_inb (self, self->cmd_port, &status, error))
			return FALSE;
		if ((status & KB_OBF) == 0)
			break;
		if (!fu_superio_device_inb (self, self->data_port, &unused, error))
			return FALSE;
		if (g_timer_elapsed (timer, NULL) > FU_PLUGIN_SUPERIO_TIMEOUT) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_TIMED_OUT,
					     "timed out whilst waiting for flush");
			return FALSE;
		}
	} while (TRUE);
	return TRUE;
}

static gboolean
fu_superio_device_ec_get_param (FuSuperioDevice *self, guint8 param, guint8 *data, GError **error)
{
	if (!fu_superio_device_ec_cmd (self, 0x80, error))
		return FALSE;
	if (!fu_superio_device_ec_write (self, param, error))
		return FALSE;
	return fu_superio_device_ec_read (self, data, error);
}

#if 0
static gboolean
fu_superio_device_ec_set_param (FuSuperioDevice *self, guint8 param, guint8 data, GError **error)
{
	if (!fu_superio_device_ec_cmd (self, 0x81, error))
		return FALSE;
	if (!fu_superio_device_ec_write (self, param, error))
		return FALSE;
	return fu_superio_device_ec_write (self, data, error);
}
#endif

static gchar *
fu_superio_device_ec_get_str (FuSuperioDevice *self, guint8 idx, GError **error)
{
	GString *str = g_string_new (NULL);
	if (!fu_superio_device_ec_cmd (self, idx, error))
		return NULL;
	for (guint i = 0; i < 0xff; i++) {
		guint8 c = 0;
		if (!fu_superio_device_ec_read (self, &c, error))
			return NULL;
		if (c == '$')
			break;
		g_string_append_c (str, c);
	}
	return g_string_free (str, FALSE);
}

static gboolean
fu_superio_device_open (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);

	/* open device */
	self->fd = g_open (fu_device_get_physical_id (device), O_RDWR);
	if (self->fd < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to open %s: %s",
			     fu_device_get_physical_id (device),
			     strerror (errno));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_superio_device_probe (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	g_autofree gchar *guid = NULL;

	/* use the chipset name as the logical ID and for the GUID */
	fu_device_set_logical_id (device, self->chipset);
	guid = g_strdup_printf ("SuperIO-%s", self->chipset);
	fu_device_add_guid (device, guid);
	return TRUE;
}

static gboolean
fu_superio_device_setup (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	guint8 size_tmp = 0;
	g_autofree gchar *name = NULL;
	g_autofree gchar *version = NULL;

	/* check ID is correct */
	if (!fu_superio_device_check_id (self, error)) {
		g_prefix_error (error, "failed to probe id: ");
		return FALSE;
	}

	/* get EC size */
	if (!fu_superio_device_ec_flush (self, error))
		return FALSE;
	if (!fu_superio_device_ec_get_param (self, 0xe5, &size_tmp, error))
		return FALSE;
	self->size = ((guint32) size_tmp) << 10;

	/* get EC strings */
	name = fu_superio_device_ec_get_str (self, 0x92, error);
	if (name == NULL)
		return FALSE;
	fu_device_set_name (device, name);
	version = fu_superio_device_ec_get_str (self, 0x93, error);
	if (version == NULL)
		return FALSE;
	fu_device_set_version (device, version);

	/* success */
	return TRUE;
}

static gboolean
fu_superio_device_close (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	if (!g_close (self->fd, error))
		return FALSE;
	self->fd = 0;
	return TRUE;
}

static void
fu_superio_device_init (FuSuperioDevice *self)
{
	fu_device_set_physical_id (FU_DEVICE (self), "/dev/port");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_summary (FU_DEVICE (self), "SuperIO device");
	fu_device_add_icon (FU_DEVICE (self), "computer");
}

static void
fu_superio_device_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_superio_device_parent_class)->finalize (object);
}

static void
fu_superio_device_class_init (FuSuperioDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_superio_device_finalize;
	klass_device->to_string = fu_superio_device_to_string;
	klass_device->open = fu_superio_device_open;
	klass_device->probe = fu_superio_device_probe;
	klass_device->setup = fu_superio_device_setup;
	klass_device->close = fu_superio_device_close;
}

FuSuperioDevice *
fu_superio_device_new (const gchar *chipset, guint16 id, guint8 data_port, guint8 cmd_port)
{
	FuSuperioDevice *self;
	self = g_object_new (FU_TYPE_SUPERIO_DEVICE, NULL);
	self->chipset = g_strdup (chipset);
	self->id = id;
	self->data_port = data_port > 0 ? data_port : 0x62;
	self->cmd_port = cmd_port > 0 ? cmd_port : 0x66;
	return self;
}
