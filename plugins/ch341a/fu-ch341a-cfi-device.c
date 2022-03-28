/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-ch341a-cfi-device.h"
#include "fu-ch341a-device.h"

struct _FuCh341aCfiDevice {
	FuCfiDevice parent_instance;
};

G_DEFINE_TYPE(FuCh341aCfiDevice, fu_ch341a_cfi_device, FU_TYPE_CFI_DEVICE)

#define CH341A_PAYLOAD_SIZE 0x1A

static gboolean
fu_ch341a_cfi_device_chip_select(FuCfiDevice *self, gboolean value, GError **error)
{
	FuCh341aDevice *proxy = FU_CH341A_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	return fu_ch341a_device_chip_select(proxy, value, error);
}

typedef struct {
	guint8 mask;
	guint8 value;
} FuCh341aCfiDeviceHelper;

static gboolean
fu_ch341a_cfi_device_wait_for_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuCh341aCfiDeviceHelper *helper = (FuCh341aCfiDeviceHelper *)user_data;
	FuCh341aCfiDevice *self = FU_CH341A_CFI_DEVICE(device);
	FuCh341aDevice *proxy = FU_CH341A_DEVICE(fu_device_get_proxy(device));
	guint8 buf[2] = {0x0};
	g_autoptr(FuDeviceLocker) cslocker = NULL;

	/* enable chip */
	cslocker = fu_cfi_device_chip_select_locker_new(FU_CFI_DEVICE(self), error);
	if (cslocker == NULL)
		return FALSE;
	if (!fu_cfi_device_get_cmd(FU_CFI_DEVICE(self),
				   FU_CFI_DEVICE_CMD_READ_STATUS,
				   &buf[0],
				   error))
		return FALSE;
	if (!fu_ch341a_device_spi_transfer(proxy, buf, sizeof(buf), error)) {
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
fu_ch341a_cfi_device_wait_for_status(FuCh341aCfiDevice *self,
				     guint8 mask,
				     guint8 value,
				     guint count,
				     guint delay,
				     GError **error)
{
	FuCh341aCfiDeviceHelper helper = {.mask = mask, .value = value};
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_ch341a_cfi_device_wait_for_status_cb,
				    count,
				    delay,
				    &helper,
				    error);
}

static gboolean
fu_ch341a_cfi_device_read_jedec(FuCh341aCfiDevice *self, GError **error)
{
	FuCh341aDevice *proxy = FU_CH341A_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	guint8 buf[CH341A_PAYLOAD_SIZE] = {0x9F};
	g_autoptr(FuDeviceLocker) cslocker = NULL;
	g_autoptr(GString) flash_id = g_string_new(NULL);

	/* enable chip */
	cslocker = fu_cfi_device_chip_select_locker_new(FU_CFI_DEVICE(self), error);
	if (cslocker == NULL)
		return FALSE;

	/* read JEDEC ID */
	if (!fu_ch341a_device_spi_transfer(proxy, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to request JEDEC ID: ");
		return FALSE;
	}
	if (buf[1] == 0x0 && buf[2] == 0x0 && buf[3] == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "flash ID non-valid");
		return FALSE;
	}
	if (buf[1] == 0xFF && buf[2] == 0xFF && buf[3] == 0xFF) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device not detected");
		return FALSE;
	}
	g_string_append_printf(flash_id, "%02X", buf[1]);
	g_string_append_printf(flash_id, "%02X", buf[2]);
	g_string_append_printf(flash_id, "%02X", buf[3]);
	fu_cfi_device_set_flash_id(FU_CFI_DEVICE(self), flash_id->str);

	/* success */
	return TRUE;
}

static gboolean
fu_ch341a_cfi_device_setup(FuDevice *device, GError **error)
{
	FuCh341aCfiDevice *self = FU_CH341A_CFI_DEVICE(device);

	/* setup SPI chip */
	if (!fu_ch341a_cfi_device_read_jedec(self, error))
		return FALSE;

	/* this is a generic SPI chip */
	fu_device_add_instance_id(device, "SPI");
	fu_device_add_vendor_id(device, "SPI:*");

	/* FuCfiDevice->setup */
	return FU_DEVICE_CLASS(fu_ch341a_cfi_device_parent_class)->setup(device, error);
}

