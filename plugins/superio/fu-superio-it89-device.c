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
	fu_device_set_version (FU_DEVICE (self), version, FWUPD_VERSION_FORMAT_PAIR);

	/* get size from the EC */
	if (!fu_superio_it89_device_ec_size (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_superio_it89_device_ec_pm1do_sci (FuSuperioDevice *self, guint8 val, GError **error)
{
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DOSCI, error))
		return FALSE;
	if (!fu_superio_device_ec_write1 (self, val, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_superio_it89_device_ec_pm1do_smi (FuSuperioDevice *self, guint8 val, GError **error)
{
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DOCMI, error))
		return FALSE;
	if (!fu_superio_device_ec_write1 (self, val, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_superio_device_ec_read_status (FuSuperioDevice *self, GError **error)
{
	guint8 tmp = 0x00;

	/* read status register */
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DO, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_sci (self, SIO_SPI_CMD_RDSR, error))
		return FALSE;

	/* wait for write */
	do {
		if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DI, error))
			return FALSE;
		if (!fu_superio_device_ec_read (self, &tmp, error))
			return FALSE;
	} while ((tmp & SIO_STATUS_EC_OBF) != 0);

	/* watch SCI events */
	return fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DISCI, error);
}

static gboolean
fu_superio_device_ec_write_disable (FuSuperioDevice *self, GError **error)
{
	guint8 tmp = 0x00;

	/* read existing status */
	if (!fu_superio_device_ec_read_status (self, error))
		return FALSE;

	/* write disable */
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DO, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_sci (self, SIO_SPI_CMD_WRDI, error))
		return FALSE;

	/* read status register */
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DO, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_sci (self, SIO_SPI_CMD_RDSR, error))
		return FALSE;

	/* wait for read */
	do {
		if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DI, error))
			return FALSE;
		if (!fu_superio_device_ec_read (self, &tmp, error))
			return FALSE;
	} while ((tmp & SIO_STATUS_EC_IBF) != 0);

	/* watch SCI events */
	return fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DISCI, error);
}

static gboolean
fu_superio_device_ec_write_enable (FuSuperioDevice *self, GError **error)
{
	guint8 tmp = 0x0;

	/* read existing status */
	if (!fu_superio_device_ec_read_status (self, error))
		return FALSE;

	/* write enable */
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DO, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_sci (self, SIO_SPI_CMD_WREN, error))
		return FALSE;

	/* read status register */
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DO, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_sci (self, SIO_SPI_CMD_RDSR, error))
		return FALSE;

	/* wait for !BUSY */
	do {
		if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DI, error))
			return FALSE;
		if (!fu_superio_device_ec_read (self, &tmp, error))
			return FALSE;
	} while ((tmp & 3) != SIO_STATUS_EC_IBF);

	/* watch SCI events */
	return fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DISCI, error);
}

static GBytes *
fu_superio_it89_device_read_addr (FuSuperioDevice *self,
				  guint32 addr,
				  guint size,
				  GFileProgressCallback progress_cb,
				  GError **error)
{
	g_autofree guint8 *buf = NULL;

	/* check... */
	if (!fu_superio_device_ec_write_disable (self, error))
		return NULL;
	if (!fu_superio_device_ec_read_status (self, error))
		return NULL;

	/* high speed read */
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DO, error))
		return NULL;
	if (!fu_superio_it89_device_ec_pm1do_sci (self, SIO_SPI_CMD_HS_READ, error))
		return NULL;

	/* set address, MSB, MID, LSB */
	if (!fu_superio_it89_device_ec_pm1do_smi (self, addr >> 16, error))
		return NULL;
	if (!fu_superio_it89_device_ec_pm1do_smi (self, addr >> 8, error))
		return NULL;
	if (!fu_superio_it89_device_ec_pm1do_smi (self, addr & 0xff, error))
		return NULL;

	/* padding for HS? */
	if (!fu_superio_it89_device_ec_pm1do_smi (self, 0x0, error))
		return NULL;

	/* read out data */
	buf = g_malloc0 (size);
	for (guint i = 0; i < size; i++) {
		if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DI, error))
			return NULL;
		if (!fu_superio_device_ec_read (self, &buf[i], error))
			return NULL;

		/* update progress */
		if (progress_cb != NULL)
			progress_cb ((goffset) i, (goffset) size, self);
	}

	/* check again... */
	if (!fu_superio_device_ec_read_status (self, error))
		return NULL;

	/* success */
	return g_bytes_new_take (g_steal_pointer (&buf), size);
}

static void
fu_superio_it89_device_progress_cb (goffset current, goffset total, gpointer user_data)
{
	FuDevice *device = FU_DEVICE (user_data);
	fu_device_set_progress_full (device, (gsize) current, (gsize) total);
}

