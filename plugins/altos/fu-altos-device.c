/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include <errno.h>

#include "fu-io-channel.h"
#include "fu-altos-device.h"
#include "fu-altos-firmware.h"

struct _FuAltosDevice {
	FuUsbDevice		 parent_instance;
	FuAltosDeviceKind	 kind;
	guint32			 serial[9];
	gchar			*guid;
	gchar			*tty;
	gchar			*version;
	guint64			 addr_base;
	guint64			 addr_bound;
	struct termios		 tty_termios;
	FuIOChannel		*io_channel;
};

G_DEFINE_TYPE (FuAltosDevice, fu_altos_device, FU_TYPE_USB_DEVICE)

#ifndef HAVE_GUDEV_232
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevClient, g_object_unref)
#pragma clang diagnostic pop
#endif

/**
 * fu_altos_device_kind_from_string:
 * @kind: the string.
 *
 * Converts the text representation to an enumerated value.
 *
 * Returns: (transfer full): a #FuAltosDeviceKind, or %FU_ALTOS_DEVICE_KIND_UNKNOWN for unknown.
 *
 * Since: 0.1.0
 **/
FuAltosDeviceKind
fu_altos_device_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "BOOTLOADER") == 0)
		return FU_ALTOS_DEVICE_KIND_BOOTLOADER;
	if (g_strcmp0 (kind, "CHAOSKEY") == 0)
		return FU_ALTOS_DEVICE_KIND_CHAOSKEY;
	return FU_ALTOS_DEVICE_KIND_UNKNOWN;
}

/**
 * fu_altos_device_kind_to_string:
 * @kind: the #FuAltosDeviceKind.
 *
 * Converts the enumerated value to an text representation.
 *
 * Returns: string version of @kind
 *
 * Since: 0.1.0
 **/
const gchar *
fu_altos_device_kind_to_string (FuAltosDeviceKind kind)
{
	if (kind == FU_ALTOS_DEVICE_KIND_BOOTLOADER)
		return "BOOTLOADER";
	if (kind == FU_ALTOS_DEVICE_KIND_CHAOSKEY)
		return "CHAOSKEY";
	return NULL;
}

static void
fu_altos_device_finalize (GObject *object)
{
	FuAltosDevice *self = FU_ALTOS_DEVICE (object);

	g_free (self->guid);
	g_free (self->tty);
	g_free (self->version);

	G_OBJECT_CLASS (fu_altos_device_parent_class)->finalize (object);
}

FuAltosDeviceKind
fu_altos_device_get_kind (FuAltosDevice *self)
{
	return self->kind;
}

static gboolean
fu_altos_device_find_tty (FuAltosDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
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
		    g_usb_device_get_bus (usb_device))
			continue;
		if (g_udev_device_get_sysfs_attr_as_int (dev, "devnum") !=
		    g_usb_device_get_address (usb_device))
			continue;

		/* success */
		self->tty = g_strdup (dev_file);
		return TRUE;
	}

	/* failure */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "failed to find tty for %u:%u",
		     g_usb_device_get_bus (usb_device),
		     g_usb_device_get_address (usb_device));
	return FALSE;
}

static gboolean
fu_altos_device_tty_write (FuAltosDevice *self,
			   const gchar *data,
			   gssize data_len,
			   GError **error)
{
	/* lets assume this is text */
	if (data_len < 0)
		data_len = strlen (data);
	return fu_io_channel_write_raw (self->io_channel,
					(const guint8 *) data,
					(gsize) data_len,
					500, /* ms */
					FU_IO_CHANNEL_FLAG_NONE,
					error);
}

static GString *
fu_altos_device_tty_read (FuAltosDevice *self,
			  guint timeout_ms,
			  gssize max_size,
			  GError **error)
{
	g_autoptr(GBytes) buf = NULL;
	buf = fu_io_channel_read_bytes (self->io_channel, max_size,
					timeout_ms, FU_IO_CHANNEL_FLAG_NONE, error);
	if (buf == NULL)
		return NULL;
	return g_string_new_len (g_bytes_get_data (buf, NULL), g_bytes_get_size (buf));
}

