/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCfiDevice"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-cfi-device.h"
#include "fu-dump.h"
#include "fu-mem.h"
#include "fu-quirks.h"
#include "fu-string.h"

/**
 * FuCfiDevice:
 *
 * A chip conforming to the Common Flash Memory Interface, typically a SPI flash chip.
 *
 * Where required, the quirks instance IDs will be added in ->setup().
 *
 * The defaults are set as follows, and can be overridden in quirk files:
 *
 * * `PageSize`: 0x100
 * * `SectorSize`: 0x1000
 * * `BlockSize`: 0x10000
 *
 * See also: [class@FuDevice]
 */

typedef struct {
	gchar *flash_id;
	guint8 cmd_read_id_sz;
	guint32 page_size;
	guint32 sector_size;
	guint32 block_size;
	FuCfiDeviceCmd cmds[FU_CFI_DEVICE_CMD_LAST];
} FuCfiDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCfiDevice, fu_cfi_device, FU_TYPE_DEVICE)
enum { PROP_0, PROP_FLASH_ID, PROP_LAST };

#define GET_PRIVATE(o) (fu_cfi_device_get_instance_private(o))

#define FU_CFI_DEVICE_PAGE_SIZE_DEFAULT	  0x100
#define FU_CFI_DEVICE_SECTOR_SIZE_DEFAULT 0x1000
#define FU_CFI_DEVICE_BLOCK_SIZE_DEFAULT  0x10000

static const gchar *
fu_cfi_device_cmd_to_string(FuCfiDeviceCmd cmd)
{
	if (cmd == FU_CFI_DEVICE_CMD_READ_ID)
		return "ReadId";
	if (cmd == FU_CFI_DEVICE_CMD_PAGE_PROG)
		return "PageProg";
	if (cmd == FU_CFI_DEVICE_CMD_CHIP_ERASE)
		return "ChipErase";
	if (cmd == FU_CFI_DEVICE_CMD_READ_DATA)
		return "ReadData";
	if (cmd == FU_CFI_DEVICE_CMD_READ_STATUS)
		return "ReadStatus";
	if (cmd == FU_CFI_DEVICE_CMD_SECTOR_ERASE)
		return "SectorErase";
	if (cmd == FU_CFI_DEVICE_CMD_WRITE_EN)
		return "WriteEn";
	if (cmd == FU_CFI_DEVICE_CMD_WRITE_STATUS)
		return "WriteStatus";
	if (cmd == FU_CFI_DEVICE_CMD_BLOCK_ERASE)
		return "BlockErase";
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

typedef struct {
	guint8 mask;
	guint8 value;
} FuCfiDeviceHelper;

static gboolean
fu_cfi_device_wait_for_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuCfiDeviceHelper *helper = (FuCfiDeviceHelper *)user_data;
	FuCfiDevice *self = FU_CFI_DEVICE(device);
	guint8 buf[2] = {0x0};
	g_autoptr(FuDeviceLocker) cslocker = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* enable chip */
	cslocker = fu_cfi_device_chip_select_locker_new(self, error);
	if (cslocker == NULL)
		return FALSE;
	if (!fu_cfi_device_get_cmd(self, FU_CFI_DEVICE_CMD_READ_STATUS, &buf[0], error))
		return FALSE;
	if (!fu_cfi_device_send_command(self,
					buf,
					sizeof(buf),
					buf,
					sizeof(buf),
					progress,
					error)) {
		g_prefix_error(error, "failed to want to status: ");
		return FALSE;
	}
	if ((buf[0x1] & helper->mask) != helper->value) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "wanted 0x%x, got 0x%x",
			    helper->value,
			    buf[0x1] & helper->mask);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cfi_device_wait_for_status(FuCfiDevice *self,
			      guint8 mask,
			      guint8 value,
			      guint count,
			      guint delay,
			      GError **error)
{
	FuCfiDeviceHelper helper = {.mask = mask, .value = value};
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_cfi_device_wait_for_status_cb,
				    count,
				    delay,
				    &helper,
				    error);
}

