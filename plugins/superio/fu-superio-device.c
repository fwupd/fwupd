/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021, TUXEDO Computers GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-superio-common.h"
#include "fu-superio-device.h"

#define FU_PLUGIN_SUPERIO_DEFAULT_TIMEOUT 250 /* ms */

typedef struct {
	gchar *chipset;
	guint timeout_ms;
	guint16 port;
	guint16 data_port;
	guint16 control_port;
	guint16 id;
} FuSuperioDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuSuperioDevice, fu_superio_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_superio_device_get_instance_private(o))

enum { PROP_0, PROP_CHIPSET, PROP_LAST };

gboolean
fu_superio_device_io_read(FuSuperioDevice *self, guint8 addr, guint8 *data, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);

	if (priv->port == 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "port isn't set");
		return FALSE;
	}

	if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(self), priv->port, &addr, 1, error))
		return FALSE;
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(self), priv->port + 1, data, 1, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_superio_device_io_read16(FuSuperioDevice *self, guint8 addr, guint16 *data, GError **error)
{
	guint8 msb;
	guint8 lsb;
	if (!fu_superio_device_io_read(self, addr, &msb, error))
		return FALSE;
	if (!fu_superio_device_io_read(self, addr + 1, &lsb, error))
		return FALSE;
	*data = ((guint16)msb << 8) | (guint16)lsb;
	return TRUE;
}

gboolean
fu_superio_device_io_write(FuSuperioDevice *self, guint8 addr, guint8 data, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);

	if (priv->port == 0) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "port isn't set");
		return FALSE;
	}

	if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(self), priv->port, &addr, 1, error))
		return FALSE;
	if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(self), priv->port + 1, &data, 1, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_superio_device_set_ldn(FuSuperioDevice *self, guint8 ldn, GError **error)
{
	return fu_superio_device_io_write(self, SIO_LDNxx_IDX_LDNSEL, ldn, error);
}

static gboolean
fu_superio_device_regdump(FuSuperioDevice *self, guint8 ldn, GError **error)
{
	const gchar *ldnstr = fu_superio_ldn_to_text(ldn);
	guint8 buf[0xff] = {0x00};
	guint16 iobad0 = 0x0;
	guint16 iobad1 = 0x0;
	g_autoptr(GString) str = g_string_new(NULL);

	/* set LDN */
	if (!fu_superio_device_set_ldn(self, ldn, error))
		return FALSE;
	for (guint i = 0x00; i < 0xff; i++) {
		if (!fu_superio_device_io_read(self, i, &buf[i], error))
			return FALSE;
	}

	/* get the i/o base addresses */
	if (!fu_superio_device_io_read16(self, SIO_LDNxx_IDX_IOBAD0, &iobad0, error))
		return FALSE;
	if (!fu_superio_device_io_read16(self, SIO_LDNxx_IDX_IOBAD1, &iobad1, error))
		return FALSE;

	g_string_append_printf(str, "LDN:0x%02x ", ldn);
	if (iobad0 != 0x0)
		g_string_append_printf(str, "IOBAD0:0x%04x ", iobad0);
	if (iobad1 != 0x0)
		g_string_append_printf(str, "IOBAD1:0x%04x ", iobad1);
	if (ldnstr != NULL)
		g_string_append_printf(str, "(%s)", ldnstr);
	if (g_getenv("FWUPD_SUPERIO_VERBOSE") != NULL)
		fu_dump_raw(G_LOG_DOMAIN, str->str, buf, sizeof(buf));
	return TRUE;
}

static void
fu_superio_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE(device);
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS(fu_superio_device_parent_class)->to_string(device, idt, str);

	fu_string_append(str, idt, "Chipset", priv->chipset);
	fu_string_append_kx(str, idt, "Id", priv->id);
	fu_string_append_kx(str, idt, "Port", priv->port);
	fu_string_append_kx(str, idt, "DataPort", priv->data_port);
	fu_string_append_kx(str, idt, "ControlPort", priv->control_port);
}

static gboolean
fu_superio_device_check_id(FuSuperioDevice *self, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);
	guint16 id_tmp;

	/* no quirk entry */
	if (priv->id == 0x0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "invalid SuperioId");
		return FALSE;
	}

	/* can't check the ID, assume it's correct */
	if (priv->port == 0)
		return TRUE;

	/* check ID, which can be done from any LDN */
	if (!fu_superio_device_io_read16(self, SIO_LDNxx_IDX_CHIPID1, &id_tmp, error))
		return FALSE;
	if (priv->id != id_tmp) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "SuperIO chip not supported, got %04x, expected %04x",
			    (guint)id_tmp,
			    (guint)priv->id);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_superio_device_wait_for(FuSuperioDevice *self, guint8 mask, gboolean set, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GTimer) timer = g_timer_new();
	do {
		guint8 status = 0x00;
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(self),
					  priv->control_port,
					  &status,
					  1,
					  error))
			return FALSE;
		if (g_timer_elapsed(timer, NULL) * 1000.0f > priv->timeout_ms)
			break;
		if (set && (status & mask) != 0)
			return TRUE;
		if (!set && (status & mask) == 0)
			return TRUE;
	} while (TRUE);
	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_TIMED_OUT,
		    "timed out whilst waiting for 0x%02x:%i",
		    mask,
		    set);
	return FALSE;
}