static gboolean
fu_altos_device_tty_open (FuAltosDevice *self, GError **error)
{
	struct termios termios;
	g_autoptr(GString) str = NULL;

	/* open device */
	self->io_channel = fu_io_channel_new_file (self->tty, error);
	if (self->io_channel == NULL)
		return FALSE;

	/* get the old termios settings so we can restore later */
	if (tcgetattr (fu_io_channel_unix_get_fd (self->io_channel), &termios) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to get attributes from fd");
		return FALSE;
	}
	self->tty_termios = termios;
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
	if (tcsetattr (fu_io_channel_unix_get_fd (self->io_channel),
		       TCSAFLUSH, &termios) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to set attributes on fd");
		return FALSE;
	}

	/* dump any pending input */
	str = fu_altos_device_tty_read (self, 50, -1, NULL);
	if (str != NULL)
		g_debug ("dumping pending buffer: %s", str->str);

	return TRUE;
}

static gboolean
fu_altos_device_tty_close (FuAltosDevice *self, GError **error)
{
	tcsetattr (fu_io_channel_unix_get_fd (self->io_channel),
		   TCSAFLUSH, &self->tty_termios);
	if (!fu_io_channel_shutdown (self->io_channel, error))
		return FALSE;
	g_clear_object (&self->io_channel);
	return TRUE;
}

static GString *
fu_altos_device_read_page (FuAltosDevice *self, guint address, GError **error)
{
	g_autoptr(GString) str = NULL;
	g_autofree gchar *cmd = g_strdup_printf ("R %x\n", address);
	if (!fu_altos_device_tty_write (self, cmd, -1, error))
		return NULL;
	str = fu_altos_device_tty_read (self, 1500, 256, error);
	if (str == NULL)
		return NULL;
	return g_steal_pointer (&str);
}

static gboolean
fu_altos_device_write_page (FuAltosDevice *self,
			    guint address,
			    const guint8 *data,
			    guint data_len,
			    GError **error)
{
	g_autofree gchar *cmd = g_strdup_printf ("W %x\n", address);
	if (!fu_altos_device_tty_write (self, cmd, -1, error))
		return FALSE;
	if (!fu_altos_device_tty_write (self, (const gchar *) data, data_len, error))
		return FALSE;
	return TRUE;
}

static FuFirmware *
fu_altos_device_prepare_firmware (FuDevice *device,
				  GBytes *fw,
				  FwupdInstallFlags flags,
				  GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_altos_firmware_new ();
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return FALSE;
	return g_steal_pointer (&firmware);
}

static gboolean
fu_altos_device_write_firmware (FuDevice *device,
				FuFirmware *firmware,
				FwupdInstallFlags flags,
				GError **error)
{
	FuAltosDevice *self = FU_ALTOS_DEVICE (device);
	const gchar *data;
	const gsize data_len;
	guint flash_len;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(FuFirmwareImage) img = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GString) buf = g_string_new (NULL);

	/* check kind */
	if (self->kind != FU_ALTOS_DEVICE_KIND_BOOTLOADER) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "verification only supported in bootloader");
		return FALSE;
	}

	/* check sizes */
	if (self->addr_base == 0x0 || self->addr_bound == 0x0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "address base and bound are unset");
		return FALSE;
	}

	/* read in blocks of 256 bytes */
	flash_len = self->addr_bound - self->addr_base;
	if (flash_len == 0x0 || flash_len > 0x100000) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "address range was icorrect");
		return FALSE;
	}

	/* load ihex blob */
	img = fu_firmware_get_image_default (firmware, error);
	if (img == NULL)
		return FALSE;

	/* check the start address */
	if (fu_firmware_image_get_addr (img) != self->addr_base) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "start address not correct %" G_GUINT64_FORMAT ":"
			     "%" G_GUINT64_FORMAT,
			     fu_firmware_image_get_addr (img),
			     self->addr_base);
		return FALSE;
	}

	/* check firmware will fit */
	fw = fu_firmware_image_get_bytes (img, error);
	if (fw == NULL)
		return FALSE;
	data = g_bytes_get_data (fw, (gsize *) &data_len);
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
					    (FuDeviceLockerFunc) fu_altos_device_tty_open,
					    (FuDeviceLockerFunc) fu_altos_device_tty_close,
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
			if (!fu_memcpy_safe (buf_tmp, sizeof(buf_tmp), 0,		/* dst */
					     (const guint8 *) data, data_len, i,	/* src */
					     chunk_len, error))
				return FALSE;
		}

		/* verify data from device */
		if (!fu_altos_device_write_page (self,
						 self->addr_base + i,
						 buf_tmp,
						 0x100,
						 error))
			return FALSE;

		/* verify data written on device */
		str = fu_altos_device_read_page (self,
						 self->addr_base + i,
						 error);
		if (str == NULL)
			return FALSE;
		if (str->len != 0x100) {
			g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "failed to verify @%x, "
					     "not enough data returned",
					     (guint) (self->addr_base + i));
			return FALSE;
		}
		if (memcmp (str->str, buf_tmp, 0x100) != 0) {
			g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_WRITE,
					     "failed to verify @%x",
					     (guint) (self->addr_base + i));
			return FALSE;
		}

		/* progress */
		fu_device_set_progress_full (device, i, flash_len);
		g_string_append_len (buf, str->str, str->len);
	}

	/* go to application mode */
	if (!fu_altos_device_tty_write (self, "a\n", -1, error))
		return FALSE;

	/* progress complete */
	fu_device_set_progress_full (device, flash_len, flash_len);

	/* success */
	return TRUE;
}

