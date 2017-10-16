/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <appstream-glib.h>
#include <fcntl.h>
#include <gudev/gudev.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include <errno.h>

#include "fu-altos-firmware.h"
#include "fu-device-altos.h"

typedef struct
{
	FuDeviceAltosKind	 kind;
	GUsbDevice		*usb_device;
	guint32			 serial[9];
	gchar			*guid;
	gchar			*tty;
	gchar			*version;
	guint64			 addr_base;
	guint64			 addr_bound;
	struct termios		 tty_termios;
	gint			 tty_fd;
} FuDeviceAltosPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuDeviceAltos, fu_device_altos, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_device_altos_get_instance_private (o))

#ifndef HAVE_GUDEV_232
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevClient, g_object_unref)
#endif

/**
 * fu_device_altos_kind_from_string:
 * @kind: the string.
 *
 * Converts the text representation to an enumerated value.
 *
 * Returns: (transfer full): a #FuDeviceAltosKind, or %FU_DEVICE_ALTOS_KIND_UNKNOWN for unknown.
 *
 * Since: 0.1.0
 **/
FuDeviceAltosKind
fu_device_altos_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "BOOTLOADER") == 0)
		return FU_DEVICE_ALTOS_KIND_BOOTLOADER;
	if (g_strcmp0 (kind, "CHAOSKEY") == 0)
		return FU_DEVICE_ALTOS_KIND_CHAOSKEY;
	return FU_DEVICE_ALTOS_KIND_UNKNOWN;
}

/**
 * fu_device_altos_kind_to_string:
 * @kind: the #FuDeviceAltosKind.
 *
 * Converts the enumerated value to an text representation.
 *
 * Returns: string version of @kind
 *
 * Since: 0.1.0
 **/
const gchar *
fu_device_altos_kind_to_string (FuDeviceAltosKind kind)
{
	if (kind == FU_DEVICE_ALTOS_KIND_BOOTLOADER)
		return "BOOTLOADER";
	if (kind == FU_DEVICE_ALTOS_KIND_CHAOSKEY)
		return "CHAOSKEY";
	return NULL;
}

static void
fu_device_altos_finalize (GObject *object)
{
	FuDeviceAltos *device = FU_DEVICE_ALTOS (object);
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);

	g_free (priv->guid);
	g_free (priv->tty);
	g_free (priv->version);
	if (priv->usb_device != NULL)
		g_object_unref (priv->usb_device);

	G_OBJECT_CLASS (fu_device_altos_parent_class)->finalize (object);
}

static void
fu_device_altos_init (FuDeviceAltos *device)
{
}

static void
fu_device_altos_class_init (FuDeviceAltosClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_device_altos_finalize;
}

FuDeviceAltosKind
fu_device_altos_get_kind (FuDeviceAltos *device)
{
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);
	return priv->kind;
}

static gboolean
fu_device_altos_find_tty (FuDeviceAltos *device, GError **error)
{
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);
	g_autoptr(GList) devices = NULL;
	g_autoptr(GUdevClient) gudev_client = g_udev_client_new (NULL);

	/* find all tty devices */
	devices = g_udev_client_query_by_subsystem (gudev_client, "tty");
	for (GList *l = devices; l != NULL; l = l->next) {
		GUdevDevice *dev = G_UDEV_DEVICE (l->data);

		/* get the tty device */
		const gchar *dev_file = g_udev_device_get_device_file (dev);
		if (dev_file == NULL)
			continue;

		/* get grandparent */
		dev = g_udev_device_get_parent (dev);
		if (dev == NULL)
			continue;
		dev = g_udev_device_get_parent (dev);
		if (dev == NULL)
			continue;

		/* check correct device */
		if (g_udev_device_get_sysfs_attr_as_int (dev, "busnum") !=
		    g_usb_device_get_bus (priv->usb_device))
			continue;
		if (g_udev_device_get_sysfs_attr_as_int (dev, "devnum") !=
		    g_usb_device_get_address (priv->usb_device))
			continue;

		/* success */
		priv->tty = g_strdup (dev_file);
		return TRUE;
	}

	/* failure */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "failed to find tty for %u:%u",
		     g_usb_device_get_bus (priv->usb_device),
		     g_usb_device_get_address (priv->usb_device));
	return FALSE;
}