gboolean
fu_superio_device_ec_read_data(FuSuperioDevice *self, guint8 *data, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);
	if (!fu_superio_device_wait_for(self, SIO_STATUS_EC_OBF, TRUE, error))
		return FALSE;
	return fu_udev_device_pread(FU_UDEV_DEVICE(self), priv->data_port, data, 1, error);
}

gboolean
fu_superio_device_ec_write_data(FuSuperioDevice *self, guint8 data, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);
	if (!fu_superio_device_wait_for(self, SIO_STATUS_EC_IBF, FALSE, error))
		return FALSE;
	return fu_udev_device_pwrite(FU_UDEV_DEVICE(self), priv->data_port, &data, 1, error);
}

gboolean
fu_superio_device_ec_write_cmd(FuSuperioDevice *self, guint8 cmd, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);
	if (!fu_superio_device_wait_for(self, SIO_STATUS_EC_IBF, FALSE, error))
		return FALSE;
	return fu_udev_device_pwrite(FU_UDEV_DEVICE(self), priv->control_port, &cmd, 1, error);
}

static gboolean
fu_superio_device_ec_flush(FuSuperioDevice *self, GError **error)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);
	guint8 status = 0x00;
	g_autoptr(GTimer) timer = g_timer_new();
	do {
		guint8 unused = 0;
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(self),
					  priv->control_port,
					  &status,
					  1,
					  error))
			return FALSE;
		if ((status & SIO_STATUS_EC_OBF) == 0)
			break;
		if (!fu_udev_device_pread(FU_UDEV_DEVICE(self), priv->data_port, &unused, 1, error))
			return FALSE;
		if (g_timer_elapsed(timer, NULL) * 1000.f > priv->timeout_ms) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_TIMED_OUT,
					    "timed out whilst waiting for flush");
			return FALSE;
		}
	} while (TRUE);
	return TRUE;
}

gboolean
fu_superio_device_reg_read(FuSuperioDevice *self, guint8 address, guint8 *data, GError **error)
{
	if (!fu_superio_device_ec_write_cmd(self, SIO_CMD_EC_READ, error))
		return FALSE;
	if (!fu_superio_device_ec_write_data(self, address, error))
		return FALSE;
	return fu_superio_device_ec_read_data(self, data, error);
}

gboolean
fu_superio_device_reg_write(FuSuperioDevice *self, guint8 address, guint8 data, GError **error)
{
	if (!fu_superio_device_ec_write_cmd(self, SIO_CMD_EC_WRITE, error))
		return FALSE;
	if (!fu_superio_device_ec_write_data(self, address, error))
		return FALSE;
	return fu_superio_device_ec_write_data(self, data, error);
}

static gboolean
fu_superio_device_probe(FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE(device);
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *devid = NULL;
	g_autofree gchar *name = NULL;

	/* use the chipset name as the logical ID and for the GUID */
	fu_device_set_logical_id(device, priv->chipset);
	devid = g_strdup_printf("SuperIO-%s", priv->chipset);
	fu_device_add_instance_id(device, devid);
	name = g_strdup_printf("SuperIO %s", priv->chipset);
	fu_device_set_name(FU_DEVICE(self), name);
	return TRUE;
}