static gboolean
fu_superio_it89_device_write_addr (FuSuperioDevice *self, guint addr, GBytes *fw, GError **error)
{
	gsize size = 0;
	const guint8 *buf = g_bytes_get_data (fw, &size);

	/* sanity check */
	if ((addr & 0xff) != 0x00) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "write addr unaligned, got 0x%04x",
			     (guint) addr);
	}
	if (size % 2 != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "write length not supported, got 0x%04x",
			     (guint) size);
	}

	/* enable writes */
	if (!fu_superio_device_ec_write_enable (self, error))
		return FALSE;

	/* write DWORDs */
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DO, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_sci (self, SIO_SPI_CMD_WRITE_WORD, error))
		return FALSE;

	/* set address, MSB, MID, LSB */
	if (!fu_superio_it89_device_ec_pm1do_smi (self, addr >> 16, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_smi (self, addr >> 8, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_smi (self, addr & 0xff, error))
		return FALSE;

	/* write data two bytes at a time */
	for (guint i = 0; i < size; i += 2) {
		if (i > 0) {
			if (!fu_superio_device_ec_read_status (self, error))
				return FALSE;
			if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DO, error))
				return FALSE;
			if (!fu_superio_it89_device_ec_pm1do_sci (self,
								  SIO_SPI_CMD_WRITE_WORD,
								  error))
				return FALSE;
		}
		if (!fu_superio_it89_device_ec_pm1do_smi (self, buf[i+0], error))
			return FALSE;
		if (!fu_superio_it89_device_ec_pm1do_smi (self, buf[i+1], error))
			return FALSE;
	}

	/* reset back? */
	if (!fu_superio_device_ec_write_disable (self, error))
		return FALSE;
	return fu_superio_device_ec_read_status (self, error);
}

static gboolean
fu_superio_it89_device_erase_addr (FuSuperioDevice *self, guint addr, GError **error)
{
	/* enable writes */
	if (!fu_superio_device_ec_write_enable (self, error))
		return FALSE;

	/* sector erase */
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DO, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_sci (self, SIO_SPI_CMD_4K_SECTOR_ERASE, error))
		return FALSE;

	/* set address, MSB, MID, LSB */
	if (!fu_superio_it89_device_ec_pm1do_smi (self, addr >> 16, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_smi (self, addr >> 8, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_smi (self, addr & 0xff, error))
		return FALSE;

	/* watch SCI events */
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DISCI, error))
		return FALSE;
	return fu_superio_device_ec_read_status (self, error);
}

/* The 14th byte of the 16 byte signature is always read from the hardware as
 * 0x00 rather than the specified 0xAA. Fix up the firmware to match the
 * .ROM file which uses 0x7F as the number of bytes to mirror to e-flash... */
static GBytes *
fu_plugin_superio_fix_signature (FuSuperioDevice *self, GBytes *fw, GError **error)
{
	gsize sz = 0;
	const guint8 *buf = g_bytes_get_data (fw, &sz);
	g_autofree guint8 *buf2 = NULL;
	const guint signature_offset = 0x4d; /* IT85, IT89 is 0x8d */

	/* not big enough */
	if (sz < signature_offset + 1) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "image too small to fix");
		return NULL;
	}

	/* not zero */
	if (buf[signature_offset] != 0x0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "nonzero signature byte");
		return NULL;
	}

	/* fix signature to match SMT version */
	buf2 = g_memdup (buf, sz);
	buf2[signature_offset] = 0x7f;
	return g_bytes_new_take (g_steal_pointer (&buf2), sz);
}

static FuFirmware *
fu_superio_it89_device_read_firmware (FuDevice *device, GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	guint64 fwsize = fu_device_get_firmware_size_min (device);
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) fw = NULL;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);
	blob = fu_superio_it89_device_read_addr (self, 0x0, fwsize,
						 fu_superio_it89_device_progress_cb,
						 error);
	fw = fu_plugin_superio_fix_signature (self, blob, error);
	return fu_firmware_new_from_bytes (fw);
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
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	return TRUE;
}

