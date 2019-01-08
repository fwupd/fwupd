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

#include "fu-superio-common.h"
#include "fu-superio-device.h"

#define FU_PLUGIN_SUPERIO_TIMEOUT	0.25 /* s */

#define KB_IBF			(1 << 1)  /* i/p buffer full */
#define KB_OBF			(1 << 0)  /* o/p buffer full */

#define SIO_CMD_GET_PARAM	0x80
#define SIO_CMD_SET_PARAM	0x81
#define SIO_CMD_GET_NAME_STR	0x92
#define SIO_CMD_GET_VERSION_STR	0x93

struct _FuSuperioDevice {
	FuDevice		 parent_instance;
	gint			 fd;
	gchar			*chipset;
	guint16			 port;
	guint16			 pm1_iobad0;
	guint16			 pm1_iobad1;
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
	g_string_append_printf (str, "    id:\t\t\t0x%04x\n", (guint) self->id);
	g_string_append_printf (str, "    port:\t\t0x%04x\n", (guint) self->port);
	g_string_append_printf (str, "    pm1-iobad0:\t\t0x%04x\n", (guint) self->pm1_iobad0);
	g_string_append_printf (str, "    pm1-iobad1:\t\t0x%04x\n", (guint) self->pm1_iobad1);
	g_string_append_printf (str, "    size:\t\t0x%04x\n", (guint) self->size);
}

