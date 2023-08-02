/*
 * Copyright (C) 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-genesys-common.h"
#include "fu-genesys-scaler-device.h"
#include "fu-genesys-usbhub-codesign-firmware.h"
#include "fu-genesys-usbhub-device.h"
#include "fu-genesys-usbhub-firmware.h"
#include "fu-genesys-usbhub-struct.h"

/**
 * FU_GENESYS_USBHUB_FLAG_HAS_MSTAR_SCALER:
 *
 * Device has a MStar scaler attached via I2C.
 *
 * Since 1.7.6
 */
#define FU_GENESYS_USBHUB_FLAG_HAS_MSTAR_SCALER (1 << 0)
/**
 * FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY:
 *
 * Device has a public-key appended to firmware.
 *
 * Since 1.8.0
 */
#define FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY (1 << 1)

#define GENESYS_USBHUB_STATIC_TOOL_DESC_IDX_USB_3_0  0x84
#define GENESYS_USBHUB_DYNAMIC_TOOL_DESC_IDX_USB_3_0 0x85
#define GENESYS_USBHUB_STATIC_TOOL_DESC_IDX_USB_2_0  0x81
#define GENESYS_USBHUB_DYNAMIC_TOOL_DESC_IDX_USB_2_0 0x82
#define GENESYS_USBHUB_FW_INFO_DESC_IDX		     0x83
#define GENESYS_USBHUB_VENDOR_SUPPORT_DESC_IDX	     0x86

#define GENESYS_USBHUB_GL_HUB_VERIFY 0x71
#define GENESYS_USBHUB_GL_HUB_SWITCH 0x81
#define GENESYS_USBHUB_GL_HUB_READ   0x82
#define GENESYS_USBHUB_GL_HUB_WRITE  0x83

#define GENESYS_USBHUB_ENCRYPT_REGION_START 0x01
#define GENESYS_USBHUB_ENCRYPT_REGION_END   0x15

#define GL3523_PUBLIC_KEY_LEN 0x212
#define GL3523_SIG_LEN	      0x100

#define GENESYS_USBHUB_USB_TIMEOUT	   5000 /* ms */
#define GENESYS_USBHUB_FLASH_WRITE_TIMEOUT 500	/* ms */

typedef enum {
	FW_BANK_1,
	FW_BANK_2,

	FW_BANK_COUNT
} FuGenesysFwBank;

#define GL3523_BONDING_VALID_BIT 0x0F
#define GL3590_BONDING_VALID_BIT 0x7F

#define GL3523_BONDING_FLASH_DUMP_LOCATION_BIT 1 << 4
#define GL3590_BONDING_FLASH_DUMP_LOCATION_BIT 1 << 7

typedef enum {
	ISP_EXIT,
	ISP_ENTER,
} FuGenesysIspMode;

typedef struct {
	guint8 req_switch;
	guint8 req_read;
	guint8 req_write;
} FuGenesysVendorCommandSetting;

typedef struct {
	guint16 cmd;
	guint16 len;
} FuGenesysQueryRdidFormat;

typedef struct {
	FuGenesysChip chip;
	gboolean support_dual_bank;
	gboolean support_code_size;
	guint32 fw_bank_addr[FW_BANK_COUNT][FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT];
	guint32 fw_bank_capacity[FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT];
	guint32 fw_data_max_count;
} FuGenesysModelSpec;

struct _FuGenesysUsbhubDevice {
	FuUsbDevice parent_instance;
	GByteArray *st_static_ts;
	GByteArray *st_dynamic_ts;
	GByteArray *st_fwinfo_ts;
	GByteArray *st_vendor_ts;
	FuGenesysVendorCommandSetting vcs;
	FuGenesysModelSpec spec;

	FuGenesysTsVersion tool_string_version;
	FuGenesysFwStatus running_bank;
	guint8 bonding;

	gboolean is_gl352350; /* model with unique codesign mechanism */

	FuCfiDevice *cfi_device;
	guint32 flash_erase_delay;
	guint32 flash_write_delay;
	guint32 flash_block_size;
	guint32 flash_sector_size;
	guint32 flash_rw_size;

	guint16 fw_bank_code_sizes[FW_BANK_COUNT][FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT];
	guint16 fw_bank_vers[FW_BANK_COUNT][FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT];
	FuGenesysFwBank update_fw_banks[FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT];

	/**
	 * GL3523 hub default boot up on fw bank1 - even bank2's version is higher.
	 * It only boots up on bank2 when bank1 is broken and bank2 is right.
	 *
	 * Therefore, we want bank1 always be last updated firmware.
	 * Also, to fulfill dual bank mechanism, we shall keep at last one fw bank is right.
	 *
	 * For this purpose, we usually backup bank1 (right fw) to bank2, and update fw to bank1.
	 * In rare case - bootup on bank2, we can update fw to bank1 and skip backup.
	 */
	gboolean backup_hub_fw_bank1;
	GBytes *hub_fw_bank1_data; /* restore hub bank1 fw for backup */

	/* codesign info */
	gboolean has_codesign;
	FuGenesysFwCodesign codesign;
	GByteArray *st_public_key;
};

G_DEFINE_TYPE(FuGenesysUsbhubDevice, fu_genesys_usbhub_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_genesys_usbhub_device_mstar_scaler_setup(FuGenesysUsbhubDevice *self, GError **error)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	g_autoptr(FuGenesysScalerDevice) scaler_device = fu_genesys_scaler_device_new(ctx);

	fu_device_add_child(FU_DEVICE(self), FU_DEVICE(scaler_device));

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_read_flash(FuGenesysUsbhubDevice *self,
				    guint start_addr,
				    guint8 *buf,
				    guint bufsz,
				    FuProgress *progress,
				    GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_mutable_new(buf, bufsz, start_addr, 0x0, self->flash_rw_size);
	if (progress != NULL) {
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_set_steps(progress, chunks->len);
	}
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   self->vcs.req_read,
						   (fu_chunk_get_address(chk) & 0x0f0000) >>
						       4,			       /* value */
						   fu_chunk_get_address(chk) & 0xffff, /* idx */
						   fu_chunk_get_data_out(chk),	       /* data */
						   fu_chunk_get_data_sz(chk), /* data length */
						   NULL,		      /* actual length */
						   GENESYS_USBHUB_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error,
				       "error reading flash at 0x%04x: ",
				       fu_chunk_get_address(chk));
			return FALSE;
		}
		if (progress != NULL)
			fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_compare_flash_blank(FuGenesysUsbhubDevice *self,
					     guint start_addr,
					     guint code_size,
					     FuProgress *progress,
					     GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	guint read_addr = 0;
	guint read_size = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) blank_buf = g_byte_array_new();

	if (code_size < 0x400) {
		/* for small data, compare entire size */
		read_addr = start_addr;
		read_size = code_size;
	} else {
		/* for large data, compare last 1024 bytes */
		read_addr = start_addr + code_size - 0x400;
		read_size = 0x400;
	}

	fu_byte_array_set_size(buf, self->flash_rw_size, 0xFF);
	fu_byte_array_set_size(blank_buf, self->flash_rw_size, 0xFF);

	chunks = fu_chunk_array_new(NULL, read_size, read_addr, 0x0, self->flash_rw_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   self->vcs.req_read,
						   (fu_chunk_get_address(chk) & 0x0f0000) >>
						       4,			       /* value */
						   fu_chunk_get_address(chk) & 0xffff, /* idx */
						   buf->data,			       /* data */
						   fu_chunk_get_data_sz(chk), /* data length */
						   NULL,		      /* actual length */
						   GENESYS_USBHUB_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error,
				       "error reading flash at 0x%04x: ",
				       fu_chunk_get_address(chk));
			return FALSE;
		}
		if (!fu_memcmp_safe(buf->data,
				    fu_chunk_get_data_sz(chk),
				    0x0,
				    blank_buf->data,
				    blank_buf->len,
				    0x0,
				    fu_chunk_get_data_sz(chk),
				    error)) {
			g_prefix_error(error,
				       "compare flash blank at 0x%04x: ",
				       fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_compare_flash_data(FuGenesysUsbhubDevice *self,
					    guint start_addr,
					    const guint8 *buf,
					    guint bufsz,
					    FuProgress *progress,
					    GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GByteArray) read_buf = g_byte_array_new();

	fu_byte_array_set_size(read_buf, self->flash_rw_size, 0xFF);

	chunks = fu_chunk_array_new(buf, bufsz, start_addr, 0, self->flash_rw_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   self->vcs.req_read,
						   (fu_chunk_get_address(chk) & 0x0f0000) >>
						       4,			       /* value */
						   fu_chunk_get_address(chk) & 0xffff, /* idx */
						   read_buf->data,		       /* data */
						   fu_chunk_get_data_sz(chk), /* data length */
						   NULL,		      /* actual length */
						   GENESYS_USBHUB_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error,
				       "error reading flash at 0x%04x: ",
				       fu_chunk_get_address(chk));
			return FALSE;
		}
		if (!fu_memcmp_safe(read_buf->data,
				    read_buf->len,
				    0x0,
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0,
				    fu_chunk_get_data_sz(chk),
				    error)) {
			g_prefix_error(error,
				       "compare flash data failed at 0x%04x: ",
				       fu_chunk_get_address(chk));
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_reset(FuGenesysUsbhubDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));

	/* send data to device */
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vcs.req_switch,
					   0x0003, /* value */
					   0,	   /* idx */
					   NULL,   /* data */
					   0,	   /* data length */
					   NULL,   /* actual length */
					   GENESYS_USBHUB_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error, "error resetting device: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static FuCfiDevice *
fu_genesys_usbhub_device_cfi_setup(FuGenesysUsbhubDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	const FuGenesysQueryRdidFormat rdid[] = {
	    {.cmd = 0x1D02, .len = 0x02},
	    {.cmd = 0x4B01, .len = 0x02},
	    {.cmd = 0x9001, .len = 0x02},
	    {.cmd = 0x9f02, .len = 0x03},
	    {.cmd = 0xAB01, .len = 0x02},
	};

	for (guint8 i = 0; i < G_N_ELEMENTS(rdid); i++) {
		guint8 buf[3] = {0};
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FuCfiDevice) cfi_device = NULL;
		g_autofree gchar *flash_id = NULL;

		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   self->vcs.req_read,
						   rdid[i].cmd, /* value */
						   0,		/* idx */
						   buf,		/* data */
						   rdid[i].len, /* data length */
						   NULL,	/* actual length */
						   GENESYS_USBHUB_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error, "error reading flash chip: ");
			return NULL;
		}

		if (rdid[i].len == 2) {
			flash_id = g_strdup_printf("%02X%02X", buf[0], buf[1]);
		} else if (rdid[i].len == 3) {
			flash_id = g_strdup_printf("%02X%02X%02X", buf[0], buf[1], buf[2]);
		}

		cfi_device = fu_cfi_device_new(fu_device_get_context(FU_DEVICE(self)), flash_id);
		if (cfi_device == NULL)
			continue;

		if (!fu_device_setup(FU_DEVICE(cfi_device), &error_local)) {
			g_debug("ignoring %s: %s", flash_id, error_local->message);
			continue;
		}

		if (fu_device_get_name(FU_DEVICE(cfi_device)) == NULL) {
			g_debug("read %#x: %s", rdid[i].cmd, flash_id);
			continue;
		}

		fu_dump_raw(G_LOG_DOMAIN, "Flash ID", buf, rdid[i].len);
		g_debug("CFI: %s", fu_device_get_name(FU_DEVICE(cfi_device)));

		return g_steal_pointer(&cfi_device);
	}

	/* failure */
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no CFI device found");
	return NULL;
}