static gboolean
fu_ch341a_cfi_device_write_enable(FuCh341aCfiDevice *self, GError **error)
{
	FuCh341aDevice *proxy = FU_CH341A_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	guint8 buf[1] = {0x0};
	g_autoptr(FuDeviceLocker) cslocker = NULL;

	/* write enable */
	if (!fu_cfi_device_get_cmd(FU_CFI_DEVICE(self), FU_CFI_DEVICE_CMD_WRITE_EN, &buf[0], error))
		return FALSE;
	cslocker = fu_cfi_device_chip_select_locker_new(FU_CFI_DEVICE(self), error);
	if (cslocker == NULL)
		return FALSE;
	if (!fu_ch341a_device_spi_transfer(proxy, buf, sizeof(buf), error))
		return FALSE;
	if (!fu_device_locker_close(cslocker, error))
		return FALSE;

	/* check that WEL is now set */
	return fu_ch341a_cfi_device_wait_for_status(self, 0b10, 0b10, 10, 5, error);
}

static gboolean
fu_ch341a_cfi_device_chip_erase(FuCh341aCfiDevice *self, GError **error)
{
	FuCh341aDevice *proxy = FU_CH341A_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	guint8 buf[] = {0x0};
	g_autoptr(FuDeviceLocker) cslocker = NULL;

	/* enable chip */
	cslocker = fu_cfi_device_chip_select_locker_new(FU_CFI_DEVICE(self), error);
	if (cslocker == NULL)
		return FALSE;

	/* erase */
	if (!fu_cfi_device_get_cmd(FU_CFI_DEVICE(self),
				   FU_CFI_DEVICE_CMD_CHIP_ERASE,
				   &buf[0],
				   error))
		return FALSE;
	if (!fu_ch341a_device_spi_transfer(proxy, buf, sizeof(buf), error))
		return FALSE;
	if (!fu_device_locker_close(cslocker, error))
		return FALSE;

	/* poll Read Status register BUSY */
	return fu_ch341a_cfi_device_wait_for_status(self, 0b1, 0b0, 100, 500, error);
}

static gboolean
fu_ch341a_cfi_device_write_page(FuCh341aCfiDevice *self, FuChunk *page, GError **error)
{
	FuCh341aDevice *proxy = FU_CH341A_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	guint8 buf[4] = {0x0};
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(FuDeviceLocker) cslocker = NULL;

	if (!fu_ch341a_cfi_device_write_enable(self, error))
		return FALSE;

	cslocker = fu_cfi_device_chip_select_locker_new(FU_CFI_DEVICE(self), error);
	if (cslocker == NULL)
		return FALSE;

	/* cmd, then 24 bit starting address */
	fu_common_write_uint32(buf, fu_chunk_get_address(page), G_BIG_ENDIAN);
	if (!fu_cfi_device_get_cmd(FU_CFI_DEVICE(self),
				   FU_CFI_DEVICE_CMD_PAGE_PROG,
				   &buf[0],
				   error))
		return FALSE;
	if (!fu_ch341a_device_spi_transfer(proxy, buf, sizeof(buf), error))
		return FALSE;

	/* send data */
	chunks = fu_chunk_array_new(fu_chunk_get_data(page),
				    fu_chunk_get_data_sz(page),
				    0x0,
				    0x0,
				    CH341A_PAYLOAD_SIZE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 buf2[CH341A_PAYLOAD_SIZE] = {0x0};
		if (!fu_memcpy_safe(buf2,
				    sizeof(buf2),
				    0x0, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;
		if (!fu_ch341a_device_spi_transfer(proxy, buf2, fu_chunk_get_data_sz(chk), error))
			return FALSE;
	}
	if (!fu_device_locker_close(cslocker, error))
		return FALSE;

	/* poll Read Status register BUSY */
	return fu_ch341a_cfi_device_wait_for_status(self, 0b1, 0b0, 100, 50, error);
}

static gboolean
fu_ch341a_cfi_device_write_pages(FuCh341aCfiDevice *self,
				 GPtrArray *pages,
				 FuProgress *progress,
				 GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, pages->len);
	for (guint i = 0; i < pages->len; i++) {
		FuChunk *page = g_ptr_array_index(pages, i);
		if (!fu_ch341a_cfi_device_write_page(self, page, error))
			return FALSE;
		fu_progress_step_done(progress);
	}
	/* success */
	return TRUE;
}

static GBytes *
fu_ch341a_cfi_device_read_firmware(FuCh341aCfiDevice *self,
				   gsize bufsz,
				   FuProgress *progress,
				   GError **error)
{
	FuCh341aDevice *proxy = FU_CH341A_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	guint8 buf[CH341A_PAYLOAD_SIZE] = {0x0};
	g_autoptr(FuDeviceLocker) cslocker = NULL;
	g_autoptr(GByteArray) blob = g_byte_array_new();
	g_autoptr(GPtrArray) chunks = NULL;

	/* enable chip */
	cslocker = fu_cfi_device_chip_select_locker_new(FU_CFI_DEVICE(self), error);
	if (cslocker == NULL)
		return NULL;

	/* read each block */
	chunks = fu_chunk_array_new(NULL, bufsz + 0x4, 0x0, 0x0, CH341A_PAYLOAD_SIZE);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);

	/* cmd, then 24 bit starting address */
	fu_common_write_uint32(buf, 0x0, G_BIG_ENDIAN);
	if (!fu_cfi_device_get_cmd(FU_CFI_DEVICE(self),
				   FU_CFI_DEVICE_CMD_READ_DATA,
				   &buf[0],
				   error))
		return NULL;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		/* the first package has cmd and address info */
		if (!fu_ch341a_device_spi_transfer(proxy, buf, sizeof(buf), error))
			return NULL;
		if (i == 0) {
			g_byte_array_append(blob, buf + 0x4, fu_chunk_get_data_sz(chk) - 0x4);
		} else {
			g_byte_array_append(blob, buf + 0x0, fu_chunk_get_data_sz(chk));
		}

		/* done */
		fu_progress_step_done(progress);
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&blob));
}