static gboolean
fu_device_altos_tty_write (FuDeviceAltos *device,
			   const gchar *data,
			   gssize data_len,
			   GError **error)
{
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);
	gint rc;
	gssize idx = 0;
	guint timeout_ms = 500;
	struct pollfd fds;

	/* lets assume this is text */
	if (data_len < 0)
		data_len = strlen (data);

	fds.fd = priv->tty_fd;
	fds.events = POLLOUT;

	g_debug ("write, with timeout %ums", timeout_ms);
	while (idx < data_len) {

		/* wait for data to be allowed to write without blocking */
		rc = poll (&fds, 1, (gint) timeout_ms);
		if (rc == 0)
			break;
		if (rc < 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "failed to poll %i",
				     priv->tty_fd);
			return FALSE;
		}

		/* we can write data */
		if (fds.revents & POLLOUT) {
			gssize len;
			g_debug ("writing %" G_GSSIZE_FORMAT " bytes: %s", data_len, data);
			len = write (priv->tty_fd, data + idx, data_len - idx);
			if (len < 0) {
				if (errno == EAGAIN) {
					g_debug ("got EAGAIN, trying harder");
					continue;
				}
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "failed to write %" G_GSSIZE_FORMAT
					     " bytes to %i: %s" ,
					     data_len,
					     priv->tty_fd,
					     strerror (errno));
				return FALSE;
			}
			g_debug ("wrote %" G_GSSIZE_FORMAT " bytes", len);
			idx += len;
		}
	}

	return TRUE;
}

static GString *
fu_device_altos_tty_read (FuDeviceAltos *device,
			  guint timeout_ms,
			  gssize max_size,
			  GError **error)
{
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);
	gint rc;
	struct pollfd fds;
	g_autoptr(GString) str = g_string_new (NULL);

	fds.fd = priv->tty_fd;
	fds.events = POLLIN;

	g_debug ("read, with timeout %ums", timeout_ms);
	for (;;) {
		/* wait for data to appear */
		rc = poll (&fds, 1, (gint) timeout_ms);
		if (rc == 0)
			break;
		if (rc < 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "failed to poll %i",
				     priv->tty_fd);
			return NULL;
		}

		/* we have data to read */
		if (fds.revents & POLLIN) {
			guint8 buf[1024];
			gssize len = read (priv->tty_fd, buf, sizeof (buf));
			if (len < 0) {
				if (errno == EAGAIN) {
					g_debug ("got EAGAIN, trying harder");
					continue;
				}
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "failed to read %i: %s",
					     priv->tty_fd,
					     strerror (errno));
				return NULL;
			}
			if (len > 0) {
				g_debug ("read %" G_GSSIZE_FORMAT " bytes from device", len);
				g_string_append_len (str, (gchar *) buf, len);
			}

			/* check maximum size */
			if (max_size > 0 && str->len >= (guint) max_size)
				break;
			continue;
		}
		if (fds.revents & POLLERR) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "error condition");
			return NULL;
		}
		if (fds.revents & POLLHUP) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "connection hung up");
			return NULL;
		}
		if (fds.revents & POLLNVAL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     "invalid request");
			return NULL;
		}
	}

	/* no data */
	if (str->len == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "no data received from device in %ums",
			     timeout_ms);
		return NULL;
	}

	/* return blob */
	return g_steal_pointer (&str);
}

static gboolean
fu_device_altos_tty_open (FuDeviceAltos *device, GError **error)
{
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);
	struct termios termios;
	g_autoptr(GString) str = NULL;

	/* open device */
	priv->tty_fd = open (priv->tty, O_RDWR | O_NONBLOCK);
	if (priv->tty_fd < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to open %s",
			     priv->tty);
		return FALSE;
	}

	/* get the old termios settings so we can restore later */
	if (tcgetattr (priv->tty_fd, &termios) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to get attributes from fd");
		return FALSE;
	}
	priv->tty_termios = termios;
	cfmakeraw (&termios);

	/* set speed */
	if (cfsetspeed (&termios, B9600) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to set terminal speed");
		return FALSE;
	}

	/* one input byte is enough to return
	 * inter-character timer off */
	termios.c_cc[VMIN]  = 1;
	termios.c_cc[VTIME] = 0;

	/* set all new data */
	if (tcsetattr (priv->tty_fd, TCSAFLUSH, &termios) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to set attributes on fd");
		return FALSE;
	}

	/* dump any pending input */
	str = fu_device_altos_tty_read (device, 50, -1, NULL);
	if (str != NULL)
		g_debug ("dumping pending buffer: %s", str->str);

	return TRUE;
}