static gboolean
fu_genesys_usbhub_device_wait_flash_status_register_cb(FuDevice *device,
						       gpointer user_data,
						       GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	guint8 status = 0;
	FuGenesysWaitFlashRegisterHelper *helper = user_data;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vcs.req_read,
					   helper->reg << 8 | 0x02, /* value */
					   0,			    /* idx */
					   &status,		    /* data */
					   1,			    /* data length */
					   NULL,		    /* actual length */
					   GENESYS_USBHUB_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error getting flash status register (0x%02x): ",
			       helper->reg);
		return FALSE;
	}
	if (status != helper->expected_val) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "wrong value in flash status register");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_set_isp_mode(FuGenesysUsbhubDevice *self,
				      FuGenesysIspMode mode,
				      GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   self->vcs.req_switch,
					   mode, /* value */
					   0,	 /* idx */
					   NULL, /* data */
					   0,	 /* data length */
					   NULL, /* actual length */
					   GENESYS_USBHUB_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "error setting isp mode - "
			       "control transfer error (reg 0x%02x) ",
			       self->vcs.req_switch);
		return FALSE;
	}

	if (mode == ISP_ENTER) {
		FuGenesysWaitFlashRegisterHelper helper = {.reg = 5, .expected_val = 0};

		/* 150ms */
		if (!fu_device_retry(FU_DEVICE(self),
				     fu_genesys_usbhub_device_wait_flash_status_register_cb,
				     5,
				     &helper,
				     error)) {
			g_prefix_error(error, "error setting isp mode: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_authentication_request(FuGenesysUsbhubDevice *self,
						guint8 offset_start,
						guint8 offset_end,
						guint8 data_check,
						GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	guint8 buf = 0;

	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   GENESYS_USBHUB_GL_HUB_VERIFY,
					   (offset_end << 8) | offset_start, /* value */
					   0,				     /* idx */
					   &buf,			     /* data */
					   1,				     /* data length */
					   NULL,			     /* actual length */
					   GENESYS_USBHUB_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "control transfer error (req: 0x%0x): ",
			       (guint)GENESYS_USBHUB_GL_HUB_VERIFY);
		return FALSE;
	}
	if (!g_usb_device_control_transfer(usb_device,
					   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
					   G_USB_DEVICE_RECIPIENT_DEVICE,
					   GENESYS_USBHUB_GL_HUB_VERIFY,
					   (offset_end << 8) | offset_start, /* value */
					   1 | (data_check << 8),	     /* idx */
					   &buf,			     /* data */
					   1,				     /* data length */
					   NULL,			     /* actual length */
					   GENESYS_USBHUB_USB_TIMEOUT,
					   NULL,
					   error)) {
		g_prefix_error(error,
			       "control transfer error (req: 0x%0x): ",
			       (guint)GENESYS_USBHUB_GL_HUB_VERIFY);
		return FALSE;
	}
	if (buf != 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    "device authentication failed");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_authenticate(FuGenesysUsbhubDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	guint8 low_byte;
	guint8 high_byte;
	guint8 temp_byte;
	guint8 offset_start;
	guint8 offset_end;

	if (self->vcs.req_switch == GENESYS_USBHUB_GL_HUB_SWITCH) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device authentication not supported");
		return FALSE;
	}

	low_byte = g_usb_device_get_release(usb_device) & 0xff;
	high_byte = (g_usb_device_get_release(usb_device) & 0xff00) >> 8;
	temp_byte = low_byte ^ high_byte;

	offset_start = g_random_int_range(GENESYS_USBHUB_ENCRYPT_REGION_START,
					  GENESYS_USBHUB_ENCRYPT_REGION_END - 1);
	offset_end = g_random_int_range(offset_start + 1, GENESYS_USBHUB_ENCRYPT_REGION_END);
	for (guint8 i = offset_start; i <= offset_end; i++) {
		temp_byte ^= self->st_fwinfo_ts->data[i];
	}
	if (!fu_genesys_usbhub_device_authentication_request(self,
							     offset_start,
							     offset_end,
							     temp_byte,
							     error)) {
		g_prefix_error(error, "error authenticating device: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_enter_isp_mode(FuGenesysUsbhubDevice *self, GError **error)
{
	if (self->has_codesign) {
		if (!fu_genesys_usbhub_device_authenticate(self, error))
			return FALSE;
	}
	if (!fu_genesys_usbhub_device_set_isp_mode(self, ISP_ENTER, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_get_descriptor_data(GBytes *desc_bytes,
					     guint8 *dst,
					     guint dst_size,
					     GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(desc_bytes, &bufsz);

	if (bufsz <= 2) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "data is too small");
		return FALSE;
	}

	/* discard first 2 bytes (desc. length and type) */
	buf += 2;
	bufsz -= 2;
	for (gsize i = 0, j = 0; i < bufsz && j < dst_size; i += 2, j++)
		dst[j] = buf[i];

	/* legacy hub replies "USB2.0 Hub" or "USB3.0 Hub" */
	if (memcmp(dst, "USB", 3) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "tool string unsupported");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_check_fw_signature(FuGenesysUsbhubDevice *self,
					    FuGenesysFwType fw_type,
					    int bank_num,
					    GError **error)
{
	guint8 sig[GENESYS_USBHUB_FW_SIG_LEN] = {0};
	const gchar *sig_txt = NULL;

	g_return_val_if_fail(fw_type < FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT, FALSE);
	g_return_val_if_fail(bank_num < FW_BANK_COUNT, FALSE);

	/* get firmware signature from device */
	if (!fu_genesys_usbhub_device_read_flash(self,
						 self->spec.fw_bank_addr[bank_num][fw_type] +
						     GENESYS_USBHUB_FW_SIG_OFFSET,
						 sig,
						 sizeof(sig),
						 NULL,
						 error)) {
		g_prefix_error(error,
			       "error getting fw signature (bank %d) from device: ",
			       bank_num);
		return FALSE;
	}

	/* select firmware signature text and compare */
	switch (fw_type) {
	case FU_GENESYS_FW_TYPE_HUB:
		sig_txt = GENESYS_USBHUB_FW_SIG_TEXT_HUB;
		break;
	case FU_GENESYS_FW_TYPE_DEV_BRIDGE:
		sig_txt = GENESYS_USBHUB_FW_SIG_TEXT_DEV_BRIDGE;
		break;
	case FU_GENESYS_FW_TYPE_PD:
		sig_txt = GENESYS_USBHUB_FW_SIG_TEXT_PD;
		break;
	default:
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported firmware signature");
		return FALSE;
	}

	if (memcmp(sig, sig_txt, sizeof(sig)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_SIGNATURE_INVALID,
				    "wrong firmware signature");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* read the code size from the firmware stored in the device */
static gboolean
fu_genesys_usbhub_device_get_fw_bank_code_size(FuGenesysUsbhubDevice *self,
					       FuGenesysFwType fw_type,
					       int bank_num,
					       GError **error)
{
	guint8 kbs = 0;

	g_return_val_if_fail(fw_type < FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT, FALSE);
	g_return_val_if_fail(bank_num < FW_BANK_COUNT, FALSE);

	/* check firmware type available */
	if (self->spec.fw_bank_capacity[fw_type] == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported firmware type %s",
			    fu_genesys_fw_type_to_string(fw_type));
		return FALSE;
	}

	/* check bank 2 available */
	if (!self->spec.support_dual_bank && bank_num == FW_BANK_2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported dual bank");
		return FALSE;
	}

	/* check firmware signature from device */
	if (!fu_genesys_usbhub_device_check_fw_signature(self, fw_type, bank_num, error))
		return FALSE;

	/* bank firmware use fixed code size */
	if (!self->spec.support_code_size) {
		self->fw_bank_code_sizes[bank_num][fw_type] = self->spec.fw_bank_capacity[fw_type];
		return TRUE;
	}

	/* get code size from device */
	if (!fu_genesys_usbhub_device_read_flash(self,
						 self->spec.fw_bank_addr[bank_num][fw_type] +
						     GENESYS_USBHUB_CODE_SIZE_OFFSET,
						 &kbs,
						 1,
						 NULL,
						 error)) {
		g_prefix_error(error, "error getting fw size from device: ");
		return FALSE;
	}
	self->fw_bank_code_sizes[bank_num][fw_type] = 1024 * kbs;

	/* success */
	return TRUE;
}

/* read the version from the firmware stored in the device */
static gboolean
fu_genesys_usbhub_device_get_fw_bank_version(FuGenesysUsbhubDevice *self,
					     FuGenesysFwType fw_type,
					     int bank_num,
					     GError **error)
{
	gsize bufsz = 0;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autofree guint8 *buf = NULL;

	g_return_val_if_fail(fw_type < FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT, FALSE);
	g_return_val_if_fail(bank_num < FW_BANK_COUNT, FALSE);

	/* check firmware type available */
	if (self->spec.fw_bank_capacity[fw_type] == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported firmware type %s",
			    fu_genesys_fw_type_to_string(fw_type));
		return FALSE;
	}

	/* get bank firmware code size */
	if (!fu_genesys_usbhub_device_get_fw_bank_code_size(self,
							    fw_type,
							    bank_num,
							    &error_local)) {
		g_debug("ignoring firmware %s bank%d: %s",
			fu_genesys_fw_type_to_string(fw_type),
			bank_num + 1,
			error_local->message);
		self->fw_bank_vers[bank_num][fw_type] = 0;
		return TRUE;
	}

	/* get bank firmware from device */
	bufsz = self->fw_bank_code_sizes[bank_num][fw_type];
	buf = g_malloc0(bufsz);
	if (!fu_genesys_usbhub_device_read_flash(self,
						 self->spec.fw_bank_addr[bank_num][fw_type],
						 buf,
						 bufsz,
						 NULL,
						 error))
		return FALSE;

	/* verify bank firmware integrity */
	blob = g_bytes_new_take(g_steal_pointer(&buf), bufsz);
	if (!fu_genesys_usbhub_firmware_verify_checksum(blob, &error_local)) {
		g_debug("ignoring firmware %s bank%d: %s",
			fu_genesys_fw_type_to_string(fw_type),
			bank_num + 1,
			error_local->message);
		self->fw_bank_vers[bank_num][fw_type] = 0;
		return TRUE;
	}

	/* get firmware version from bank firmware */
	if (!fu_memread_uint16_safe(g_bytes_get_data(blob, NULL),
				    g_bytes_get_size(blob),
				    GENESYS_USBHUB_VERSION_OFFSET,
				    &self->fw_bank_vers[bank_num][fw_type],
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to get version: ");
		return FALSE;
	}

	/* keep hub bank 1 fw that may needs backup to bank2 later */
	if (self->spec.chip.model == ISP_MODEL_HUB_GL3523 && bank_num == FW_BANK_1)
		self->hub_fw_bank1_data = g_steal_pointer(&blob);

	/* success */
	return TRUE;
}

/* read the public-key from the firmware stored in the device */
static gboolean
fu_genesys_usbhub_device_get_public_key(FuGenesysUsbhubDevice *self, int bank_num, GError **error)
{
	gsize bufsz = self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_CODESIGN];
	g_autofree guint8 *buf = NULL;
	g_autofree gchar *guid = NULL;

	g_return_val_if_fail(bank_num < FW_BANK_COUNT, FALSE);

	/* check public-key available */
	if (bufsz == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported public-key");
		return FALSE;
	}

	/* get public-key from device */
	buf = g_malloc0(bufsz);
	if (!fu_genesys_usbhub_device_read_flash(
		self,
		self->spec.fw_bank_addr[bank_num][FU_GENESYS_FW_TYPE_CODESIGN],
		buf,
		bufsz,
		NULL,
		error)) {
		g_prefix_error(error, "error getting public-key from device: ");
		return FALSE;
	}

	/* validate and parse public-key */
	if (fu_struct_genesys_fw_rsa_public_key_text_validate(buf, bufsz, 0, NULL)) {
		self->codesign = FU_GENESYS_FW_CODESIGN_RSA;
		self->st_public_key =
		    fu_struct_genesys_fw_rsa_public_key_text_parse(buf, bufsz, 0, error);
	} else if (fu_struct_genesys_fw_ecdsa_public_key_validate(buf, bufsz, 0, NULL)) {
		self->codesign = FU_GENESYS_FW_CODESIGN_ECDSA;
		self->st_public_key =
		    fu_struct_genesys_fw_ecdsa_public_key_parse(buf, bufsz, 0, error);
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "device does not have public-key");
		return FALSE;
	}

	/* add PUBKEY product instance */
	guid = fwupd_guid_hash_data(self->st_public_key->data,
				    self->st_public_key->len,
				    FWUPD_GUID_FLAG_NONE);
	fu_device_add_instance_strup(FU_DEVICE(self), "PUBKEY", guid);

	/* success */
	return TRUE;
}

static gint
fu_genesys_tsdigit_value(gchar c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 10;
	return g_ascii_digit_value(c);
}

static gboolean
fu_genesys_usbhub_device_get_info_from_static_ts(FuGenesysUsbhubDevice *self,
						 const guint8 *buf,
						 gsize bufsz,
						 GError **error)
{
	g_autofree gchar *project_ic_type = NULL;

	self->st_static_ts = fu_struct_genesys_ts_static_parse(buf, bufsz, 0, error);
	project_ic_type = fu_struct_genesys_ts_static_get_mask_project_ic_type(self->st_static_ts);

	/* verify chip model and revision */
	if (memcmp(project_ic_type, "3521", 4) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "ic type %s already EOL and not supported",
			    project_ic_type);
		return FALSE;
	} else if (memcmp(project_ic_type, "3523", 4) == 0) {
		self->spec.chip.model = ISP_MODEL_HUB_GL3523;
	} else if (memcmp(project_ic_type, "3590", 4) == 0) {
		self->spec.chip.model = ISP_MODEL_HUB_GL3590;
	} else if (memcmp(project_ic_type, "3525", 4) == 0) {
		self->spec.chip.model = ISP_MODEL_HUB_GL3525;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported ic type %s",
			    project_ic_type);
		return FALSE;
	}

	self->spec.chip.revision = 10 * (project_ic_type[4] - '0') + (project_ic_type[5] - '0');

	/* convert tool string version */
	self->tool_string_version =
	    fu_struct_genesys_ts_static_get_tool_string_version(self->st_static_ts);

	/* setup firmware parameters */
	switch (self->spec.chip.model) {
	case ISP_MODEL_HUB_GL3521:
		self->spec.support_dual_bank = FALSE;
		self->spec.support_code_size = FALSE;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_HUB] = 0x0000;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_HUB] = 0x5000;
		self->spec.fw_data_max_count = 0x5000;
		break;
	case ISP_MODEL_HUB_GL3523:
		self->spec.support_dual_bank = TRUE;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_HUB] = 0x0000;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_HUB] = 0x8000;

		if (self->spec.chip.revision == 50) {
			self->is_gl352350 = TRUE;
			self->spec.support_code_size = TRUE;
			self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_CODESIGN] = 0x7C00;
			self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_CODESIGN] = 0xFC00;
			self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_HUB] = 0x8000;
		} else {
			self->spec.support_code_size = FALSE;
			self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_CODESIGN] = 0x6000;
			self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_CODESIGN] = 0xE000;
			self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_HUB] = 0x6000;
		}
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_CODESIGN] = 0x400;
		self->spec.fw_data_max_count = 0x10000;
		break;
	case ISP_MODEL_HUB_GL3590:
		self->spec.support_dual_bank = TRUE;
		self->spec.support_code_size = TRUE;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_HUB] = 0x0000;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_HUB] = 0x10000;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_DEV_BRIDGE] = 0x20000;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_DEV_BRIDGE] = 0x30000;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_CODESIGN] = 0xFF00;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_CODESIGN] = 0x1FF00;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_HUB] = 0x10000;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_DEV_BRIDGE] = 0x10000;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_CODESIGN] = 0x100;
		self->spec.fw_data_max_count = 0x40000;
		break;
	case ISP_MODEL_HUB_GL3525:
		self->spec.support_dual_bank = TRUE;
		self->spec.support_code_size = TRUE;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_HUB] = 0x0000;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_HUB] = 0xB000;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_PD] = 0x16000;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_PD] = 0x23000;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_DEV_BRIDGE] = 0x30000;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_DEV_BRIDGE] = 0x38000;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_CODESIGN] = 0x16000;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_CODESIGN] = 0x17000;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_HUB] = 0xB000;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_PD] = 0xD000;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_DEV_BRIDGE] = 0x8000;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_CODESIGN] = 0x1000;
		self->spec.fw_data_max_count = 0x40000;
		break;
	default:
		break;
	}

	/* add IC product instance */
	fu_device_add_instance_str(FU_DEVICE(self), "IC", project_ic_type);

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_get_info_from_dynamic_ts(FuGenesysUsbhubDevice *self,
						  const guint8 *buf,
						  gsize bufsz,
						  GError **error)
{
	gint ss_port_number = 0;
	gint hs_port_number = 0;
	gchar running_mode = 0;
	guint8 bonding = 0;
	guint8 portnum = 0;
	gboolean flash_dump_location_bit = FALSE;

	/* bonding is not supported */
	if (self->tool_string_version < FU_GENESYS_TS_VERSION_BONDING) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "legacy model 0x%02x not supported",
			    self->spec.chip.model);
		return FALSE;
	}

	/* get running mode, portnum, bonding and flash dump location bit */
	switch (self->spec.chip.model) {
	case ISP_MODEL_HUB_GL3523:
		self->st_dynamic_ts =
		    fu_struct_genesys_ts_dynamic_gl3523_parse(buf, bufsz, 0, error);
		running_mode =
		    fu_struct_genesys_ts_dynamic_gl3523_get_running_mode(self->st_dynamic_ts)[0];
		ss_port_number = fu_genesys_tsdigit_value(
		    fu_struct_genesys_ts_dynamic_gl3523_get_ss_port_number(self->st_dynamic_ts)[0]);
		hs_port_number = fu_genesys_tsdigit_value(
		    fu_struct_genesys_ts_dynamic_gl3523_get_hs_port_number(self->st_dynamic_ts)[0]);
		bonding = fu_genesys_tsdigit_value(
		    fu_struct_genesys_ts_dynamic_gl3523_get_bonding(self->st_dynamic_ts)[0]);
		if (self->tool_string_version < FU_GENESYS_TS_VERSION_BONDING_QC)
			bonding <<= 1;
		self->bonding = bonding & GL3523_BONDING_VALID_BIT;
		flash_dump_location_bit = (bonding & GL3523_BONDING_FLASH_DUMP_LOCATION_BIT) > 0;
		break;
	case ISP_MODEL_HUB_GL3590:
		if (self->spec.chip.revision == 30) {
			self->st_dynamic_ts =
			    fu_struct_genesys_ts_dynamic_gl359030_parse(buf, bufsz, 0, error);
			running_mode = fu_struct_genesys_ts_dynamic_gl359030_get_running_mode(
			    self->st_dynamic_ts)[0];
			ss_port_number = fu_genesys_tsdigit_value(
			    fu_struct_genesys_ts_dynamic_gl359030_get_ss_port_number(
				self->st_dynamic_ts)[0]);
			hs_port_number = fu_genesys_tsdigit_value(
			    fu_struct_genesys_ts_dynamic_gl359030_get_hs_port_number(
				self->st_dynamic_ts)[0]);
			self->bonding =
			    fu_struct_genesys_ts_dynamic_gl359030_get_bonding(self->st_dynamic_ts);
			flash_dump_location_bit =
			    fu_struct_genesys_ts_dynamic_gl359030_get_hub_fw_status(
				self->st_dynamic_ts) == FU_GENESYS_FW_STATUS_BANK2;
		} else {
			self->st_dynamic_ts =
			    fu_struct_genesys_ts_dynamic_gl3590_parse(buf, bufsz, 0, error);
			running_mode = fu_struct_genesys_ts_dynamic_gl3590_get_running_mode(
			    self->st_dynamic_ts)[0];
			ss_port_number = fu_genesys_tsdigit_value(
			    fu_struct_genesys_ts_dynamic_gl3590_get_ss_port_number(
				self->st_dynamic_ts)[0]);
			hs_port_number = fu_genesys_tsdigit_value(
			    fu_struct_genesys_ts_dynamic_gl3590_get_hs_port_number(
				self->st_dynamic_ts)[0]);
			bonding =
			    fu_struct_genesys_ts_dynamic_gl3590_get_bonding(self->st_dynamic_ts);
			self->bonding = bonding & GL3590_BONDING_VALID_BIT;
			flash_dump_location_bit =
			    (bonding & GL3590_BONDING_FLASH_DUMP_LOCATION_BIT) > 0;
		}
		break;
	case ISP_MODEL_HUB_GL3525:
		self->st_dynamic_ts =
		    fu_struct_genesys_ts_dynamic_gl3525_parse(buf, bufsz, 0, error);
		running_mode =
		    fu_struct_genesys_ts_dynamic_gl3525_get_running_mode(self->st_dynamic_ts)[0];
		ss_port_number = fu_genesys_tsdigit_value(
		    fu_struct_genesys_ts_dynamic_gl3525_get_ss_port_number(self->st_dynamic_ts)[0]);
		hs_port_number = fu_genesys_tsdigit_value(
		    fu_struct_genesys_ts_dynamic_gl3525_get_hs_port_number(self->st_dynamic_ts)[0]);
		self->bonding =
		    fu_struct_genesys_ts_dynamic_gl3525_get_bonding(self->st_dynamic_ts);
		flash_dump_location_bit = fu_struct_genesys_ts_dynamic_gl3525_get_hub_fw_status(
					      self->st_dynamic_ts) == FU_GENESYS_FW_STATUS_BANK2;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported model 0x%02x",
			    self->spec.chip.model);
		return FALSE;
	}

	if (running_mode == 'M') {
		self->running_bank = FU_GENESYS_FW_STATUS_MASK;
	} else if (flash_dump_location_bit) {
		self->running_bank = FU_GENESYS_FW_STATUS_BANK2;
	} else {
		self->running_bank = FU_GENESYS_FW_STATUS_BANK1;
	}

	portnum = ss_port_number << 4 | hs_port_number;

	/* add specific product info */
	fu_device_add_instance_u8(FU_DEVICE(self), "PORTNUM", portnum);
	fu_device_add_instance_u8(FU_DEVICE(self), "BONDING", self->bonding);

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	return fu_genesys_usbhub_device_enter_isp_mode(FU_GENESYS_USBHUB_DEVICE(device), error);
}