static gboolean
fu_cfi_device_read_jedec(FuCfiDevice *self, GError **error)
{
	guint8 buf_res[] = {0x9F};
	guint8 buf_req[3] = {0x0};
	g_autoptr(FuDeviceLocker) cslocker = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GString) flash_id = g_string_new(NULL);

	/* enable chip */
	cslocker = fu_cfi_device_chip_select_locker_new(self, error);
	if (cslocker == NULL)
		return FALSE;

	/* read JEDEC ID */
	if (!fu_cfi_device_send_command(self,
					buf_res,
					sizeof(buf_res),
					buf_req,
					sizeof(buf_req),
					progress,
					error)) {
		g_prefix_error(error, "failed to request JEDEC ID: ");
		return FALSE;
	}
	if ((buf_req[0] == 0x0 && buf_req[1] == 0x0 && buf_req[2] == 0x0) ||
	    (buf_req[0] == 0xFF && buf_req[1] == 0xFF && buf_req[2] == 0xFF)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device not detected, flash ID 0x%02X%02X%02X",
			    buf_req[0],
			    buf_req[1],
			    buf_req[2]);
		return FALSE;
	}
	g_string_append_printf(flash_id, "%02X", buf_req[0]);
	g_string_append_printf(flash_id, "%02X", buf_req[1]);
	g_string_append_printf(flash_id, "%02X", buf_req[2]);
	fu_cfi_device_set_flash_id(self, flash_id->str);

	/* success */
	return TRUE;
}

static gboolean
fu_cfi_device_setup(FuDevice *device, GError **error)
{
	gsize flash_idsz = 0;
	FuCfiDevice *self = FU_CFI_DEVICE(device);
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);

	/* setup SPI chip */
	if (priv->flash_id == NULL) {
		if (!fu_cfi_device_read_jedec(self, error))
			return FALSE;
	}

	/* sanity check */
	if (priv->flash_id != NULL)
		flash_idsz = strlen(priv->flash_id);
	if (flash_idsz == 0 || flash_idsz % 2 != 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "not a valid flash-id");
		return FALSE;
	}

	/* typically this will add quirk strings of 2, 4, then 6 bytes */
	for (guint i = 0; i < flash_idsz; i += 2) {
		g_autofree gchar *flash_id = g_strndup(priv->flash_id, i + 2);
		fu_device_add_instance_str(device, "FLASHID", flash_id);
		if (!fu_device_build_instance_id_quirk(device, error, "CFI", "FLASHID", NULL))
			return FALSE;
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

/**
 * fu_cfi_device_get_page_size:
 * @self: a #FuCfiDevice
 *
 * Gets the chip page size. This is typically the largest writable block size.
 *
 * This is typically set with the `CfiDevicePageSize` quirk key.
 *
 * Returns: page size in bytes
 *
 * Since: 1.7.3
 **/
guint32
fu_cfi_device_get_page_size(FuCfiDevice *self)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFI_DEVICE(self), G_MAXUINT32);
	return priv->page_size;
}

/**
 * fu_cfi_device_set_page_size:
 * @self: a #FuCfiDevice
 * @page_size: page size in bytes, or 0 if unknown
 *
 * Sets the chip page size. This is typically the largest writable block size.
 *
 * Since: 1.7.3
 **/
void
fu_cfi_device_set_page_size(FuCfiDevice *self, guint32 page_size)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFI_DEVICE(self));
	priv->page_size = page_size;
}

/**
 * fu_cfi_device_get_sector_size:
 * @self: a #FuCfiDevice
 *
 * Gets the chip sector size. This is typically the smallest erasable page size.
 *
 * This is typically set with the `CfiDeviceSectorSize` quirk key.
 *
 * Returns: sector size in bytes
 *
 * Since: 1.7.3
 **/