static guint16
fu_superio_device_check_id (FuSuperioDevice *self, GError **error)
{
	guint16 id_tmp;

	/* check matches */
	if (!fu_superio_regval16 (self->fd, self->port, 0x20, &id_tmp, error))
		return FALSE;
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
		if (!fu_superio_inb (self->fd, self->pm1_iobad1, &status, error))
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
fu_superio_device_ec_read (FuSuperioDevice *self,
			   guint16 port,
			   guint8 *data,
			   GError **error)
{
	if (!fu_superio_device_wait_for (self, KB_OBF, TRUE, error))
		return FALSE;
	return fu_superio_inb (self->fd, port, data, error);
}

static gboolean
fu_superio_device_ec_write (FuSuperioDevice *self,
			    guint16 port,
			    guint8 data,
			    GError **error)
{
	if (!fu_superio_device_wait_for (self, KB_IBF, FALSE, error))
		return FALSE;
	return fu_superio_outb (self->fd, port, data, error);
}

static gboolean
fu_superio_device_ec_flush (FuSuperioDevice *self, GError **error)
{
	guint8 status = 0x00;
	g_autoptr(GTimer) timer = g_timer_new ();
	do {
		guint8 unused = 0;
		if (!fu_superio_inb (self->fd, self->pm1_iobad1, &status, error))
			return FALSE;
		if ((status & KB_OBF) == 0)
			break;
		if (!fu_superio_inb (self->fd, self->pm1_iobad0, &unused, error))
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
	if (!fu_superio_device_ec_write (self, self->pm1_iobad1, SIO_CMD_GET_PARAM, error))
		return FALSE;
	if (!fu_superio_device_ec_write (self, self->pm1_iobad0, param, error))
		return FALSE;
	return fu_superio_device_ec_read (self, self->pm1_iobad0, data, error);
}

#if 0
static gboolean
fu_superio_device_ec_set_param (FuSuperioDevice *self, guint8 param, guint8 data, GError **error)
{
	if (!fu_superio_device_ec_write (self, self->pm1_iobad1, SIO_CMD_SET_PARAM, error))
		return FALSE;
	if (!fu_superio_device_ec_write (self, self->pm1_iobad0, param, error))
		return FALSE;
	return fu_superio_device_ec_write (self, self->pm1_iobad0, data, error);
}
#endif

static gchar *
fu_superio_device_ec_get_str (FuSuperioDevice *self, guint8 idx, GError **error)
{
	GString *str = g_string_new (NULL);
	if (!fu_superio_device_ec_write (self, self->pm1_iobad1, idx, error))
		return NULL;
	for (guint i = 0; i < 0xff; i++) {
		guint8 c = 0;
		if (!fu_superio_device_ec_read (self, self->pm1_iobad0, &c, error))
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
fu_superio_device_setup_it85xx (FuSuperioDevice *self, GError **error)
{
	guint8 size_tmp = 0;
	g_autofree gchar *name = NULL;
	g_autofree gchar *version = NULL;

	/* get EC size */
	if (!fu_superio_device_ec_flush (self, error)) {
		g_prefix_error (error, "failed to flush: ");
		return FALSE;
	}
	if (!fu_superio_device_ec_get_param (self, 0xe5, &size_tmp, error)) {
		g_prefix_error (error, "failed to get EC size: ");
		return FALSE;
	}
	self->size = ((guint32) size_tmp) << 10;

	/* get EC strings */
	name = fu_superio_device_ec_get_str (self, SIO_CMD_GET_NAME_STR, error);
	if (name == NULL) {
		g_prefix_error (error, "failed to get EC name: ");
		return FALSE;
	}
	fu_device_set_name (FU_DEVICE (self), name);
	version = fu_superio_device_ec_get_str (self, SIO_CMD_GET_VERSION_STR, error);
	if (version == NULL) {
		g_prefix_error (error, "failed to get EC version: ");
		return FALSE;
	}
	fu_device_set_version (FU_DEVICE (self), version);
	return TRUE;
}

static gboolean
fu_superio_device_setup_it89xx (FuSuperioDevice *self, GError **error)
{
	guint8 version_tmp[2] = { 0x00 };
	g_autofree gchar *version = NULL;

	/* get version */
	if (!fu_superio_device_ec_flush (self, error)) {
		g_prefix_error (error, "failed to flush: ");
		return FALSE;
	}
	if (!fu_superio_device_ec_get_param (self, 0x00, &version_tmp[0], error)) {
		g_prefix_error (error, "failed to get version major: ");
		return FALSE;
	}
	if (!fu_superio_device_ec_get_param (self, 0x01, &version_tmp[1], error)) {
		g_prefix_error (error, "failed to get version minor: ");
		return FALSE;
	}
	version = g_strdup_printf ("%02u.%02u", version_tmp[0], version_tmp[1]);
	fu_device_set_version (FU_DEVICE (self), version);

	/* FIXME: hardcoded */
	self->size = 0x20000;
	return TRUE;
}

static gboolean
fu_superio_device_setup (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);

	/* check ID is correct */
	if (!fu_superio_device_check_id (self, error)) {
		g_prefix_error (error, "failed to probe id: ");
		return FALSE;
	}

	/* dump LDNs */
	if (g_getenv ("FWUPD_SUPERIO_VERBOSE") != NULL) {
		for (guint j = 0; j < MAXLDN; j++) {
			if (!fu_superio_regdump (self->fd, self->port, j, error))
				return FALSE;
		}
	}

	/* set Power Management I/F Channel 1 LDN */
	if (!fu_superio_set_ldn (self->fd, self->port, 0x11, error))
		return FALSE;

	/* get the PM1 IOBAD0 address */
	if (!fu_superio_regval16 (self->fd, self->port, 0x60, &self->pm1_iobad0, error))
		return FALSE;

	/* get the PM1 IOBAD1 address */
	if (!fu_superio_regval16 (self->fd, self->port, 0x62, &self->pm1_iobad1, error))
		return FALSE;

	/* dump PMC register map */
	if (g_getenv ("FWUPD_SUPERIO_VERBOSE") != NULL) {
		guint8 buf[0xff] = { 0x00 };
		for (guint i = 0x00; i < 0xff; i++) {
			g_autoptr(GError) error_local = NULL;
			if (!fu_superio_device_ec_get_param (self, i, &buf[i], &error_local)) {
				g_debug ("param: 0x%02x = %s", i, error_local->message);
				continue;
			}
		}
		fu_common_dump_raw (G_LOG_DOMAIN, "EC PMC Registers", buf, 0x100);
	}

	/* IT85xx */
	if (self->id >> 8 == 0x85) {
		if (!fu_superio_device_setup_it85xx (self, error))
			return FALSE;
	}

	/* IT89xx */
	if (self->id >> 8 == 0x89) {
		if (!fu_superio_device_setup_it89xx (self, error))
			return FALSE;
	}

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
fu_superio_device_new (const gchar *chipset, guint16 id, guint16 port)
{
	FuSuperioDevice *self;
	self = g_object_new (FU_TYPE_SUPERIO_DEVICE, NULL);
	self->chipset = g_strdup (chipset);
	self->id = id;
	self->port = port;
	return self;
}