static gboolean
fu_ch341a_cfi_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuCh341aCfiDevice *self = FU_CH341A_CFI_DEVICE(device);
	FuCh341aDevice *proxy = FU_CH341A_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw_verify = NULL;
	g_autoptr(GPtrArray) pages = NULL;
	g_autoptr(FuDeviceLocker) cslocker = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open programmer */
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 33);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 35);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* erase */
	if (!fu_ch341a_cfi_device_write_enable(self, error)) {
		g_prefix_error(error, "failed to enable writes: ");
		return FALSE;
	}
	if (!fu_ch341a_cfi_device_chip_erase(self, error)) {
		g_prefix_error(error, "failed to erase: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* write each block */
	pages = fu_chunk_array_new_from_bytes(fw,
					      0x0,
					      0x0,
					      fu_cfi_device_get_page_size(FU_CFI_DEVICE(self)));
	if (!fu_ch341a_cfi_device_write_pages(self,
					      pages,
					      fu_progress_get_child(progress),
					      error)) {
		g_prefix_error(error, "failed to write pages: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* verify each block */
	fw_verify = fu_ch341a_cfi_device_read_firmware(self,
						       g_bytes_get_size(fw),
						       fu_progress_get_child(progress),
						       error);
	if (fw_verify == NULL) {
		g_prefix_error(error, "failed to verify blocks: ");
		return FALSE;
	}
	if (!fu_common_bytes_compare(fw, fw_verify, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static GBytes *
fu_ch341a_cfi_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuCh341aCfiDevice *self = FU_CH341A_CFI_DEVICE(device);
	FuCh341aDevice *proxy = FU_CH341A_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));
	gsize bufsz = fu_device_get_firmware_size_max(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open programmer */
	locker = fu_device_locker_new(proxy, error);
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
	return fu_ch341a_cfi_device_read_firmware(self, bufsz, progress, error);
}

static void
fu_ch341a_cfi_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100); /* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0);	/* reload */
}

static void
fu_ch341a_cfi_device_init(FuCh341aCfiDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "org.jedec.cfi");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
}

static void
fu_ch341a_cfi_device_class_init(FuCh341aCfiDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	FuCfiDeviceClass *klass_cfi = FU_CFI_DEVICE_CLASS(klass);

	klass_cfi->chip_select = fu_ch341a_cfi_device_chip_select;

	klass_device->setup = fu_ch341a_cfi_device_setup;
	klass_device->write_firmware = fu_ch341a_cfi_device_write_firmware;
	klass_device->dump_firmware = fu_ch341a_cfi_device_dump_firmware;
	klass_device->set_progress = fu_ch341a_cfi_device_set_progress;
}