guint32
fu_cfi_device_get_sector_size(FuCfiDevice *self)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFI_DEVICE(self), G_MAXUINT32);
	return priv->sector_size;
}

/**
 * fu_cfi_device_set_block_size:
 * @self: a #FuCfiDevice
 * @block_size: block size in bytes, or 0 if unknown
 *
 * Sets the chip block size. This is typically the largest erasable chunk size.
 *
 * Since: 1.7.4
 **/
void
fu_cfi_device_set_block_size(FuCfiDevice *self, guint32 block_size)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFI_DEVICE(self));
	priv->block_size = block_size;
}

/**
 * fu_cfi_device_get_block_size:
 * @self: a #FuCfiDevice
 *
 * Gets the chip block size. This is typically the largest erasable block size.
 *
 * This is typically set with the `CfiDeviceBlockSize` quirk key.
 *
 * Returns: block size in bytes
 *
 * Since: 1.7.4
 **/
guint32
fu_cfi_device_get_block_size(FuCfiDevice *self)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CFI_DEVICE(self), G_MAXUINT32);
	return priv->block_size;
}

/**
 * fu_cfi_device_set_sector_size:
 * @self: a #FuCfiDevice
 * @sector_size: sector size in bytes, or 0 if unknown
 *
 * Sets the chip sector size. This is typically the smallest erasable page size.
 *
 * Since: 1.7.3
 **/
void
fu_cfi_device_set_sector_size(FuCfiDevice *self, guint32 sector_size)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CFI_DEVICE(self));
	priv->sector_size = sector_size;
}

static gboolean
fu_cfi_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuCfiDevice *self = FU_CFI_DEVICE(device);
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	guint64 tmp;

	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_CMD_READ_ID) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->cmds[FU_CFI_DEVICE_CMD_READ_ID] = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_CMD_READ_ID_SZ) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->cmd_read_id_sz = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_CMD_CHIP_ERASE) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->cmds[FU_CFI_DEVICE_CMD_CHIP_ERASE] = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_CMD_BLOCK_ERASE) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->cmds[FU_CFI_DEVICE_CMD_BLOCK_ERASE] = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_CMD_SECTOR_ERASE) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->cmds[FU_CFI_DEVICE_CMD_SECTOR_ERASE] = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_CMD_WRITE_STATUS) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->cmds[FU_CFI_DEVICE_CMD_WRITE_STATUS] = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_CMD_PAGE_PROG) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->cmds[FU_CFI_DEVICE_CMD_PAGE_PROG] = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_CMD_READ_DATA) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->cmds[FU_CFI_DEVICE_CMD_READ_DATA] = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_CMD_READ_STATUS) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->cmds[FU_CFI_DEVICE_CMD_READ_STATUS] = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_CMD_WRITE_EN) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		priv->cmds[FU_CFI_DEVICE_CMD_WRITE_EN] = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_PAGE_SIZE) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		priv->page_size = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_SECTOR_SIZE) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		priv->sector_size = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, FU_QUIRKS_CFI_DEVICE_BLOCK_SIZE) == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		priv->block_size = tmp;
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
	fu_string_append(str, idt, "FlashId", priv->flash_id);
	for (guint i = 0; i < FU_CFI_DEVICE_CMD_LAST; i++) {
		fu_string_append_kx(str, idt, fu_cfi_device_cmd_to_string(i), priv->cmds[i]);
	}
	if (priv->page_size > 0)
		fu_string_append_kx(str, idt, "PageSize", priv->page_size);
	if (priv->sector_size > 0)
		fu_string_append_kx(str, idt, "SectorSize", priv->sector_size);
	if (priv->block_size > 0)
		fu_string_append_kx(str, idt, "BlockSize", priv->block_size);
}

/**
 * fu_cfi_device_send_command:
 * @self: a #FuCfiDevice
 * @wbuf: buffer
 * @wbufsz: size of @wbuf, possibly zero
 * @rbuf: buffer
 * @rbufsz: size of @rbuf, possibly zero
 * @progress: a #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Sends an unspecified command stream to the CFI device.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.8.14
 **/
