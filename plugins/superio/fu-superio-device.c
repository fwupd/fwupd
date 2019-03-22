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

/* unknown source, IT87 only */
#define SIO_CMD_EC_GET_NAME_STR		0x92
#define SIO_CMD_EC_GET_VERSION_STR	0x93

typedef struct
{
	gint			 fd;
	gchar			*chipset;
	guint16			 port;
	guint16			 pm1_iobad0;
	guint16			 pm1_iobad1;
	guint16			 id;
} FuSuperioDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuSuperioDevice, fu_superio_device, FU_TYPE_DEVICE)

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
	if (!fu_superio_outb (priv->fd, priv->port, addr, error))
		return FALSE;
	if (!fu_superio_inb (priv->fd, priv->port + 1, data, error))
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
	if (!fu_superio_outb (priv->fd, priv->port, addr, error))
		return FALSE;
	if (!fu_superio_outb (priv->fd, priv->port + 1, data, error))
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
fu_superio_device_to_string (FuDevice *device, GString *str)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	g_string_append (str, "  FuSuperioDevice:\n");
	g_string_append_printf (str, "    fd:\t\t\t%i\n", priv->fd);
	g_string_append_printf (str, "    chipset:\t\t%s\n", priv->chipset);
	g_string_append_printf (str, "    id:\t\t\t0x%04x\n", (guint) priv->id);
	g_string_append_printf (str, "    port:\t\t0x%04x\n", (guint) priv->port);
	g_string_append_printf (str, "    pm1-iobad0:\t\t0x%04x\n", (guint) priv->pm1_iobad0);
	g_string_append_printf (str, "    pm1-iobad1:\t\t0x%04x\n", (guint) priv->pm1_iobad1);
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
		if (!fu_superio_inb (priv->fd, priv->pm1_iobad1, &status, error))
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
	return fu_superio_inb (priv->fd, priv->pm1_iobad0, data, error);
}

gboolean
fu_superio_device_ec_write0 (FuSuperioDevice *self, guint8 data, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	if (!fu_superio_device_wait_for (self, SIO_STATUS_EC_IBF, FALSE, error))
		return FALSE;
	return fu_superio_outb (priv->fd, priv->pm1_iobad0, data, error);
}

gboolean
fu_superio_device_ec_write1 (FuSuperioDevice *self, guint8 data, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	if (!fu_superio_device_wait_for (self, SIO_STATUS_EC_IBF, FALSE, error))
		return FALSE;
	return fu_superio_outb (priv->fd, priv->pm1_iobad1, data, error);
}

static gboolean
fu_superio_device_ec_flush (FuSuperioDevice *self, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	guint8 status = 0x00;
	g_autoptr(GTimer) timer = g_timer_new ();
	do {
		guint8 unused = 0;
		if (!fu_superio_inb (priv->fd, priv->pm1_iobad1, &status, error))
			return FALSE;
		if ((status & SIO_STATUS_EC_OBF) == 0)
			break;
		if (!fu_superio_inb (priv->fd, priv->pm1_iobad0, &unused, error))
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

static gchar *
fu_superio_device_ec_get_str (FuSuperioDevice *self, guint8 idx, GError **error)
{
	GString *str = g_string_new (NULL);
	if (!fu_superio_device_ec_write1 (self, idx, error))
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
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);

	/* open device */
	priv->fd = g_open (fu_device_get_physical_id (device), O_RDWR);
	if (priv->fd < 0) {
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
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	g_autofree gchar *devid = NULL;

	/* use the chipset name as the logical ID and for the GUID */
	fu_device_set_logical_id (device, priv->chipset);
	devid = g_strdup_printf ("SuperIO-%s", priv->chipset);
	fu_device_add_instance_id (device, devid);
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
	fu_device_set_firmware_size (FU_DEVICE (self), ((guint32) size_tmp) << 10);

	/* get EC strings */
	name = fu_superio_device_ec_get_str (self, SIO_CMD_EC_GET_NAME_STR, error);
	if (name == NULL) {
		g_prefix_error (error, "failed to get EC name: ");
		return FALSE;
	}
	fu_device_set_name (FU_DEVICE (self), name);
	version = fu_superio_device_ec_get_str (self, SIO_CMD_EC_GET_VERSION_STR, error);
	if (version == NULL) {
		g_prefix_error (error, "failed to get EC version: ");
		return FALSE;
	}
	fu_device_set_version (FU_DEVICE (self), version);
	return TRUE;
}

static gboolean
fu_superio_device_it89xx_read_ec_register (FuSuperioDevice *self,
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
fu_superio_device_it89xx_ec_size (FuSuperioDevice *self, GError **error)
{
	guint8 tmp = 0;

	/* not sure why we can't just use SIO_LDNxx_IDX_CHIPID1,
	 * but lets do the same as the vendor flash tool... */
	if (!fu_superio_device_it89xx_read_ec_register (self,
							GCTRL_ECHIPID1,
							&tmp,
							error))
		return FALSE;
	if (tmp == 0x85) {
		g_warning ("possibly IT85xx class device");
		fu_device_set_firmware_size (FU_DEVICE (self), 0x20000);
		return TRUE;
	}

	/* can't we just use SIO_LDNxx_IDX_CHIPVER... */
	if (!fu_superio_device_it89xx_read_ec_register (self,
							GCTRL_ECHIPVER,
							&tmp,
							error))
		return FALSE;
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

	/* get size from the EC */
	if (!fu_superio_device_it89xx_ec_size (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_superio_device_setup (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	guint8 tmp = 0x0;

	/* check port is valid */
	if (!fu_superio_inb (priv->fd, priv->pm1_iobad0, &tmp, error))
		return FALSE;
	if (tmp != 0xff) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "check port!");
		return FALSE;
	}

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

	/* IT85xx */
	if (priv->id >> 8 == 0x85) {
		if (!fu_superio_device_setup_it85xx (self, error))
			return FALSE;
	}

	/* IT89xx */
	if (priv->id >> 8 == 0x89) {
		if (!fu_superio_device_setup_it89xx (self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_superio_device_attach (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);

	/* re-enable HOSTWA -- use 0xfd for LCFC */
	if (!fu_superio_device_ec_write1 (self, 0xfc, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_superio_device_detach (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	guint8 tmp = 0x00;

	/* turn off HOSTWA bit, keeping HSEMIE and HSEMW high */
	if (!fu_superio_device_ec_write1 (self, 0xdc, error))
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
	return TRUE;
}

static gboolean
fu_superio_device_close (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	FuSuperioDevicePrivate *priv = GET_PRIVATE (self);
	if (!g_close (priv->fd, error))
		return FALSE;
	priv->fd = 0;
	return TRUE;
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
	klass_device->open = fu_superio_device_open;
	klass_device->attach = fu_superio_device_attach;
	klass_device->detach = fu_superio_device_detach;
	klass_device->probe = fu_superio_device_probe;
	klass_device->setup = fu_superio_device_setup;
	klass_device->close = fu_superio_device_close;
}
