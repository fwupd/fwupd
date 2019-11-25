/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-superio-common.h"
#include "fu-superio-device.h"

#define FU_PLUGIN_SUPERIO_TIMEOUT	0.25 /* s */

typedef struct
{
	gchar			*chipset;
	guint16			 port;
	guint16			 pm1_iobad0;
	guint16			 pm1_iobad1;
	guint16			 id;
} FuSuperioDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuSuperioDevice, fu_superio_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_superio_device_get_instance_private (o))

enum {
	PROP_0,
	PROP_CHIPSET,
	PROP_PORT,
	PROP_ID,
	PROP_LAST
};

gboolean
fu_superio_device_regval (FuSuperioDevice *self, guint8 addr,
			  guint8 *data, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	if (!fu_udev_device_pwrite (FU_UDEV_DEVICE (self), priv->port, addr, error))
		return FALSE;
	if (!fu_udev_device_pread (FU_UDEV_DEVICE (self), priv->port + 1, data, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_superio_device_regval16 (FuSuperioDevice *self, guint8 addr,
			    guint16 *data, GError **error)
{
	guint8 msb;
	guint8 lsb;
	if (!fu_superio_device_regval (self, addr, &msb, error))
		return FALSE;
	if (!fu_superio_device_regval (self, addr + 1, &lsb, error))
		return FALSE;
	*data = ((guint16) msb << 8) | (guint16) lsb;
	return TRUE;
}

gboolean
fu_superio_device_regwrite (FuSuperioDevice *self, guint8 addr,
			    guint8 data, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	if (!fu_udev_device_pwrite (FU_UDEV_DEVICE (self), priv->port, addr, error))
		return FALSE;
	if (!fu_udev_device_pwrite (FU_UDEV_DEVICE (self), priv->port + 1, data, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_superio_device_set_ldn (FuSuperioDevice *self, guint8 ldn, GError **error)
{
	return fu_superio_device_regwrite (self, SIO_LDNxx_IDX_LDNSEL, ldn, error);
}

static gboolean
fu_superio_device_regdump (FuSuperioDevice *self, guint8 ldn, GError **error)
{
	const gchar *ldnstr = fu_superio_ldn_to_text (ldn);
	guint8 buf[0xff] = { 0x00 };
	guint16 iobad0 = 0x0;
	guint16 iobad1 = 0x0;
	g_autoptr(GString) str = g_string_new (NULL);

	/* set LDN */
	if (!fu_superio_device_set_ldn (self, ldn, error))
		return FALSE;
	for (guint i = 0x00; i < 0xff; i++) {
		if (!fu_superio_device_regval (self, i, &buf[i], error))
			return FALSE;
	}

	/* get the i/o base addresses */
	if (!fu_superio_device_regval16 (self, SIO_LDNxx_IDX_IOBAD0, &iobad0, error))
		return FALSE;
	if (!fu_superio_device_regval16 (self, SIO_LDNxx_IDX_IOBAD1, &iobad1, error))
		return FALSE;

	g_string_append_printf (str, "LDN:0x%02x ", ldn);
	if (iobad0 != 0x0)
		g_string_append_printf (str, "IOBAD0:0x%04x ", iobad0);
	if (iobad1 != 0x0)
		g_string_append_printf (str, "IOBAD1:0x%04x ", iobad1);
	if (ldnstr != NULL)
		g_string_append_printf (str, "(%s)", ldnstr);
	fu_common_dump_raw (G_LOG_DOMAIN, str->str, buf, sizeof(buf));
	return TRUE;
}

static void
fu_superio_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	fu_common_string_append_kv (str, idt, "Chipset", priv->chipset);
	fu_common_string_append_kx (str, idt, "Id", priv->id);
	fu_common_string_append_kx (str, idt, "Port", priv->port);
	fu_common_string_append_kx (str, idt, "PM1_IOBAD0", priv->pm1_iobad0);
	fu_common_string_append_kx (str, idt, "PM1_IOBAD1", priv->pm1_iobad1);
}

static guint16
fu_superio_device_check_id (FuSuperioDevice *self, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	guint16 id_tmp;

	/* check ID, which can be done from any LDN */
	if (!fu_superio_device_regval16 (self, SIO_LDNxx_IDX_CHIPID1, &id_tmp, error))
		return FALSE;
	if (priv->id != id_tmp) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "SuperIO chip not supported, got %04x, expected %04x",
			     (guint) id_tmp, (guint) priv->id);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_superio_device_wait_for (FuSuperioDevice *self, guint8 mask, gboolean set, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GTimer) timer = g_timer_new ();
	do {
		guint8 status = 0x00;
		if (!fu_udev_device_pread (FU_UDEV_DEVICE (self), priv->pm1_iobad1, &status, error))
			return FALSE;
		if (g_timer_elapsed (timer, NULL) > FU_PLUGIN_SUPERIO_TIMEOUT)
			break;
		if (set && (status & mask) != 0)
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

gboolean
fu_superio_device_ec_read (FuSuperioDevice *self, guint8 *data, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	if (!fu_superio_device_wait_for (self, SIO_STATUS_EC_OBF, TRUE, error))
		return FALSE;
	return fu_udev_device_pread (FU_UDEV_DEVICE (self), priv->pm1_iobad0, data, error);
}

gboolean
fu_superio_device_ec_write0 (FuSuperioDevice *self, guint8 data, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	if (!fu_superio_device_wait_for (self, SIO_STATUS_EC_IBF, FALSE, error))
		return FALSE;
	return fu_udev_device_pwrite (FU_UDEV_DEVICE (self), priv->pm1_iobad0, data, error);
}

gboolean
fu_superio_device_ec_write1 (FuSuperioDevice *self, guint8 data, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	if (!fu_superio_device_wait_for (self, SIO_STATUS_EC_IBF, FALSE, error))
		return FALSE;
	return fu_udev_device_pwrite (FU_UDEV_DEVICE (self), priv->pm1_iobad1, data, error);
}

static gboolean
fu_superio_device_ec_flush (FuSuperioDevice *self, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	guint8 status = 0x00;
	g_autoptr(GTimer) timer = g_timer_new ();
	do {
		guint8 unused = 0;
		if (!fu_udev_device_pread (FU_UDEV_DEVICE (self), priv->pm1_iobad1, &status, error))
			return FALSE;
		if ((status & SIO_STATUS_EC_OBF) == 0)
			break;
		if (!fu_udev_device_pread (FU_UDEV_DEVICE (self), priv->pm1_iobad0, &unused, error))
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

gboolean
fu_superio_device_ec_get_param (FuSuperioDevice *self, guint8 param, guint8 *data, GError **error)
{
	if (!fu_superio_device_ec_write1 (self, SIO_CMD_EC_READ, error))
		return FALSE;
	if (!fu_superio_device_ec_write0 (self, param, error))
		return FALSE;
	return fu_superio_device_ec_read (self, data, error);
}

#if 0
static gboolean
fu_superio_device_ec_set_param (FuSuperioDevice *self, guint8 param, guint8 data, GError **error)
{
	if (!fu_superio_device_ec_write1 (self, SIO_CMD_EC_WRITE, error))
		return FALSE;
	if (!fu_superio_device_ec_write0 (self, param, error))
		return FALSE;
	return fu_superio_device_ec_write0 (self, data, error);
}
#endif

static gboolean
fu_superio_device_probe (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	g_autofree gchar *devid = NULL;
	g_autofree gchar *name = NULL;

	/* use the chipset name as the logical ID and for the GUID */
	fu_device_set_logical_id (device, priv->chipset);
	devid = g_strdup_printf ("SuperIO-%s", priv->chipset);
	fu_device_add_instance_id (device, devid);
	name = g_strdup_printf ("SuperIO %s", priv->chipset);
	fu_device_set_name (FU_DEVICE (self), name);
	return TRUE;
}

static gboolean
fu_superio_device_setup (FuDevice *device, GError **error)
{
	FuSuperioDeviceClass *klass = FU_SUPERIO_DEVICE_GET_CLASS (device);
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);

	/* check ID is correct */
	if (!fu_superio_device_check_id (self, error)) {
		g_prefix_error (error, "failed to probe id: ");
		return FALSE;
	}

	/* dump LDNs */
	if (g_getenv ("FWUPD_SUPERIO_VERBOSE") != NULL) {
		for (guint j = 0; j < SIO_LDN_LAST; j++) {
			if (!fu_superio_device_regdump (self, j, error))
				return FALSE;
		}
	}

	/* set Power Management I/F Channel 1 LDN */
	if (!fu_superio_device_set_ldn (self, SIO_LDN_PM1, error))
		return FALSE;

	/* get the PM1 IOBAD0 address */
	if (!fu_superio_device_regval16 (self, SIO_LDNxx_IDX_IOBAD0,
					 &priv->pm1_iobad0, error))
		return FALSE;

	/* get the PM1 IOBAD1 address */
	if (!fu_superio_device_regval16 (self, SIO_LDNxx_IDX_IOBAD1,
					 &priv->pm1_iobad1, error))
		return FALSE;

	/* drain */
	if (!fu_superio_device_ec_flush (self, error)) {
		g_prefix_error (error, "failed to flush: ");
		return FALSE;
	}

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
		fu_common_dump_raw (G_LOG_DOMAIN, "EC Registers", buf, 0x100);
	}

	/* subclassed setup */
	if (klass->setup != NULL)
		return klass->setup (self, error);

	/* success */
	return TRUE;
}

static FuFirmware *
fu_superio_device_prepare_firmware (FuDevice *device,
				    GBytes *fw,
				    FwupdInstallFlags flags,
				    GError **error)
{
	gsize sz = 0;
	const guint8 *buf = g_bytes_get_data (fw, &sz);
	const guint8 sig1[] = { 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5 };
	const guint8 sig2[] = { 0x85, 0x12, 0x5a, 0x5a, 0xaa };

	/* find signature -- maybe ignore byte 0x14 too? */
	for (gsize off = 0; off < sz; off += 16) {
		if (memcmp (&buf[off], sig1, sizeof(sig1)) == 0 &&
		    memcmp (&buf[off + 8], sig2, sizeof(sig2)) == 0) {
			g_debug ("found signature at 0x%04x", (guint) off);
			return fu_firmware_new_from_bytes (fw);
		}
	}
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "did not detect signature in firmware image");
	return NULL;
}

static void
fu_superio_device_get_property (GObject *object, guint prop_id,
				GValue *value, GParamSpec *pspec)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (object);
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_CHIPSET:
		g_value_set_string (value, priv->chipset);
		break;
	case PROP_PORT:
		g_value_set_uint (value, priv->port);
		break;
	case PROP_ID:
		g_value_set_uint (value, priv->id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_superio_device_set_property (GObject *object, guint prop_id,
				const GValue *value, GParamSpec *pspec)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (object);
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_CHIPSET:
		g_free (priv->chipset);
		priv->chipset = g_value_dup_string (value);
		break;
	case PROP_PORT:
		priv->port = g_value_get_uint (value);
		break;
	case PROP_ID:
		priv->id = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_superio_device_init (FuSuperioDevice *self)
{
	fu_device_set_physical_id (FU_DEVICE (self), "/dev/port");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_set_protocol (FU_DEVICE (self), "tw.com.ite.superio");
	fu_device_set_summary (FU_DEVICE (self), "Embedded Controller");
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
	GParamSpec *pspec;
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);

	/* properties */
	object_class->get_property = fu_superio_device_get_property;
	object_class->set_property = fu_superio_device_set_property;
	pspec = g_param_spec_string ("chipset", NULL, NULL, NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_CHIPSET, pspec);
	pspec = g_param_spec_uint ("port", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE |
				   G_PARAM_CONSTRUCT_ONLY |
				   G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_PORT, pspec);
	pspec = g_param_spec_uint ("id", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE |
				   G_PARAM_CONSTRUCT_ONLY |
				   G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	object_class->finalize = fu_superio_device_finalize;
	klass_device->to_string = fu_superio_device_to_string;
	klass_device->probe = fu_superio_device_probe;
	klass_device->setup = fu_superio_device_setup;
	klass_device->prepare_firmware = fu_superio_device_prepare_firmware;
}