gboolean
fu_cfi_device_send_command(FuCfiDevice *self,
			   const guint8 *wbuf,
			   gsize wbufsz,
			   guint8 *rbuf,
			   gsize rbufsz,
			   FuProgress *progress,
			   GError **error)
{
	FuCfiDeviceClass *klass = FU_CFI_DEVICE_GET_CLASS(self);
	g_return_val_if_fail(FU_IS_CFI_DEVICE(self), FALSE);
	g_return_val_if_fail(FU_IS_PROGRESS(progress), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (klass->send_command == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "send_command is not implemented on this device");
		return FALSE;
	}
	if (wbufsz > 0)
		fu_dump_raw(G_LOG_DOMAIN, "SPI write", wbuf, wbufsz);
	if (!klass->send_command(self, wbuf, wbufsz, rbuf, rbufsz, progress, error))
		return FALSE;
	if (rbufsz > 0)
		fu_dump_raw(G_LOG_DOMAIN, "SPI read", rbuf, rbufsz);
	return TRUE;
}

/**
 * fu_cfi_device_chip_select:
 * @self: a #FuCfiDevice
 * @value: boolean
 * @error: (nullable): optional return location for an error
 *
 * Sets the chip select value.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.8.0
 **/
gboolean
fu_cfi_device_chip_select(FuCfiDevice *self, gboolean value, GError **error)
{
	FuCfiDeviceClass *klass = FU_CFI_DEVICE_GET_CLASS(self);
	g_return_val_if_fail(FU_IS_CFI_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (klass->chip_select == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "chip select is not implemented on this device");
		return FALSE;
	}
	return klass->chip_select(self, value, error);
}

static gboolean
fu_cfi_device_chip_select_assert(GObject *device, GError **error)
{
	return fu_cfi_device_chip_select(FU_CFI_DEVICE(device), TRUE, error);
}

static gboolean
fu_cfi_device_chip_select_deassert(GObject *device, GError **error)
{
	return fu_cfi_device_chip_select(FU_CFI_DEVICE(device), FALSE, error);
}

/**
 * fu_cfi_device_chip_select_locker_new:
 * @self: a #FuCfiDevice
 *
 * Creates a custom device locker that asserts and deasserts the chip select signal.
 *
 * Returns: (transfer full): (nullable): a #FuDeviceLocker
 *
 * Since: 1.8.0
 **/