static GBytes *
fu_altos_device_read_firmware (FuDevice *device, GError **error)
{
	FuAltosDevice *self = FU_ALTOS_DEVICE (device);
	guint flash_len;
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_autoptr(GString) buf = g_string_new (NULL);

	/* check kind */
	if (self->kind != FU_ALTOS_DEVICE_KIND_BOOTLOADER) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "verification only supported in bootloader");
		return NULL;
	}

	/* check sizes */
	if (self->addr_base == 0x0 || self->addr_bound == 0x0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "address base and bound are unset");
		return NULL;
	}

	/* read in blocks of 256 bytes */
	flash_len = self->addr_bound - self->addr_base;
	if (flash_len == 0x0 || flash_len > 0x100000) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "address range was icorrect");
		return NULL;
	}

	/* open tty for download */
	locker = fu_device_locker_new_full (device,
					    (FuDeviceLockerFunc) fu_altos_device_tty_open,
					    (FuDeviceLockerFunc) fu_altos_device_tty_close,
					    error);
	if (locker == NULL)
		return NULL;
	for (guint i = self->addr_base; i < self->addr_bound; i+= 0x100) {
		g_autoptr(GString) str = NULL;

		/* request data from device */
		str = fu_altos_device_read_page (self, i, error);
		if (str == NULL)
			return NULL;

		/* progress */
		fu_device_set_progress_full (device,
					     i - self->addr_base,
					     self->addr_bound - self->addr_base);
		g_string_append_len (buf, str->str, str->len);
	}

	/* success */
	return g_bytes_new (buf->str, buf->len);
}

static gboolean
fu_altos_device_probe_bootloader (FuAltosDevice *self, GError **error)
{
	g_autoptr(FuDeviceLocker) locker  = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GString) str = NULL;

	/* get tty for upload */
	if (!fu_altos_device_find_tty (self, error))
		return FALSE;
	locker = fu_device_locker_new_full (self,
					    (FuDeviceLockerFunc) fu_altos_device_tty_open,
					    (FuDeviceLockerFunc) fu_altos_device_tty_close,
					    error);
	if (locker == NULL)
		return FALSE;

	/* get the version information */
	if (!fu_altos_device_tty_write (self, "v\n", -1, error))
		return FALSE;
	str = fu_altos_device_tty_read (self, 100, -1, error);
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
			fu_device_remove_flag (FU_DEVICE (self),
					       FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
			continue;
		}

		/* version number */
		if (g_str_has_prefix (lines[i], "software-version ")) {
			fu_device_set_version (FU_DEVICE (self), lines[i] + 17,
					       FWUPD_VERSION_FORMAT_TRIPLET);
			continue;
		}

		/* address base and bound */
		if (g_str_has_prefix (lines[i], "flash-range      ")) {
			g_auto(GStrv) addrs = g_strsplit (lines[i] + 17, " ", -1);
			self->addr_base = g_ascii_strtoull (addrs[0], NULL, 16);
			self->addr_bound = g_ascii_strtoull (addrs[1], NULL, 16);
			g_debug ("base: %x, bound: %x",
				 (guint) self->addr_base,
				 (guint) self->addr_bound);
			continue;
		}

		/* unknown line */
		g_debug ("unknown data: '%s'", lines[i]);
	}

	return TRUE;
}