static gboolean
fu_device_altos_tty_close (FuDeviceAltos *device, GError **error)
{
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);

	tcsetattr (priv->tty_fd, TCSAFLUSH, &priv->tty_termios);
	close (priv->tty_fd);

	return TRUE;
}

static GString *
fu_device_altos_read_page (FuDeviceAltos *device, guint address, GError **error)
{
	g_autoptr(GString) str = NULL;
	g_autofree gchar *cmd = g_strdup_printf ("R %x\n", address);
	if (!fu_device_altos_tty_write (device, cmd, -1, error))
		return NULL;
	str = fu_device_altos_tty_read (device, 1500, 256, error);
	if (str == NULL)
		return NULL;
	return g_steal_pointer (&str);
}

static gboolean
fu_device_altos_write_page (FuDeviceAltos *device,
			    guint address,
			    const guint8 *data,
			    guint data_len,
			    GError **error)
{
	g_autofree gchar *cmd = g_strdup_printf ("W %x\n", address);
	if (!fu_device_altos_tty_write (device, cmd, -1, error))
		return FALSE;
	if (!fu_device_altos_tty_write (device, (const gchar *) data, data_len, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_device_altos_write_firmware (FuDeviceAltos *device,
				GBytes *fw,
				FuDeviceAltosWriteFirmwareFlag flags,
				GFileProgressCallback progress_cb,
				gpointer progress_data,
				GError **error)
{
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);
	GBytes *fw_blob;
	const gchar *data;
	const gsize data_len;
	guint flash_len;
	g_autoptr(FuAltosFirmware) altos_firmware = NULL;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GString) buf = g_string_new (NULL);

	/* check kind */
	if (priv->kind != FU_DEVICE_ALTOS_KIND_BOOTLOADER) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "verification only supported in bootloader");
		return FALSE;
	}

	/* check sizes */
	if (priv->addr_base == 0x0 || priv->addr_bound == 0x0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "address base and bound are unset");
		return FALSE;
	}

	/* read in blocks of 256 bytes */
	flash_len = priv->addr_bound - priv->addr_base;
	if (flash_len == 0x0 || flash_len > 0x100000) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "address range was icorrect");
		return FALSE;
	}

	/* load ihex blob */
	altos_firmware = fu_altos_firmware_new ();
	if (!fu_altos_firmware_parse (altos_firmware, fw, error))
		return FALSE;

	/* check the start address */
	if (fu_altos_firmware_get_address (altos_firmware) != priv->addr_base) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "start address not correct %" G_GUINT64_FORMAT ":"
			     "%" G_GUINT64_FORMAT,
			     fu_altos_firmware_get_address (altos_firmware),
			     priv->addr_base);
		return FALSE;
	}

	/* check firmware will fit */
	fw_blob = fu_altos_firmware_get_data (altos_firmware);
	data = g_bytes_get_data (fw_blob, (gsize *) &data_len);
	if (data_len > flash_len) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware too large for device %" G_GSIZE_FORMAT ":%u",
			     data_len, flash_len);
		return FALSE;
	}

	/* open tty for download */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_device_altos_tty_open,
					    (FuDeviceLockerFunc) fu_device_altos_tty_close,
					    error);
	if (locker == NULL)
		return FALSE;
	for (guint i = 0; i < flash_len; i+= 0x100) {
		g_autoptr(GString) str = NULL;
		guint8 buf_tmp[0x100];

		/* copy remaining data into buf if required */
		memset (buf_tmp, 0xff, sizeof (buf));
		if (i < data_len) {
			gsize chunk_len = 0x100;
			if (i + 0x100 > data_len)
				chunk_len = data_len - i;
			memcpy (buf_tmp, data + i, chunk_len);
		}

		/* verify data from device */
		if (!fu_device_altos_write_page (device,
						 priv->addr_base + i,
						 buf_tmp,
						 0x100,
						 error))
			return FALSE;

		/* verify data written on device */
		str = fu_device_altos_read_page (device,
						 priv->addr_base + i,
						 error);
		if (str == NULL)
			return FALSE;
		if (str->len != 0x100) {
			g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "failed to verify @%x, "
					     "not enough data returned",
					     (guint) (priv->addr_base + i));
			return FALSE;
		}
		if (memcmp (str->str, buf_tmp, 0x100) != 0) {
			g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "failed to verify @%x",
					     (guint) (priv->addr_base + i));
			return FALSE;
		}

		/* progress */
		if (progress_cb != NULL) {
			progress_cb ((goffset) i,
				     (goffset) flash_len,
				     progress_data);
		}
		g_string_append_len (buf, str->str, str->len);
	}

	/* go to application mode */
	if (flags & FU_DEVICE_ALTOS_WRITE_FIRMWARE_FLAG_REBOOT) {
		if (!fu_device_altos_tty_write (device, "a\n", -1, error))
			return FALSE;
	}

	/* progress complete */
	if (progress_cb != NULL) {
		progress_cb ((goffset) flash_len,
			     (goffset) flash_len,
			     progress_data);
	}

	/* success */
	return TRUE;
}