static gboolean
fu_superio_it89_device_check_eflash (FuSuperioDevice *self, GError **error)
{
	g_autoptr(GBytes) fw = NULL;
	const guint64 fwsize = fu_device_get_firmware_size_min (FU_DEVICE (self));
	const guint sigsz = 16;

	/* last 16 bytes of eeprom */
	fw = fu_superio_it89_device_read_addr (self, fwsize - sigsz,
					       sigsz, NULL, error);
	if (fw == NULL) {
		g_prefix_error (error, "failed to read signature bytes");
		return FALSE;
	}

	/* cannot flash here without keyboard programmer */
	if (!fu_common_bytes_is_empty (fw)) {
		gsize sz = 0;
		const guint8 *buf = g_bytes_get_data (fw, &sz);
		g_autoptr(GString) str = g_string_new (NULL);
		for (guint i = 0; i < sz; i++)
			g_string_append_printf (str, "0x%02x ", buf[i]);
		if (str->len > 0)
			g_string_truncate (str, str->len - 1);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "e-flash has been protected: %s",
			     str->str);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_superio_it89_device_write_chunk (FuSuperioDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(GBytes) fw1 = NULL;
	g_autoptr(GBytes) fw2 = NULL;
	g_autoptr(GBytes) fw3 = NULL;

	/* erase page */
	if (!fu_superio_it89_device_erase_addr (self, chk->address, error)) {
		g_prefix_error (error, "failed to erase @0x%04x", (guint) chk->address);
		return FALSE;
	}

	/* check erased */
	fw1 = fu_superio_it89_device_read_addr (self, chk->address,
						chk->data_sz, NULL,
						error);
	if (fw1 == NULL) {
		g_prefix_error (error, "failed to read erased "
				"bytes @0x%04x", (guint) chk->address);
		return FALSE;
	}
	if (!fu_common_bytes_is_empty (fw1)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "sector was not erased");
		return FALSE;
	}

	/* skip empty page */
	fw2 = g_bytes_new_static (chk->data, chk->data_sz);
	if (fu_common_bytes_is_empty (fw2))
		return TRUE;

	/* write page */
	if (!fu_superio_it89_device_write_addr (self, chk->address, fw2, error)) {
		g_prefix_error (error, "failed to write @0x%04x", (guint) chk->address);
		return FALSE;
	}

	/* verify page */
	fw3 = fu_superio_it89_device_read_addr (self, chk->address,
						chk->data_sz, NULL,
						error);
	if (fw3 == NULL) {
		g_prefix_error (error, "failed to read written "
				"bytes @0x%04x", (guint) chk->address);
		return FALSE;
	}
	if (!fu_common_bytes_compare (fw2, fw3, error)) {
		g_prefix_error (error, "failed to verify @0x%04x",
				(guint) chk->address);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_superio_it89_device_get_jedec_id (FuSuperioDevice *self, guint8 *id, GError **error)
{
	/* read status register */
	if (!fu_superio_device_ec_read_status (self, error))
		return FALSE;
	if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DO, error))
		return FALSE;
	if (!fu_superio_it89_device_ec_pm1do_sci (self, SIO_SPI_CMD_JEDEC_ID, error))
		return FALSE;

	/* wait for reads */
	for (guint i = 0; i < 4; i++) {
		if (!fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DI, error))
			return FALSE;
		if (!fu_superio_device_ec_read (self, &id[i], error))
			return FALSE;
	}

	/* watch SCI events */
	return fu_superio_device_ec_write1 (self, SIO_EC_PMC_PM1DISCI, error);
}

static gboolean
fu_superio_it89_device_write_firmware (FuDevice *device,
				       FuFirmware *firmware,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuSuperioDevice *self = FU_SUPERIO_DEVICE (device);
	guint8 id[4] = { 0x0 };
	g_autoptr(GBytes) fw_fixed = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* check JEDEC ID */
	if (!fu_superio_it89_device_get_jedec_id (self, id, error)) {
		g_prefix_error (error, "failed to get JEDEC ID: ");
		return FALSE;
	}
	if (id[0] != 0xff || id[1] != 0xff || id[2] != 0xfe || id[3] != 0xff) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "JEDEC ID not valid, 0x%02x%02x%02x%02x",
			     id[0], id[1], id[2], id[3]);
		return FALSE;
	}

	/* check eflash is writable */
	if (!fu_superio_it89_device_check_eflash (self, error))
		return FALSE;

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* disable the mirroring of e-flash */
	if (g_getenv ("FWUPD_SUPERIO_DISABLE_MIRROR") != NULL) {
		fw_fixed = fu_plugin_superio_fix_signature (self, fw, error);
		if (fw_fixed == NULL)
			return FALSE;
	} else {
		fw_fixed = g_bytes_ref (fw);
	}

	/* chunks of 1kB, skipping the final chunk */
	chunks = fu_chunk_array_new_from_bytes (fw_fixed, 0x00, 0x00, 0x400);
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < chunks->len - 1; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);

		/* try this many times; the failure-to-flash case leaves you
		 * without a keyboard and future boot may completely fail */
		for (guint j = 0;; j++) {
			g_autoptr(GError) error_chk = NULL;
			if (fu_superio_it89_device_write_chunk (self, chk, &error_chk))
				break;
			if (j > 5) {
				g_propagate_error (error, g_steal_pointer (&error_chk));
				return FALSE;
			}
			g_warning ("failure %u: %s", j, error_chk->message);
		}

		/* set progress */
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len);
	}

	/* success */
	fu_device_set_progress (device, 100);
	return TRUE;
}

static void
fu_superio_it89_device_init (FuSuperioIt89Device *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_ONLY_OFFLINE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
}

static void
fu_superio_it89_device_class_init (FuSuperioIt89DeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuSuperioDeviceClass *klass_superio_device = FU_SUPERIO_DEVICE_CLASS (klass);
	klass_device->attach = fu_superio_it89_device_attach;
	klass_device->detach = fu_superio_it89_device_detach;
	klass_device->read_firmware = fu_superio_it89_device_read_firmware;
	klass_device->write_firmware = fu_superio_it89_device_write_firmware;
	klass_superio_device->setup = fu_superio_it89_device_setup;
}