static gboolean
fu_altos_device_probe (FuDevice *device, GError **error)
{
	FuAltosDevice *self = FU_ALTOS_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));

	/* bootloader uses tty */
	if (self->kind == FU_ALTOS_DEVICE_KIND_BOOTLOADER)
		return fu_altos_device_probe_bootloader (self, error);

	/* get version */
	if (self->kind == FU_ALTOS_DEVICE_KIND_CHAOSKEY) {
		const gchar *version_prefix = "ChaosKey-hw-1.0-sw-";
		guint8 version_idx;
		g_autofree gchar *version = NULL;
		g_autoptr(FuDeviceLocker) locker = NULL;

		/* open */
		locker = fu_device_locker_new (usb_device, error);
		if (locker == NULL)
			return FALSE;

		/* get string */
		version_idx = g_usb_device_get_product_index (usb_device);
		version = g_usb_device_get_string_descriptor (usb_device,
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
		fu_device_set_version (FU_DEVICE (self), version + 19,
				       FWUPD_VERSION_FORMAT_TRIPLET);
	}

	/* success */
	return TRUE;
}

/* now with kind and usb_device set */
static void
fu_altos_device_init_real (FuAltosDevice *self)
{
	/* allowed, but requires manual bootloader step */
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);

	/* set default vendor */
	fu_device_set_vendor (FU_DEVICE (self), "altusmetrum.org");

	/* set name */
	switch (self->kind) {
	case FU_ALTOS_DEVICE_KIND_BOOTLOADER:
		fu_device_set_name (FU_DEVICE (self), "Altos [bootloader]");
		break;
	case FU_ALTOS_DEVICE_KIND_CHAOSKEY:
		fu_device_set_name (FU_DEVICE (self), "Altos ChaosKey");
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* set one line summary */
	fu_device_set_summary (FU_DEVICE (self),
			       "A USB hardware random number generator");

	/* only the bootloader can do the update */
	if (self->kind != FU_ALTOS_DEVICE_KIND_BOOTLOADER) {
		fu_device_add_flag (FU_DEVICE (self),
				    FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	}
}

static void
fu_altos_device_init (FuAltosDevice *self)
{
}

static void
fu_altos_device_class_init (FuAltosDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->probe = fu_altos_device_probe;
	klass_device->prepare_firmware = fu_altos_device_prepare_firmware;
	klass_device->write_firmware = fu_altos_device_write_firmware;
	klass_device->read_firmware = fu_altos_device_read_firmware;
	object_class->finalize = fu_altos_device_finalize;
}

typedef struct {
	guint16			 vid;
	guint16			 pid;
	FuAltosDeviceKind	 kind;
} FuAltosDeviceVidPid;

FuAltosDevice *
fu_altos_device_new (FuUsbDevice *device)
{
	const FuAltosDeviceVidPid vidpids[] = {
		{ 0xfffe, 0x000a, FU_ALTOS_DEVICE_KIND_BOOTLOADER },
		{ 0x1d50, 0x60c6, FU_ALTOS_DEVICE_KIND_CHAOSKEY },
		{ 0x0000, 0x0000, FU_ALTOS_DEVICE_KIND_UNKNOWN }
	};

	/* set kind */
	for (guint j = 0; vidpids[j].vid != 0x0000; j++) {
		if (fu_usb_device_get_vid (device) == vidpids[j].vid &&
		    fu_usb_device_get_pid (device) == vidpids[j].pid) {
			FuAltosDevice *self = g_object_new (FU_TYPE_ALTOS_DEVICE, NULL);
			fu_device_incorporate (FU_DEVICE (self), FU_DEVICE (device));
			self->kind = vidpids[j].kind;
			fu_altos_device_init_real (self);
			return self;
		}
	}
	return NULL;
}