GBytes *
fu_device_altos_read_firmware (FuDeviceAltos *device,
			       GFileProgressCallback progress_cb,
			       gpointer progress_data,
			       GError **error)
{
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);
	guint flash_len;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GString) buf = g_string_new (NULL);

	/* check kind */
	if (priv->kind != FU_DEVICE_ALTOS_KIND_BOOTLOADER) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "verification only supported in bootloader");
		return NULL;
	}

	/* check sizes */
	if (priv->addr_base == 0x0 || priv->addr_bound == 0x0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "address base and bound are unset");
		return NULL;
	}

	/* read in blocks of 256 bytes */
	flash_len = priv->addr_bound - priv->addr_base;
	if (flash_len == 0x0 || flash_len > 0x100000) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "address range was icorrect");
		return NULL;
	}

	/* open tty for download */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_device_altos_tty_open,
					    (FuDeviceLockerFunc) fu_device_altos_tty_close,
					    error);
	if (locker == NULL)
		return NULL;
	for (guint i = priv->addr_base; i < priv->addr_bound; i+= 0x100) {
		g_autoptr(GString) str = NULL;

		/* request data from device */
		str = fu_device_altos_read_page (device, i, error);
		if (str == NULL)
			return NULL;

		/* progress */
		if (progress_cb != NULL) {
			progress_cb ((goffset) (i - priv->addr_base),
				     (goffset) (priv->addr_bound - priv->addr_base),
				     progress_data);
		}
		g_string_append_len (buf, str->str, str->len);
	}

	/* success */
	return g_bytes_new (buf->str, buf->len);
}

static gboolean
fu_device_altos_probe_bootloader (FuDeviceAltos *device, GError **error)
{
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GString) str = NULL;

	/* get tty for upload */
	if (!fu_device_altos_find_tty (device, error))
		return FALSE;
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_device_altos_tty_open,
					    (FuDeviceLockerFunc) fu_device_altos_tty_close,
					    error);
	if (locker == NULL)
		return FALSE;

	/* get the version information */
	if (!fu_device_altos_tty_write (device, "v\n", -1, error))
		return FALSE;
	str = fu_device_altos_tty_read (device, 100, -1, error);
	if (str == NULL)
		return FALSE;

	/* parse each line */
	lines = g_strsplit_set (str->str, "\n\r", -1);
	for (guint i = 0; lines[i] != NULL; i++) {

		/* ignore */
		if (lines[i][0] == '\0')
			continue;
		if (g_str_has_prefix (lines[i], "manufacturer     "))
			continue;
		if (g_str_has_prefix (lines[i], "product          "))
			continue;

		/* we can flash firmware */
		if (g_strcmp0 (lines[i], "altos-loader") == 0) {
			fu_device_remove_flag (FU_DEVICE (device),
					       FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
			continue;
		}

		/* version number */
		if (g_str_has_prefix (lines[i], "software-version ")) {
			fu_device_set_version (FU_DEVICE (device), lines[i] + 17);
			continue;
		}

		/* address base and bound */
		if (g_str_has_prefix (lines[i], "flash-range      ")) {
			g_auto(GStrv) addrs = g_strsplit (lines[i] + 17, " ", -1);
			priv->addr_base = g_ascii_strtoull (addrs[0], NULL, 16);
			priv->addr_bound = g_ascii_strtoull (addrs[1], NULL, 16);
			g_debug ("base: %x, bound: %x",
				 (guint) priv->addr_base,
				 (guint) priv->addr_bound);
			continue;
		}

		/* unknown line */
		g_debug ("unknown data: '%s'", lines[i]);
	}

	return TRUE;
}