static gboolean
fu_genesys_usbhub_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	if (!fu_genesys_usbhub_device_reset(self, error))
		return FALSE;

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static GBytes *
fu_genesys_usbhub_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	gsize size = fu_cfi_device_get_size(self->cfi_device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autofree guint8 *buf = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 99, NULL);

	/* require detach -> attach */
	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)fu_device_detach,
					   (FuDeviceLockerFunc)fu_device_attach,
					   error);
	if (locker == NULL)
		return NULL;
	fu_progress_step_done(progress);

	buf = g_malloc0(size);
	if (!fu_genesys_usbhub_device_read_flash(self,
						 0,
						 buf,
						 size,
						 fu_progress_get_child(progress),
						 error))
		return NULL;
	fu_progress_step_done(progress);

	/* success */
	return g_bytes_new_take(g_steal_pointer(&buf), size);
}

static gboolean
fu_genesys_usbhub_device_setup(FuDevice *device, GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	guint32 block_size;
	guint32 sector_size;
	guint8 static_idx = 0;
	guint8 dynamic_idx = 0;
	const gsize bufsz = 0x20;
	g_autoptr(GBytes) static_buf = NULL;
	g_autoptr(GBytes) dynamic_buf = NULL;
	g_autoptr(GBytes) fw_buf = NULL;
	g_autofree guint8 *buf = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_genesys_usbhub_device_parent_class)->setup(device, error)) {
		g_prefix_error(error, "error setuping device: ");
		return FALSE;
	}

	/* [DEBUG] - additional info from device:
	 * release version: g_usb_device_get_release(usb_device)
	 */

	/* read standard string descriptors */
	if (g_usb_device_get_spec(usb_device) >= 0x300) {
		static_idx = GENESYS_USBHUB_STATIC_TOOL_DESC_IDX_USB_3_0;
		dynamic_idx = GENESYS_USBHUB_DYNAMIC_TOOL_DESC_IDX_USB_3_0;
	} else {
		static_idx = GENESYS_USBHUB_STATIC_TOOL_DESC_IDX_USB_2_0;
		dynamic_idx = GENESYS_USBHUB_DYNAMIC_TOOL_DESC_IDX_USB_2_0;
	}

	/*
	 * Read/parse vendor-specific string descriptors and use that
	 * data to setup device attributes.
	 */
	buf = g_malloc0(bufsz);

	/* parse static tool string */
	static_buf =
	    g_usb_device_get_string_descriptor_bytes_full(usb_device,
							  static_idx,
							  G_USB_DEVICE_LANGID_ENGLISH_UNITED_STATES,
							  64,
							  error);
	if (static_buf == NULL) {
		g_prefix_error(error, "failed to get static tool info from device: ");
		return FALSE;
	}
	if (!fu_genesys_usbhub_device_get_descriptor_data(static_buf, buf, bufsz, error)) {
		g_prefix_error(error, "failed to get static tool info from device: ");
		return FALSE;
	}
	if (!fu_genesys_usbhub_device_get_info_from_static_ts(self, buf, bufsz, error))
		return FALSE;

	/* parse dynamic tool string */
	dynamic_buf =
	    g_usb_device_get_string_descriptor_bytes_full(usb_device,
							  dynamic_idx,
							  G_USB_DEVICE_LANGID_ENGLISH_UNITED_STATES,
							  64,
							  error);
	if (dynamic_buf == NULL) {
		g_prefix_error(error, "failed to get dynamic tool info from device: ");
		return FALSE;
	}
	if (!fu_genesys_usbhub_device_get_descriptor_data(dynamic_buf, buf, bufsz, error)) {
		g_prefix_error(error, "failed to get dynamic tool info from device: ");
		return FALSE;
	}
	if (!fu_genesys_usbhub_device_get_info_from_dynamic_ts(self, buf, bufsz, error))
		return FALSE;

	/* parse firmware info tool string */
	fw_buf =
	    g_usb_device_get_string_descriptor_bytes_full(usb_device,
							  GENESYS_USBHUB_FW_INFO_DESC_IDX,
							  G_USB_DEVICE_LANGID_ENGLISH_UNITED_STATES,
							  64,
							  error);
	if (fw_buf == NULL) {
		g_prefix_error(error, "failed to get firmware info from device: ");
		return FALSE;
	}
	if (!fu_genesys_usbhub_device_get_descriptor_data(fw_buf, buf, bufsz, error)) {
		g_prefix_error(error, "failed to get firmware info from device: ");
		return FALSE;
	}
	self->st_fwinfo_ts = fu_struct_genesys_ts_firmware_info_parse(buf, bufsz, 0, error);

	/* parse vendor support tool string */
	if (self->tool_string_version >= FU_GENESYS_TS_VERSION_VENDOR_SUPPORT) {
		g_autoptr(GBytes) vendor_buf = g_usb_device_get_string_descriptor_bytes_full(
		    usb_device,
		    GENESYS_USBHUB_VENDOR_SUPPORT_DESC_IDX,
		    G_USB_DEVICE_LANGID_ENGLISH_UNITED_STATES,
		    64,
		    error);
		if (vendor_buf == NULL) {
			g_prefix_error(error, "failed to get vendor support info from device: ");
			return FALSE;
		}
		if (!fu_genesys_usbhub_device_get_descriptor_data(vendor_buf, buf, bufsz, error)) {
			g_prefix_error(error, "failed to get vendor support info from device: ");
			return FALSE;
		}
		self->st_vendor_ts =
		    fu_struct_genesys_ts_vendor_support_parse(buf, bufsz, 0, error);
	} else {
		self->st_vendor_ts = fu_struct_genesys_ts_vendor_support_new();
	}

	if (fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY))
		self->has_codesign = TRUE;

	/* enter isp mode */
	if (!fu_genesys_usbhub_device_enter_isp_mode(self, error))
		return FALSE;
	/* setup cfi device */
	self->cfi_device = fu_genesys_usbhub_device_cfi_setup(self, error);
	if (self->cfi_device == NULL)
		return FALSE;
	block_size = fu_cfi_device_get_block_size(self->cfi_device);
	if (block_size != 0)
		self->flash_block_size = block_size;
	sector_size = fu_cfi_device_get_sector_size(self->cfi_device);
	if (sector_size != 0)
		self->flash_sector_size = sector_size;

	/* setup firmware parameters */
	fu_device_set_firmware_size_max(
	    device,
	    MIN(self->spec.fw_data_max_count, fu_cfi_device_get_size(self->cfi_device)));

	/* has codesign */
	if (self->has_codesign) {
		FuGenesysFwBank bank = FW_BANK_1;
		switch (self->running_bank) {
		case FU_GENESYS_FW_STATUS_BANK1:
			bank = FW_BANK_1;
			break;
		case FU_GENESYS_FW_STATUS_BANK2:
			bank = FW_BANK_2;
			break;
		default:
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "wrong setting in .quirk, "
					    "mask code does not support codesign");
			return FALSE;
		}

		if (!fu_genesys_usbhub_device_get_public_key(self, bank, error))
			return FALSE;
	}

	/* add specific product info */
	if (self->running_bank != FU_GENESYS_FW_STATUS_MASK) {
		const gchar *vendor = fwupd_device_get_vendor(FWUPD_DEVICE(device));
		g_autofree gchar *guid = NULL;

		guid = fwupd_guid_hash_data(self->st_vendor_ts->data,
					    self->st_vendor_ts->len,
					    FWUPD_GUID_FLAG_NONE);
		fu_device_add_instance_strup(device, "VENDOR", vendor);
		fu_device_add_instance_strup(device, "VENDORSUP", guid);
	}

	fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "IC", NULL);
	fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "IC", "BONDING", NULL);
	fu_device_build_instance_id(device,
				    NULL,
				    "USB",
				    "VID",
				    "PID",
				    "VENDOR",
				    "IC",
				    "BONDING",
				    "PORTNUM",
				    "VENDORSUP",
				    NULL);
	fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "PUBKEY", NULL);

	/* have MStar scaler */
	if (fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_HAS_MSTAR_SCALER))
		if (!fu_genesys_usbhub_device_mstar_scaler_setup(self, error))
			return FALSE;

	/* success */
	return TRUE;
}