static gboolean
fu_superio_device_setup(FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE(device);
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);

	/* check ID is correct */
	if (!fu_superio_device_check_id(self, error)) {
		g_prefix_error(error, "failed to probe id: ");
		return FALSE;
	}

	/* discover the data port and control port from PM1 */
	if (priv->data_port == 0 && priv->control_port == 0) {
		/* dump LDNs */
		if (g_getenv("FWUPD_SUPERIO_VERBOSE") != NULL) {
			for (guint j = 0; j < SIO_LDN_LAST; j++) {
				if (!fu_superio_device_regdump(self, j, error))
					return FALSE;
			}
		}

		/* set Power Management I/F Channel 1 LDN */
		if (!fu_superio_device_set_ldn(self, SIO_LDN_PM1, error))
			return FALSE;

		/* get the PM1 IOBAD0 address */
		if (!fu_superio_device_io_read16(self,
						 SIO_LDNxx_IDX_IOBAD0,
						 &priv->data_port,
						 error))
			return FALSE;

		/* get the PM1 IOBAD1 address */
		if (!fu_superio_device_io_read16(self,
						 SIO_LDNxx_IDX_IOBAD1,
						 &priv->control_port,
						 error))
			return FALSE;
	}

	/* sanity check that EC is usable */
	if (!fu_superio_device_wait_for(self, SIO_STATUS_EC_IBF, FALSE, error)) {
		g_prefix_error(error, "sanity check: ");
		return FALSE;
	}

	/* drain */
	if (!fu_superio_device_ec_flush(self, error)) {
		g_prefix_error(error, "failed to flush: ");
		return FALSE;
	}

	/* dump PMC register map */
	if (g_getenv("FWUPD_SUPERIO_VERBOSE") != NULL) {
		guint8 buf[0xff] = {0x00};
		for (guint i = 0x00; i < 0xff; i++) {
			g_autoptr(GError) error_local = NULL;
			if (!fu_superio_device_reg_read(self, i, &buf[i], &error_local)) {
				g_debug("param: 0x%02x = %s", i, error_local->message);
				continue;
			}
		}
		fu_dump_raw(G_LOG_DOMAIN, "EC Registers", buf, sizeof(buf));
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_superio_device_prepare_firmware(FuDevice *device,
				   GBytes *fw,
				   FwupdInstallFlags flags,
				   GError **error)
{
	gsize sz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &sz);
	const guint8 sig1[] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5};
	const guint8 sig2[] = {0x85, 0x12, 0x5a, 0x5a, 0xaa};

	/* find signature -- maybe ignore byte 0x14 too? */
	for (gsize off = 0; off < sz; off += 16) {
		if (memcmp(&buf[off], sig1, sizeof(sig1)) == 0 &&
		    memcmp(&buf[off + 8], sig2, sizeof(sig2)) == 0) {
			g_debug("found signature at 0x%04x", (guint)off);
			return fu_firmware_new_from_bytes(fw);
		}
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "did not detect signature in firmware image");
	return NULL;
}

static void
fu_superio_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE(object);
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_CHIPSET:
		g_value_set_string(value, priv->chipset);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_superio_device_set_property(GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE(object);
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_CHIPSET:
		g_free(priv->chipset);
		priv->chipset = g_value_dup_string(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static gboolean
fu_superio_device_set_quirk_kv(FuDevice *device,
			       const gchar *key,
			       const gchar *value,
			       GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE(device);
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);
	guint64 tmp = 0;

	if (g_strcmp0(key, "SuperioAutoloadAction") == 0)
		return TRUE;
	if (g_strcmp0(key, "SuperioId") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		priv->id = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "SuperioPort") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		priv->port = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "SuperioControlPort") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		priv->control_port = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "SuperioDataPort") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		priv->data_port = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "SuperioTimeout") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT, error))
			return FALSE;
		priv->timeout_ms = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

static void
fu_superio_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_superio_device_init(FuSuperioDevice *self)
{
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);

	priv->timeout_ms = FU_PLUGIN_SUPERIO_DEFAULT_TIMEOUT;

	fu_device_set_physical_id(FU_DEVICE(self), "/dev/port");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.ite.superio");
	fu_device_set_summary(FU_DEVICE(self), "Embedded controller");
	fu_device_add_icon(FU_DEVICE(self), "computer");
}

static void
fu_superio_device_finalize(GObject *object)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE(object);
	FuSuperioDevicePrivate *priv = GET_PRIVATE(self);
	g_free(priv->chipset);

	G_OBJECT_CLASS(fu_superio_device_parent_class)->finalize(object);
}

static void
fu_superio_device_class_init(FuSuperioDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	/* properties */
	object_class->get_property = fu_superio_device_get_property;
	object_class->set_property = fu_superio_device_set_property;

	/**
	 * FuSuperioDevice:chipset:
	 *
	 * The SuperIO chipset name being used.
	 */
	pspec =
	    g_param_spec_string("chipset",
				NULL,
				NULL,
				NULL,
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_CHIPSET, pspec);

	object_class->finalize = fu_superio_device_finalize;
	klass_device->to_string = fu_superio_device_to_string;
	klass_device->set_quirk_kv = fu_superio_device_set_quirk_kv;
	klass_device->probe = fu_superio_device_probe;
	klass_device->setup = fu_superio_device_setup;
	klass_device->prepare_firmware = fu_superio_device_prepare_firmware;
	klass_device->set_progress = fu_superio_device_set_progress;
}
