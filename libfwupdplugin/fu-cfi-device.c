/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCfiDevice"

#include "config.h"

#include "fu-cfi-device.h"

/**
 * FuCfiDevice:
 *
 * A chip conforming to the Common Flash Memory Interface, typically a SPI flash chip.
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	gchar *flash_id;
	guint8 cmd_read_id_sz;
	FuCfiDeviceCmd cmds[FU_CFI_DEVICE_CMD_LAST];
} FuCfiDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCfiDevice, fu_cfi_device, FU_TYPE_DEVICE)
enum { PROP_0, PROP_FLASH_ID, PROP_LAST };

#define GET_PRIVATE(o) (fu_cfi_device_get_instance_private(o))

static const gchar *
fu_cfi_device_cmd_to_string(FuCfiDeviceCmd cmd)
{
	if (cmd == FU_CFI_DEVICE_CMD_READ_ID)
		return "CfiDeviceCmdReadId";
	if (cmd == FU_CFI_DEVICE_CMD_PAGE_PROG)
		return "CfiDeviceCmdPageProg";
	if (cmd == FU_CFI_DEVICE_CMD_CHIP_ERASE)
		return "CfiDeviceCmdChipErase";
	if (cmd == FU_CFI_DEVICE_CMD_READ_DATA)
		return "CfiDeviceCmdReadData";
	if (cmd == FU_CFI_DEVICE_CMD_READ_STATUS)
		return "CfiDeviceCmdReadStatus";
	if (cmd == FU_CFI_DEVICE_CMD_SECTOR_ERASE)
		return "CfiDeviceCmdSectorErase";
	if (cmd == FU_CFI_DEVICE_CMD_WRITE_EN)
		return "CfiDeviceCmdWriteEn";
	if (cmd == FU_CFI_DEVICE_CMD_WRITE_STATUS)
		return "CfiDeviceCmdWriteStatus";
	return NULL;
}

/**
 * fu_cfi_device_get_size:
 * @self: a #FuCfiDevice
 *
 * Gets the chip maximum size.
 *
 * This is typically set with the `FirmwareSizeMax` quirk key.
 *
 * Returns: size in bytes, or 0 if unknown
 *
 * Since: 1.7.1
 **/
guint64
fu_cfi_device_get_size(FuCfiDevice *self)
{
	g_return_val_if_fail(FU_IS_CFI_DEVICE(self), G_MAXUINT64);
	return fu_device_get_firmware_size_max(FU_DEVICE(self));
}

/**
 * fu_cfi_device_set_size:
 * @self: a #FuCfiDevice
 * @size: maximum size in bytes, or 0 if unknown
 *
 * Sets the chip maximum size.
 *
 * Since: 1.7.1
 **/
void
fu_cfi_device_set_size(FuCfiDevice *self, guint64 size)
{
	g_return_if_fail(FU_IS_CFI_DEVICE(self));
	fu_device_set_firmware_size_max(FU_DEVICE(self), size);
}

/**
 * fu_cfi_device_get_flash_id:
 * @self: a #FuCfiDevice
 *
 * Gets the chip ID used to identify the device.
 *
 * Returns: the ID, or %NULL
 *
 * Since: 1.7.1
 **/
const gchar *
fu_cfi_device_get_flash_id(FuCfiDevice *self)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFI_DEVICE(self), NULL);
	return priv->flash_id;
}

/**
 * fu_cfi_device_set_flash_id:
 * @self: a #FuCfiDevice
 * @flash_id: (nullable): The chip ID
 *
 * Sets the chip ID used to identify the device.
 *
 * Since: 1.7.1
 **/
void
fu_cfi_device_set_flash_id(FuCfiDevice *self, const gchar *flash_id)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFI_DEVICE(self));
	if (g_strcmp0(flash_id, priv->flash_id) == 0)
		return;
	g_free(priv->flash_id);
	priv->flash_id = g_strdup(flash_id);
}