static void
fu_genesys_usbhub_device_codesign_to_string(FuDevice *device, guint idt, GString *str)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	guint64 fw_max_size = fu_device_get_firmware_size_max(device);
	guint32 bank_addr1 = self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_CODESIGN];
	guint32 bank_addr2 = self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_CODESIGN];
	guint idt_detail = idt + 1;

	fu_string_append(str, idt, "Codesign", fu_genesys_fw_codesign_to_string(self->codesign));

	if (self->spec.support_dual_bank) {
		fu_string_append_kx(str, idt_detail, "Bank1Addr", bank_addr1);

		if (fw_max_size <= bank_addr2) /* capacity too small */
			return;
		fu_string_append_kx(str, idt_detail, "Bank2Addr", bank_addr2);
	}
}

static void
fu_genesys_usbhub_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	guint64 fw_max_size = fu_device_get_firmware_size_max(device);
	guint idt_detail = idt + 1;
	guint idt_bank_detail = idt_detail + 1;

	fu_string_append(str, idt, "CFI", fu_device_get_name(FU_DEVICE(self->cfi_device)));
	fu_string_append_ku(str, idt_detail, "FlashEraseDelay", self->flash_erase_delay);
	fu_string_append_ku(str, idt_detail, "FlashWriteDelay", self->flash_write_delay);
	fu_string_append_kx(str, idt_detail, "FlashBlockSize", self->flash_block_size);
	fu_string_append_kx(str, idt_detail, "FlashSectorSize", self->flash_sector_size);
	fu_string_append_kx(str, idt_detail, "FlashRwSize", self->flash_rw_size);

	fu_string_append(str,
			 idt,
			 "RunningBank",
			 fu_genesys_fw_status_to_string(self->running_bank));
	fu_string_append_kb(str, idt, "SupportDualBank", self->spec.support_dual_bank);
	fu_string_append_kb(str, idt, "SupportCodeSize", self->spec.support_code_size);

	for (gint i = 0; i < FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT; i++) {
		if (self->spec.fw_bank_capacity[i] == 0 ||		  /* unsupported fw type */
		    fw_max_size <= self->spec.fw_bank_addr[FW_BANK_1][i]) /* capacity too small */
			continue;

		if (i == FU_GENESYS_FW_TYPE_CODESIGN) {
			if (self->has_codesign)
				fu_genesys_usbhub_device_codesign_to_string(device, idt + 1, str);
			continue;
		}

		fu_string_append(str, idt_detail, "FwBank", fu_genesys_fw_type_to_string(i));
		fu_string_append_kx(str,
				    idt_bank_detail,
				    "DataTotalCount",
				    self->spec.fw_bank_capacity[i]);
		fu_string_append_ku(str, idt_bank_detail, "UpdateBank", self->update_fw_banks[i]);

		if (self->spec.chip.model == ISP_MODEL_HUB_GL3523 && i == FU_GENESYS_FW_TYPE_HUB)
			fu_string_append_kb(str,
					    idt_bank_detail,
					    "BackupHubFwBank1",
					    self->backup_hub_fw_bank1);

		if (self->spec.support_dual_bank) {
			fu_string_append_kx(str,
					    idt_bank_detail,
					    "Bank1Addr",
					    self->spec.fw_bank_addr[FW_BANK_1][i]);
			fu_string_append_kx(str,
					    idt_bank_detail,
					    "Bank1Ver",
					    self->fw_bank_vers[FW_BANK_1][i]);
			fu_string_append_kx(str,
					    idt_bank_detail,
					    "Bank1CodeSize",
					    self->fw_bank_code_sizes[FW_BANK_1][i]);

			if (fw_max_size <=
			    self->spec.fw_bank_addr[FW_BANK_2][i]) /* capacity too small */
				continue;

			fu_string_append_kx(str,
					    idt_bank_detail,
					    "Bank2Addr",
					    self->spec.fw_bank_addr[FW_BANK_2][i]);
			fu_string_append_kx(str,
					    idt_bank_detail,
					    "Bank2Ver",
					    self->fw_bank_vers[FW_BANK_2][i]);
			fu_string_append_kx(str,
					    idt_bank_detail,
					    "Bank2CodeSize",
					    self->fw_bank_code_sizes[FW_BANK_2][i]);
		}
	}
}