FuDeviceLocker *
fu_cfi_device_chip_select_locker_new(FuCfiDevice *self, GError **error)
{
	g_return_val_if_fail(FU_IS_CFI_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fu_device_locker_new_full(self,
					 fu_cfi_device_chip_select_assert,
					 fu_cfi_device_chip_select_deassert,
					 error);
}

static gboolean
fu_cfi_device_write_enable(FuCfiDevice *self, GError **error)
{
	guint8 buf[1] = {0x0};
	g_autoptr(FuDeviceLocker) cslocker = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* write enable */
	if (!fu_cfi_device_get_cmd(self, FU_CFI_DEVICE_CMD_WRITE_EN, &buf[0], error))
		return FALSE;
	cslocker = fu_cfi_device_chip_select_locker_new(self, error);
	if (cslocker == NULL)
		return FALSE;
	if (!fu_cfi_device_send_command(self, buf, sizeof(buf), NULL, 0, progress, error))
		return FALSE;
	if (!fu_device_locker_close(cslocker, error))
		return FALSE;

	/* check that WEL is now set */
	return fu_cfi_device_wait_for_status(self, 0b10, 0b10, 10, 5, error);
}

static gboolean
fu_cfi_device_chip_erase(FuCfiDevice *self, GError **error)
{
	guint8 buf[] = {0x0};
	g_autoptr(FuDeviceLocker) cslocker = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* enable chip */
	cslocker = fu_cfi_device_chip_select_locker_new(self, error);
	if (cslocker == NULL)
		return FALSE;

	/* erase */
	if (!fu_cfi_device_get_cmd(self, FU_CFI_DEVICE_CMD_CHIP_ERASE, &buf[0], error))
		return FALSE;
	if (!fu_cfi_device_send_command(self, buf, sizeof(buf), NULL, 0, progress, error))
		return FALSE;
	if (!fu_device_locker_close(cslocker, error))
		return FALSE;

	/* poll Read Status register BUSY */
	return fu_cfi_device_wait_for_status(self, 0b1, 0b0, 100, 500, error);
}

static gboolean
fu_cfi_device_write_page(FuCfiDevice *self, FuChunk *page, FuProgress *progress, GError **error)
{
	guint8 cmd = 0x0;
	g_autoptr(FuDeviceLocker) cslocker = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	if (!fu_cfi_device_write_enable(self, error))
		return FALSE;

	cslocker = fu_cfi_device_chip_select_locker_new(self, error);
	if (cslocker == NULL)
		return FALSE;

	/* cmd, 24 bit starting address, then data */
	if (!fu_cfi_device_get_cmd(self, FU_CFI_DEVICE_CMD_PAGE_PROG, &cmd, error))
		return FALSE;
	fu_byte_array_append_uint8(buf, cmd);
	fu_byte_array_append_uint24(buf, fu_chunk_get_address(page), G_BIG_ENDIAN);
	g_byte_array_append(buf, fu_chunk_get_data(page), fu_chunk_get_data_sz(page));
	g_debug("writing page at 0x%x", (guint)fu_chunk_get_address(page));
	if (!fu_cfi_device_send_command(self, buf->data, buf->len, NULL, 0, progress, error))
		return FALSE;
	if (!fu_device_locker_close(cslocker, error))
		return FALSE;

	/* poll Read Status register BUSY */
	return fu_cfi_device_wait_for_status(self, 0b1, 0b0, 100, 50, error);
}

static gboolean
fu_cfi_device_write_pages(FuCfiDevice *self, GPtrArray *pages, FuProgress *progress, GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, pages->len);
	for (guint i = 0; i < pages->len; i++) {
		FuChunk *page = g_ptr_array_index(pages, i);
		if (!fu_cfi_device_write_page(self, page, fu_progress_get_child(progress), error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cfi_device_read_block(FuCfiDevice *self, FuChunk *block, FuProgress *progress, GError **error)
{
	guint8 buf_req[4] = {0x0}; /* cmd, then 24 bit starting address */
	g_autoptr(FuDeviceLocker) cslocker = NULL;

	/* enable chip */
	cslocker = fu_cfi_device_chip_select_locker_new(self, error);
	if (cslocker == NULL)
		return FALSE;
	if (!fu_cfi_device_get_cmd(self, FU_CFI_DEVICE_CMD_READ_DATA, &buf_req[0], error))
		return FALSE;
	fu_memwrite_uint24(buf_req + 0x1, fu_chunk_get_address(block), G_BIG_ENDIAN);
	return fu_cfi_device_send_command(self,
					  buf_req,
					  sizeof(buf_req),
					  fu_chunk_get_data_out(block),
					  fu_chunk_get_data_sz(block),
					  progress,
					  error);
}

static GBytes *
fu_cfi_device_read_firmware(FuCfiDevice *self, gsize bufsz, FuProgress *progress, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) pages = NULL;

	/* progress */
	fu_byte_array_set_size(buf, bufsz, 0x0);
	pages = fu_chunk_array_mutable_new(buf->data,
					   buf->len,
					   0x0,
					   0x0,
					   fu_cfi_device_get_block_size(self));
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, pages->len);
	for (guint i = 0; i < pages->len; i++) {
		FuChunk *block = g_ptr_array_index(pages, i);
		if (!fu_cfi_device_read_block(self, block, fu_progress_get_child(progress), error))
			return NULL;
		fu_progress_step_done(progress);
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static GBytes *
fu_cfi_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuCfiDevice *self = FU_CFI_DEVICE(device);
	gsize bufsz = fu_device_get_firmware_size_max(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open programmer */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return NULL;

	/* sanity check */
	if (bufsz == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "device firmware size not set");
		return NULL;
	}
	return fu_cfi_device_read_firmware(self, bufsz, progress, error);
}

static gboolean
fu_cfi_device_write_firmware(FuDevice *device,
			     FuFirmware *firmware,
			     FuProgress *progress,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuCfiDevice *self = FU_CFI_DEVICE(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_verify = NULL;
	g_autoptr(GPtrArray) pages = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open programmer */
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 5, NULL);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* erase */
	if (!fu_cfi_device_write_enable(self, error)) {
		g_prefix_error(error, "failed to enable writes: ");
		return FALSE;
	}
	if (!fu_cfi_device_chip_erase(self, error)) {
		g_prefix_error(error, "failed to erase: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write each block */
	pages = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, fu_cfi_device_get_page_size(self));
	if (!fu_cfi_device_write_pages(self, pages, fu_progress_get_child(progress), error)) {
		g_prefix_error(error, "failed to write pages: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* verify each block */
	fw_verify = fu_cfi_device_read_firmware(self,
						g_bytes_get_size(fw),
						fu_progress_get_child(progress),
						error);
	if (fw_verify == NULL) {
		g_prefix_error(error, "failed to verify blocks: ");
		return FALSE;
	}
	if (!fu_bytes_compare(fw_verify, fw, error)) {
		g_prefix_error(error, "verify failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_cfi_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_cfi_device_init(FuCfiDevice *self)
{
	FuCfiDevicePrivate *priv = GET_PRIVATE(self);
	priv->page_size = FU_CFI_DEVICE_PAGE_SIZE_DEFAULT;
	priv->sector_size = FU_CFI_DEVICE_SECTOR_SIZE_DEFAULT;
	priv->block_size = FU_CFI_DEVICE_BLOCK_SIZE_DEFAULT;
	priv->cmds[FU_CFI_DEVICE_CMD_WRITE_STATUS] = 0x01;
	priv->cmds[FU_CFI_DEVICE_CMD_PAGE_PROG] = 0x02;
	priv->cmds[FU_CFI_DEVICE_CMD_READ_DATA] = 0x03;
	priv->cmds[FU_CFI_DEVICE_CMD_READ_STATUS] = 0x05;
	priv->cmds[FU_CFI_DEVICE_CMD_WRITE_EN] = 0x06;
	priv->cmds[FU_CFI_DEVICE_CMD_SECTOR_ERASE] = 0x20;
	priv->cmds[FU_CFI_DEVICE_CMD_CHIP_ERASE] = 0x60;
	priv->cmds[FU_CFI_DEVICE_CMD_READ_ID] = 0x9f;
	fu_device_add_protocol(FU_DEVICE(self), "org.jedec.cfi");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_vendor_id(FU_DEVICE(self), "SPI:*");
	fu_device_set_summary(FU_DEVICE(self), "CFI flash chip");
}

static void
fu_cfi_device_constructed(GObject *obj)
{
	FuCfiDevice *self = FU_CFI_DEVICE(obj);
	fu_device_add_instance_id(FU_DEVICE(self), "SPI");
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
	object_class->constructed = fu_cfi_device_constructed;
	klass_device->setup = fu_cfi_device_setup;
	klass_device->to_string = fu_cfi_device_to_string;
	klass_device->set_quirk_kv = fu_cfi_device_set_quirk_kv;
	klass_device->write_firmware = fu_cfi_device_write_firmware;
	klass_device->dump_firmware = fu_cfi_device_dump_firmware;
	klass_device->set_progress = fu_cfi_device_set_progress;

	/**
	 * FuCfiDevice:flash-id:
	 *
	 * The CCI JEDEC flash ID.
	 *
	 * Since: 1.7.1
	 */
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
