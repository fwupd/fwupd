/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-chunk.h"
#include "fu-superio-common.h"
#include "fu-superio-it89-device.h"

struct _FuSuperioIt89Device {
	FuSuperioDevice		 parent_instance;
};

G_DEFINE_TYPE (FuSuperioIt89Device, fu_superio_it89_device, FU_TYPE_SUPERIO_DEVICE)

static gboolean
fu_superio_it89_device_read_ec_register (FuSuperioDevice *self,
					 guint16 addr,
					 guint8 *outval,
					 GError **error)
{
	if (!fu_superio_device_regwrite (self,
					 SIO_LDNxx_IDX_D2ADR,
					 SIO_DEPTH2_I2EC_ADDRH,
					 error))
		return FALSE;
	if (!fu_superio_device_regwrite (self,
					 SIO_LDNxx_IDX_D2DAT,
					 addr >> 8,
					 error))
		return FALSE;
	if (!fu_superio_device_regwrite (self,
					 SIO_LDNxx_IDX_D2ADR,
					 SIO_DEPTH2_I2EC_ADDRL,
					 error))
		return FALSE;
	if (!fu_superio_device_regwrite (self,
					 SIO_LDNxx_IDX_D2DAT,
					 addr & 0xff, error))
		return FALSE;
	if (!fu_superio_device_regwrite (self,
					 SIO_LDNxx_IDX_D2ADR,
					 SIO_DEPTH2_I2EC_DATA,
					 error))
		return FALSE;
	return fu_superio_device_regval (self,
					 SIO_LDNxx_IDX_D2DAT,
					 outval,
					 error);
}

static gboolean
fu_superio_it89_device_ec_size (FuSuperioDevice *self, GError **error)
{
	guint8 tmp = 0;

	/* not sure why we can't just use SIO_LDNxx_IDX_CHIPID1,
	 * but lets do the same as the vendor flash tool... */
	if (!fu_superio_it89_device_read_ec_register (self, GCTRL_ECHIPID1, &tmp, error))
		return FALSE;
	if (tmp == 0x85) {
		g_warning ("possibly IT85xx class device?!");
		fu_device_set_firmware_size (FU_DEVICE (self), 0x20000);
		return TRUE;
	}
	g_debug ("ECHIPID1: 0x%02x", (guint) tmp);

	/* can't we just use SIO_LDNxx_IDX_CHIPVER... */
	if (!fu_superio_it89_device_read_ec_register (self, GCTRL_ECHIPVER, &tmp, error))
		return FALSE;
	g_debug ("ECHIPVER: 0x%02x", (guint) tmp);
	if (tmp >> 4 == 0x00) {
		fu_device_set_firmware_size (FU_DEVICE (self), 0x20000);
		return TRUE;
	}
	if (tmp >> 4 == 0x04) {
		fu_device_set_firmware_size (FU_DEVICE (self), 0x30000);
		return TRUE;
	}
	if (tmp >> 4 == 0x08) {
		fu_device_set_firmware_size (FU_DEVICE (self), 0x40000);
		return TRUE;
	}
	g_warning ("falling back to default size");
	fu_device_set_firmware_size (FU_DEVICE (self), 0x20000);
	return TRUE;
}

static gboolean
fu_superio_it89_device_setup (FuSuperioDevice *self, GError **error)
{
	guint8 version_tmp[2] = { 0x00 };
	g_autofree gchar *version = NULL;

	/* try to recover this */
	if (g_getenv ("FWUPD_SUPERIO_RECOVER") != NULL) {
		fu_device_set_firmware_size (FU_DEVICE (self), 0x20000);
		return TRUE;
	}

	/* get version */
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

	/* get size from the EC */
	if (!fu_superio_it89_device_ec_size (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_superio_it89_device_attach (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);

	/* re-enable HOSTWA -- use 0xfd for LCFC */
	if (!fu_superio_device_ec_write1 (self, SIO_CMD_EC_ENABLE_HOST_WA, error))
		return FALSE;

	/* success */
	fu_device_remove_flag (self, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_superio_it89_device_detach (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	guint8 tmp = 0x00;

	/* turn off HOSTWA bit, keeping HSEMIE and HSEMW high */
	if (!fu_superio_device_ec_write1 (self, SIO_CMD_EC_DISABLE_HOST_WA, error))
		return FALSE;
	if (!fu_superio_device_ec_read (self, &tmp, error))
		return FALSE;
	if (tmp != 0x33) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "failed to clear HOSTWA, got 0x%02x, expected 0x33",
			     tmp);
		return FALSE;
	}

	/* success */
	fu_device_add_flag (self, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static void
fu_superio_it89_device_init (FuSuperioIt89Device *self)
{
}

static void
fu_superio_it89_device_class_init (FuSuperioIt89DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuSuperioDeviceClass *klass_superio_device = FU_SUPERIO_DEVICE_CLASS (klass);
	klass_device->attach = fu_superio_it89_device_attach;
	klass_device->detach = fu_superio_it89_device_detach;
	klass_superio_device->setup = fu_superio_it89_device_setup;
}