gboolean
fu_device_altos_probe (FuDeviceAltos *device, GError **error)
{
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);

	/* bootloader uses tty */
	if (priv->kind == FU_DEVICE_ALTOS_KIND_BOOTLOADER)
		return fu_device_altos_probe_bootloader (device, error);

	/* get version */
	if (priv->kind == FU_DEVICE_ALTOS_KIND_CHAOSKEY) {
		const gchar *version_prefix = "ChaosKey-hw-1.0-sw-";
		guint8 version_idx;
		g_autofree gchar *version = NULL;
		g_autoptr(FuDeviceLocker) locker = NULL;

		/* open */
		locker = fu_device_locker_new (priv->usb_device, error);
		if (locker == NULL)
			return FALSE;

		/* get string */
		version_idx = g_usb_device_get_product_index (priv->usb_device);
		version = g_usb_device_get_string_descriptor (priv->usb_device,
							      version_idx,
							      error);
		if (version == NULL)
			return FALSE;
		if (!g_str_has_prefix (version, version_prefix)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not a ChaosKey v1.0 device: %s",
				     version);
			return FALSE;
		}
		fu_device_set_version (FU_DEVICE (device), version + 19);
	}

	/* success */
	return TRUE;
}

/* now with kind and usb_device set */
static void
fu_device_altos_init_real (FuDeviceAltos *device)
{
	FuDeviceAltosPrivate *priv = GET_PRIVATE (device);
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *vendor_id = NULL;

	/* allowed, but requires manual bootloader step */
	fu_device_add_flag (FU_DEVICE (device),
			    FWUPD_DEVICE_FLAG_UPDATABLE);

	/* set default vendor */
	fu_device_set_vendor (FU_DEVICE (device), "altusmetrum.org");

	/* set vendor ID */
	vendor_id = g_strdup_printf ("USB:0x%04X", g_usb_device_get_vid (priv->usb_device));
	fu_device_set_vendor_id (FU_DEVICE (device), vendor_id);

	/* set name */
	switch (priv->kind) {
	case FU_DEVICE_ALTOS_KIND_BOOTLOADER:
		fu_device_set_name (FU_DEVICE (device), "Altos [bootloader]");
		break;
	case FU_DEVICE_ALTOS_KIND_CHAOSKEY:
		fu_device_set_name (FU_DEVICE (device), "Altos ChaosKey");
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* set one line summary */
	fu_device_set_summary (FU_DEVICE (device),
			       "A USB hardware random number generator");

	/* add USB\VID_0000&PID_0000 */
	devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				  g_usb_device_get_vid (priv->usb_device),
				  g_usb_device_get_pid (priv->usb_device));
	fu_device_add_guid (FU_DEVICE (device), devid1);
	g_debug ("saving runtime GUID of %s", devid1);

	/* only the bootloader can do the update */
	if (priv->kind != FU_DEVICE_ALTOS_KIND_BOOTLOADER) {
		fu_device_add_flag (FU_DEVICE (device),
				    FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	}
}

typedef struct {
	guint16			 vid;
	guint16			 pid;
	FuDeviceAltosKind	 kind;
} FuDeviceAltosVidPid;

FuDeviceAltos *
fu_device_altos_new (GUsbDevice *usb_device)
{
	FuDeviceAltos *device;
	FuDeviceAltosPrivate *priv;
	const FuDeviceAltosVidPid vidpids[] = {
		{ 0xfffe, 0x000a, FU_DEVICE_ALTOS_KIND_BOOTLOADER },
		{ 0x1d50, 0x60c6, FU_DEVICE_ALTOS_KIND_CHAOSKEY },
		{ 0x0000, 0x0000, FU_DEVICE_ALTOS_KIND_UNKNOWN }
	};

	/* set kind */
	for (guint j = 0; vidpids[j].vid != 0x0000; j++) {
		if (g_usb_device_get_vid (usb_device) != vidpids[j].vid)
			continue;
		if (g_usb_device_get_pid (usb_device) != vidpids[j].pid)
			continue;
		device = g_object_new (FU_TYPE_DEVICE_ALTOS, NULL);
		priv = GET_PRIVATE (device);
		priv->kind = vidpids[j].kind;
		priv->usb_device = g_object_ref (usb_device);
		fu_device_altos_init_real (device);
		return device;
	}
	return NULL;
}