/**
 * Prepare update
 *
 * Collect dual bank's version. Then select lower version bank to update.
 * When device is running mask code, should not compare and select bank1.
 *
 */
static gboolean
fu_genesys_usbhub_device_prepare(FuDevice *device,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	guint64 fw_max_size = fu_device_get_firmware_size_max(device);

	/* enter isp mode */
	if (!fu_genesys_usbhub_device_enter_isp_mode(self, error))
		return FALSE;

	/* query each fw bank version of fw type */
	for (gint i = 0; i < FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT; i++) {
		if (self->spec.fw_bank_capacity[i] == 0 ||		  /* unsupported fw type */
		    fw_max_size <= self->spec.fw_bank_addr[FW_BANK_1][i]) /* capacity is smaller */
			continue;

		if (self->running_bank == FU_GENESYS_FW_STATUS_MASK || /* both banks are invalid */
		    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE)) {
			self->update_fw_banks[i] = FW_BANK_1;
			continue;
		}

		/* hub & codesign info must at the same bank */
		if (i == FU_GENESYS_FW_TYPE_CODESIGN) {
			self->update_fw_banks[i] = self->update_fw_banks[FU_GENESYS_FW_TYPE_HUB];
			continue;
		}

		if (!fu_genesys_usbhub_device_get_fw_bank_version(self, i, FW_BANK_1, error)) {
			g_prefix_error(error,
				       "error getting %s bank1 version: ",
				       fu_genesys_fw_type_to_string(i));
			return FALSE;
		}
		if (!fu_genesys_usbhub_device_get_fw_bank_version(self, i, FW_BANK_2, error)) {
			g_prefix_error(error,
				       "error getting %s bank2 version: ",
				       fu_genesys_fw_type_to_string(i));
			return FALSE;
		}

		if (self->fw_bank_vers[FW_BANK_1][i] > self->fw_bank_vers[FW_BANK_2][i]) {
			/* bank1 is more recent than bank2: write fw on bank2 */
			if (self->spec.chip.model == ISP_MODEL_HUB_GL3523) {
				/* GL3523 unique dual bank mechanism */
				self->backup_hub_fw_bank1 = TRUE;
				self->update_fw_banks[i] = FW_BANK_1;
			} else {
				self->update_fw_banks[i] = FW_BANK_2;
			}
		} else {
			/* bank2 is more recent than bank1: write fw on bank1 */
			self->update_fw_banks[i] = FW_BANK_1;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_compare_fw_public_key(FuGenesysUsbhubDevice *self,
					       FuFirmware *firmware,
					       GError **error)
{
	FuGenesysFwCodesign codesign_type = FU_GENESYS_FW_CODESIGN_NONE;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GByteArray) st_codesign = NULL;

	g_return_val_if_fail(FU_IS_GENESYS_USBHUB_CODESIGN_FIRMWARE(firmware), FALSE);

	/* compare dev and fw codesign type */
	codesign_type = fu_genesys_usbhub_codesign_firmware_get_codesign(
	    FU_GENESYS_USBHUB_CODESIGN_FIRMWARE(firmware));
	if (codesign_type != self->codesign) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_SIGNATURE_INVALID,
			    "mismatch codesign type %s",
			    fu_genesys_fw_codesign_to_string(codesign_type));
		return FALSE;
	}

	/* get fw codesign info */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL) {
		g_prefix_error(error,
			       "firmware does not have %s bytes: ",
			       fu_genesys_fw_type_to_string(FU_GENESYS_FW_TYPE_CODESIGN));
		return FALSE;
	}

	/* compare dev and fw public-key */
	switch (self->codesign) {
	case FU_GENESYS_FW_CODESIGN_RSA: {
		/* parse and validate */
		st_codesign =
		    fu_struct_genesys_fw_codesign_info_rsa_parse(g_bytes_get_data(blob, NULL),
								 g_bytes_get_size(blob),
								 0x0,
								 error);
		if (st_codesign == NULL)
			return FALSE;
		fu_dump_raw(G_LOG_DOMAIN,
			    "PublicKey",
			    st_codesign->data,
			    FU_STRUCT_GENESYS_FW_CODESIGN_INFO_RSA_SIZE);
		if (memcmp(fu_struct_genesys_fw_codesign_info_rsa_get_text_n(st_codesign),
			   fu_struct_genesys_fw_rsa_public_key_text_get_text_n(self->st_public_key),
			   FU_STRUCT_GENESYS_FW_RSA_PUBLIC_KEY_TEXT_SIZE_TEXT_N) != 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_SIGNATURE_INVALID,
					    "mismatch public-keyN");
			return FALSE;
		}
		if (memcmp(fu_struct_genesys_fw_codesign_info_rsa_get_text_e(st_codesign),
			   fu_struct_genesys_fw_rsa_public_key_text_get_text_e(self->st_public_key),
			   FU_STRUCT_GENESYS_FW_RSA_PUBLIC_KEY_TEXT_SIZE_TEXT_E) != 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_SIGNATURE_INVALID,
					    "mismatch public-keyE");
			return FALSE;
		}
		break;
	}
	case FU_GENESYS_FW_CODESIGN_ECDSA: {
		gsize fw_keysz = 0;
		const guint8 *fw_key;

		/* parse and validate */
		st_codesign =
		    fu_struct_genesys_fw_codesign_info_ecdsa_parse(g_bytes_get_data(blob, NULL),
								   g_bytes_get_size(blob),
								   0x0,
								   error);
		if (st_codesign == NULL)
			return FALSE;

		fw_key = fu_struct_genesys_fw_codesign_info_ecdsa_get_key(st_codesign, &fw_keysz);
		if (fw_keysz != FU_STRUCT_GENESYS_FW_ECDSA_PUBLIC_KEY_SIZE) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_SIGNATURE_INVALID,
					    "mismatch public-key size");
			return FALSE;
		}
		if (memcmp(fw_key,
			   self->st_public_key->data,
			   FU_STRUCT_GENESYS_FW_ECDSA_PUBLIC_KEY_SIZE) != 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_SIGNATURE_INVALID,
					    "mismatch public-key");
			fu_dump_raw(G_LOG_DOMAIN, "PublicKey", fw_key, fw_keysz);
			return FALSE;
		}
		break;
	}
	default:
		break;
	}

	/* does not exist */
	if (st_codesign == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOTHING_TO_DO,
			    "unsupported codesign type %s",
			    fu_genesys_fw_codesign_to_string(codesign_type));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_adjust_fw_addr(FuGenesysUsbhubDevice *self,
					FuFirmware *firmware,
					GError **error)
{
	FuGenesysFwType fw_type = fu_firmware_get_idx(firmware);
	FuGenesysFwBank bank_num;
	guint32 code_size = 0;
	guint32 bank_size = 0;
	g_autoptr(GPtrArray) imgs = NULL;

	/* check supported fw type */
	if (fw_type >= FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unknown firmware type %s",
			    fu_firmware_get_id(firmware));
		return FALSE;
	}

	/* check bank capacity */
	bank_size = self->spec.fw_bank_capacity[fw_type];
	if (bank_size == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device not supported firmware type %s",
			    fu_firmware_get_id(firmware));
		return FALSE;
	}
	code_size = fu_firmware_get_size(firmware);
	if (code_size > bank_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "firmware %s too large, got %#x, expected <= %#x",
			    fu_firmware_get_id(firmware),
			    code_size,
			    bank_size);
		return FALSE;
	}

	/* set update address */
	bank_num = self->update_fw_banks[fw_type];
	if (bank_num >= FW_BANK_COUNT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unknown bank num 0x%x",
			    bank_num);
		return FALSE;
	}
	fu_firmware_set_addr(firmware, self->spec.fw_bank_addr[bank_num][fw_type]);

	/* set child firmware */
	imgs = fu_firmware_get_images(firmware);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_genesys_usbhub_device_adjust_fw_addr(self, img, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_genesys_usbhub_device_prepare_firmware(FuDevice *device,
					  GBytes *fw,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_genesys_usbhub_firmware_new();

	/* parse firmware */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	/* has codesign */
	if (self->has_codesign && (flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		g_autoptr(FuFirmware) img = NULL;

		if (self->st_public_key == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "device does not have public-key");
			return NULL;
		}

		img = fu_firmware_get_image_by_idx(firmware, FU_GENESYS_FW_TYPE_CODESIGN, error);
		if (img == NULL) {
			g_prefix_error(error,
				       "firmware does not have %s: ",
				       fu_genesys_fw_type_to_string(FU_GENESYS_FW_TYPE_CODESIGN));
			return NULL;
		}

		if (!fu_genesys_usbhub_device_compare_fw_public_key(self, img, error))
			return NULL;
	}

	/* set write address into each firmware address */
	if (!fu_genesys_usbhub_device_adjust_fw_addr(self, firmware, error))
		return NULL;

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_genesys_usbhub_device_erase_flash(FuGenesysUsbhubDevice *self,
				     guint start_addr,
				     guint len,
				     FuProgress *progress,
				     GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	FuGenesysWaitFlashRegisterHelper helper = {.reg = 5, .expected_val = 0};
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new(NULL,
				    len,
				    start_addr,
				    self->flash_block_size,
				    self->flash_sector_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint16 sectornum = fu_chunk_get_address(chk) / self->flash_sector_size;
		guint16 blocknum = fu_chunk_get_page(chk);
		guint16 index = (0x01 << 8) | (sectornum << 4) | blocknum;

		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   self->vcs.req_write,
						   0x2001, /* value */
						   index,  /* idx */
						   NULL,   /* data */
						   0,	   /* data length */
						   NULL,   /* actual length */
						   GENESYS_USBHUB_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error,
				       "error erasing flash at sector 0x%02x in block 0x%02x",
				       sectornum,
				       blocknum);
			return FALSE;
		}

		/* 8s */
		if (!fu_device_retry(FU_DEVICE(self),
				     fu_genesys_usbhub_device_wait_flash_status_register_cb,
				     self->flash_erase_delay / 30,
				     &helper,
				     error)) {
			g_prefix_error(error, "error erasing flash: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_write_flash(FuGenesysUsbhubDevice *self,
				     guint start_addr,
				     const guint8 *buf,
				     guint bufsz,
				     FuProgress *progress,
				     GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	FuGenesysWaitFlashRegisterHelper helper = {.reg = 5, .expected_val = 0};
	g_autoptr(GPtrArray) chunks = NULL;

	chunks =
	    fu_chunk_array_new(buf, bufsz, start_addr, self->flash_block_size, self->flash_rw_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autofree guint8 *chkbuf_mut = NULL;

		chkbuf_mut =
		    fu_memdup_safe(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk), error);
		if (chkbuf_mut == NULL)
			return FALSE;
		if (!g_usb_device_control_transfer(usb_device,
						   G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
						   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
						   G_USB_DEVICE_RECIPIENT_DEVICE,
						   self->vcs.req_write,
						   (fu_chunk_get_page(chk) & 0x000f)
						       << 12,				 /* value */
						   fu_chunk_get_address(chk) & 0x00ffff, /* idx */
						   chkbuf_mut,				 /* data */
						   fu_chunk_get_data_sz(chk), /* data length */
						   NULL,		      /* actual length */
						   GENESYS_USBHUB_USB_TIMEOUT,
						   NULL,
						   error)) {
			g_prefix_error(error,
				       "error writing flash at 0x%02x%04x: ",
				       fu_chunk_get_page(chk),
				       fu_chunk_get_address(chk));
			return FALSE;
		}

		/* 5s */
		if (!fu_device_retry(FU_DEVICE(self),
				     fu_genesys_usbhub_device_wait_flash_status_register_cb,
				     self->flash_write_delay / 30,
				     &helper,
				     error)) {
			g_prefix_error(error, "error writing flash: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

/**
 * GL3523 hub default boot up on fw bank1 - even bank2's version is higher. It only boots up
 * on bank2 when bank1 is broken and bank2 is right. Therefore, we want bank1 always be last
 * updated firmware. Also, to fulfill dual bank mechanism, we shall keep at last one fw bank
 * is right. For this purpose, we usually backup bank1 (right fw) to bank2, then update new
 * fw to bank1.
 *
 * GL352350's fw bank stores public-key and code in the same sector and the sector will be
 * erased. Therefore, backup must include public-key.
 */
static gboolean
fu_genesys_usbhub_device_backup_hub_fw_bank1_to_bank2(FuGenesysUsbhubDevice *self,
						      FuProgress *progress,
						      GError **error)
{
	gsize bufsz = 0;
	const guint start_addr = self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_HUB];
	g_autofree guint8 *buf = NULL;

	/* reuse fw on bank1 for GL3523 */
	if (self->hub_fw_bank1_data == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "backup bank does not exist");
		return FALSE;
	}

	if (self->is_gl352350 && self->has_codesign) {
		/* merge hub fw and public-key for GL352350 */
		gsize code_size = 0;
		const guint8 *code_data = g_bytes_get_data(self->hub_fw_bank1_data, &code_size);
		guint32 codesign_offset =
		    self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_CODESIGN];

		if (codesign_offset < code_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOTHING_TO_DO,
				    "hub firmware too large, got 0x%x, expected <= 0x%x",
				    (guint)code_size,
				    codesign_offset);
			return FALSE;
		}
		bufsz = codesign_offset + self->st_public_key->len;
		buf = g_malloc0(bufsz);
		/* copy fw bank data */
		if (!fu_memcpy_safe(buf,
				    bufsz,
				    0, /* dst */
				    code_data,
				    code_size,
				    0x0, /* src */
				    code_size,
				    error))
			return FALSE;
		/* copy public-key */
		if (!fu_memcpy_safe(buf,
				    bufsz,
				    codesign_offset, /* dst */
				    self->st_public_key->data,
				    self->st_public_key->len,
				    0x0, /* src */
				    self->st_public_key->len,
				    error))
			return FALSE;
	} else {
		bufsz = g_bytes_get_size(self->hub_fw_bank1_data);
		buf = fu_memdup_safe(g_bytes_get_data(self->hub_fw_bank1_data, NULL), bufsz, error);
		if (buf == NULL)
			return FALSE;
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 30, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 15, NULL);

	/* erase */
	if (!fu_genesys_usbhub_device_erase_flash(self,
						  start_addr,
						  bufsz,
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify blank */
	if (!fu_genesys_usbhub_device_compare_flash_blank(self,
							  start_addr,
							  bufsz,
							  fu_progress_get_child(progress),
							  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	if (!fu_genesys_usbhub_device_write_flash(self,
						  start_addr,
						  buf,
						  bufsz,
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_genesys_usbhub_device_compare_flash_data(self,
							 start_addr,
							 buf,
							 bufsz,
							 fu_progress_get_child(progress),
							 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_update_firmware(FuGenesysUsbhubDevice *self,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 GError **error)
{
	gboolean skip_erase = FALSE;
	gsize bufsz = 0;
	const guint8 *buf = NULL;
	const guint start_addr = fu_firmware_get_addr(firmware);
	g_autoptr(GBytes) blob = NULL;

	/* get raw data */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	buf = g_bytes_get_data(blob, &bufsz);

	/* already erase at FU_GENESYS_FW_TYPE_HUB before */
	if (self->is_gl352350 && fu_firmware_get_idx(firmware) == FU_GENESYS_FW_TYPE_CODESIGN)
		skip_erase = TRUE;

	/* progress */
	fu_progress_set_id(progress, fu_firmware_get_id(firmware));
	if (skip_erase) {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 1, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 5, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 79, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 15, NULL);
	} else {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 30, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 5, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 15, NULL);
	}

	/* erase */
	if (!skip_erase && !fu_genesys_usbhub_device_erase_flash(self,
								 start_addr,
								 bufsz,
								 fu_progress_get_child(progress),
								 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify blank */
	if (!fu_genesys_usbhub_device_compare_flash_blank(self,
							  start_addr,
							  bufsz,
							  fu_progress_get_child(progress),
							  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	if (!fu_genesys_usbhub_device_write_flash(self,
						  start_addr,
						  buf,
						  bufsz,
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	if (!fu_genesys_usbhub_device_compare_flash_data(self,
							 start_addr,
							 buf,
							 bufsz,
							 fu_progress_get_child(progress),
							 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* enter isp mode */
	if (!fu_genesys_usbhub_device_enter_isp_mode(self, error))
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	if (self->backup_hub_fw_bank1) {
		fu_progress_add_step(progress,
				     FWUPD_STATUS_DEVICE_WRITE,
				     100,
				     "backup hub firmware to bank2");
	}

	fu_progress_add_step(progress,
			     FWUPD_STATUS_DEVICE_WRITE,
			     100,
			     fu_firmware_get_id(firmware));
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		fu_progress_add_step(progress,
				     FWUPD_STATUS_DEVICE_WRITE,
				     100,
				     fu_firmware_get_id(img));
	}

	/* backup fw to bank2 first */
	if (self->backup_hub_fw_bank1) {
		if (!fu_genesys_usbhub_device_backup_hub_fw_bank1_to_bank2(
			self,
			fu_progress_get_child(progress),
			error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* write main firmware */
	if (!fu_genesys_usbhub_device_update_firmware(self,
						      firmware,
						      fu_progress_get_child(progress),
						      error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write child firmware */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_genesys_usbhub_device_update_firmware(self,
							      img,
							      fu_progress_get_child(progress),
							      error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static void
fu_genesys_usbhub_device_set_progress(FuDevice *device, FuProgress *progress)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);

	fu_progress_set_id(progress, G_STRLOC);
	if (self->backup_hub_fw_bank1) {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 30, "write");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 70, "reload");
	} else {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 15, "write");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 85, "reload");
	}
}

static gboolean
fu_genesys_usbhub_device_set_quirk_kv(FuDevice *device,
				      const gchar *key,
				      const gchar *value,
				      GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	guint64 tmp;

	if (g_strcmp0(key, "GenesysUsbhubDeviceTransferSize") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->flash_rw_size = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysUsbhubSwitchRequest") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		self->vcs.req_switch = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysUsbhubReadRequest") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		self->vcs.req_read = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysUsbhubWriteRequest") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		self->vcs.req_write = tmp;

		/* success */
		return TRUE;
	}

	/* failure */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_genesys_usbhub_device_init(FuGenesysUsbhubDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_protocol(FU_DEVICE(self), "com.genesys.usbhub");
	fu_device_retry_set_delay(FU_DEVICE(self), 30);	   /* 30ms */
	fu_device_set_remove_delay(FU_DEVICE(self), 5000); /* 5s */
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_GENESYS_USBHUB_FLAG_HAS_MSTAR_SCALER,
					"has-mstar-scaler");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY,
					"has-public-key");
	fu_device_set_install_duration(FU_DEVICE(self), 9); /* 9 s */

	self->vcs.req_switch = GENESYS_USBHUB_GL_HUB_SWITCH;
	self->vcs.req_read = GENESYS_USBHUB_GL_HUB_READ;
	self->vcs.req_write = GENESYS_USBHUB_GL_HUB_WRITE;
	self->flash_erase_delay = 8000;	  /* 8s */
	self->flash_write_delay = 500;	  /* 500ms */
	self->flash_block_size = 0x10000; /* 64KB */
	self->flash_sector_size = 0x1000; /* 4KB */
	self->flash_rw_size = 0x40;	  /* 64B */
	self->is_gl352350 = FALSE;
	self->has_codesign = FALSE;
	self->codesign = FU_GENESYS_FW_CODESIGN_NONE;
}

static void
fu_genesys_usbhub_device_finalize(GObject *object)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(object);
	if (self->st_static_ts != NULL)
		g_byte_array_unref(self->st_static_ts);
	if (self->st_dynamic_ts != NULL)
		g_byte_array_unref(self->st_dynamic_ts);
	if (self->st_fwinfo_ts != NULL)
		g_byte_array_unref(self->st_fwinfo_ts);
	if (self->st_vendor_ts != NULL)
		g_byte_array_unref(self->st_vendor_ts);
	if (self->hub_fw_bank1_data != NULL)
		g_bytes_unref(self->hub_fw_bank1_data);
	if (self->st_public_key != NULL)
		g_byte_array_unref(self->st_public_key);
	if (self->cfi_device != NULL)
		g_object_unref(self->cfi_device);
	G_OBJECT_CLASS(fu_genesys_usbhub_device_parent_class)->finalize(object);
}

static void
fu_genesys_usbhub_device_class_init(FuGenesysUsbhubDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_genesys_usbhub_device_finalize;
	klass_device->setup = fu_genesys_usbhub_device_setup;
	klass_device->dump_firmware = fu_genesys_usbhub_device_dump_firmware;
	klass_device->prepare = fu_genesys_usbhub_device_prepare;
	klass_device->prepare_firmware = fu_genesys_usbhub_device_prepare_firmware;
	klass_device->write_firmware = fu_genesys_usbhub_device_write_firmware;
	klass_device->set_progress = fu_genesys_usbhub_device_set_progress;
	klass_device->detach = fu_genesys_usbhub_device_detach;
	klass_device->attach = fu_genesys_usbhub_device_attach;
	klass_device->to_string = fu_genesys_usbhub_device_to_string;
	klass_device->set_quirk_kv = fu_genesys_usbhub_device_set_quirk_kv;
}