static void
fu_cfi_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuCfiDevice *self = FU_CFI_DEVICE(object);
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_FLASH_ID:
		g_value_set_object(value, priv->flash_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_cfi_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuCfiDevice *self = FU_CFI_DEVICE(object);
	switch (prop_id) {
	case PROP_FLASH_ID:
		fu_cfi_device_set_flash_id(self, g_value_get_string(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_cfi_device_finalize(GObject *object)
{
	FuCfiDevice *self = FU_CFI_DEVICE(object);
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	g_free(priv->flash_id);
	G_OBJECT_CLASS(fu_cfi_device_parent_class)->finalize(object);
}

/* returns at most 4 chars from the ID, or %NULL if no different from existing ID */
static gchar *
fu_cfi_device_get_flash_id_jedec(FuCfiDevice *self)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->flash_id == NULL)
		return NULL;
	if (strlen(priv->flash_id) <= 4)
		return NULL;
	return g_strndup(priv->flash_id, 4);
}

static gboolean
fu_cfi_device_probe(FuDevice *device, GError **error)
{
	FuCfiDevice *self = FU_CFI_DEVICE(device);
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);

	/* load the parameters from quirks */
	if (priv->flash_id != NULL) {
		g_autofree gchar *flash_id_jedec = NULL;
		g_autofree gchar *instance_id0 = NULL;
		g_autofree gchar *instance_id1 = NULL;

		/* least specific so adding first */
		flash_id_jedec = fu_cfi_device_get_flash_id_jedec(self);
		if (flash_id_jedec != NULL) {
			instance_id1 = g_strdup_printf("CFI\\FLASHID_%s", flash_id_jedec);
			fu_device_add_instance_id_full(FU_DEVICE(self),
						       instance_id1,
						       FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
		}

		/* this is most specific and can override keys of instance_id1 */
		instance_id0 = g_strdup_printf("CFI\\FLASHID_%s", priv->flash_id);
		fu_device_add_instance_id_full(FU_DEVICE(self),
					       instance_id0,
					       FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);
	}

	/* success */
	return TRUE;
}

/**
 * fu_cfi_device_get_cmd:
 * @self: a #FuCfiDevice
 * @cmd: a #FuCfiDeviceCmd, e.g. %FU_CFI_DEVICE_CMD_CHIP_ERASE
 * @value: the API command value to use
 * @error: (nullable): optional return location for an error
 *
 * Gets the self vendor code.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.7.1
 **/
gboolean
fu_cfi_device_get_cmd(FuCfiDevice *self, FuCfiDeviceCmd cmd, guint8 *value, GError **error)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_CFI_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (cmd >= FU_CFI_DEVICE_CMD_LAST) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "CFI cmd invalid");
		return FALSE;
	}
	if (priv->cmds[cmd] == 0x0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "No defined CFI cmd for %s",
			    fu_cfi_device_cmd_to_string(cmd));
		return FALSE;
	}
	if (value != NULL)
		*value = priv->cmds[cmd];
	return TRUE;
}

static gboolean
fu_vli_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuCfiDevice *self = FU_CFI_DEVICE(device);
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	if (g_strcmp0(key, "CfiDeviceCmdReadId") == 0) {
		priv->cmds[FU_CFI_DEVICE_CMD_READ_ID] = fu_common_strtoull(value);
		return TRUE;
	}
	if (g_strcmp0(key, "CfiDeviceCmdReadIdSz") == 0) {
		priv->cmd_read_id_sz = fu_common_strtoull(value);
		return TRUE;
	}
	if (g_strcmp0(key, "CfiDeviceCmdChipErase") == 0) {
		priv->cmds[FU_CFI_DEVICE_CMD_CHIP_ERASE] = fu_common_strtoull(value);
		return TRUE;
	}
	if (g_strcmp0(key, "CfiDeviceCmdSectorErase") == 0) {
		priv->cmds[FU_CFI_DEVICE_CMD_SECTOR_ERASE] = fu_common_strtoull(value);
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_cfi_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCfiDevice *self = FU_CFI_DEVICE(device);
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	fu_common_string_append_kv(str, idt, "FlashId", priv->flash_id);
	for (guint i = 0; i < FU_CFI_DEVICE_CMD_LAST; i++) {
		fu_common_string_append_kx(str, idt, fu_cfi_device_cmd_to_string(i), priv->cmds[i]);
	}
}

static void
fu_cfi_device_init(FuCfiDevice *self)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	priv->cmds[FU_CFI_DEVICE_CMD_WRITE_STATUS] = 0x01;
	priv->cmds[FU_CFI_DEVICE_CMD_PAGE_PROG] = 0x02;
	priv->cmds[FU_CFI_DEVICE_CMD_READ_DATA] = 0x03;
	priv->cmds[FU_CFI_DEVICE_CMD_READ_STATUS] = 0x05;
	priv->cmds[FU_CFI_DEVICE_CMD_WRITE_EN] = 0x06;
	priv->cmds[FU_CFI_DEVICE_CMD_SECTOR_ERASE] = 0x20;
	priv->cmds[FU_CFI_DEVICE_CMD_CHIP_ERASE] = 0x60;
	priv->cmds[FU_CFI_DEVICE_CMD_READ_ID] = 0x9f;
	fu_device_set_summary(FU_DEVICE(self), "CFI flash chip");
}

static void
fu_cfi_device_class_init(FuCfiDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_cfi_device_finalize;
	object_class->get_property = fu_cfi_device_get_property;
	object_class->set_property = fu_cfi_device_set_property;
	klass_device->probe = fu_cfi_device_probe;
	klass_device->to_string = fu_cfi_device_to_string;
	klass_device->set_quirk_kv = fu_vli_device_set_quirk_kv;

	pspec = g_param_spec_string("flash-id",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FLASH_ID, pspec);
}

/**
 * fu_cfi_device_new:
 * @ctx: a #FuContext
 *
 * Creates a new #FuCfiDevice.
 *
 * Returns: (transfer full): a #FuCfiDevice
 *
 * Since: 1.7.1
 **/
FuCfiDevice *
fu_cfi_device_new(FuContext *ctx, const gchar *flash_id)
{
	return g_object_new(FU_TYPE_CFI_DEVICE, "context", ctx, "flash-id", flash_id, NULL);
}
