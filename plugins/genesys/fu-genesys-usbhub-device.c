/*
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <gusb.h>
#include <string.h>

#include "fu-genesys-common.h"
#include "fu-genesys-scaler-device.h"
#include "fu-genesys-usbhub-device.h"
#include "fu-genesys-usbhub-firmware.h"

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
 * Since 1.7.7
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
	TOOL_STRING_VERSION_9BYTE_DYNAMIC,
	TOOL_STRING_VERSION_BONDING,
	TOOL_STRING_VERSION_BONDING_QC,
	TOOL_STRING_VERSION_VENDOR_SUPPORT,
	TOOL_STRING_VERSION_MULTI_TOKEN,
	TOOL_STRING_VERSION_2ND_DYNAMIC,
	TOOL_STRING_VERSION_RESERVED,
	TOOL_STRING_VERSION_13BYTE_DYNAMIC,
} FuGenesysToolStringVersion;

typedef struct __attribute__((packed)) {
	guint8 running_mode; /* 'M' for mask code, the others for bank code */

	guint8 ss_port_number; /* super-speed port number */
	guint8 hs_port_number; /* high-speed port number */

	guint8 ss_connection_status; /* bit field. ON = DFP is a super-speed device */
	guint8 hs_connection_status; /* bit field. ON = DFP is a high-speed device */
	guint8 fs_connection_status; /* bit field. ON = DFP is a full-speed device */
	guint8 ls_connection_status; /* bit field. ON = DFP is a low-speed device */

	guint8 charging;		  /* bit field. ON = DFP is a charging port */
	guint8 non_removable_port_status; /* bit field. ON = DFP is a non-removable port */

	/*
	 * Bonding reports Hardware register status for GL3523:
	 *   2 / 4 ports         : 1 means 4 ports, 0 means 2 ports
	 *   MTT / STT           : 1 means Multi Token Transfer, 0 means Single TT
	 *   Type - C            : 1 means disable, 0 means enable
	 *   QC                  : 1 means disable, 0 means enable
	 *   Flash dump location : 1 means 32KB offset bank 1, 0 means 0 offset bank 0.
	 *
	 * Tool string Version 1:
	 *   Bit3 : Flash dump location
	 *   BIT2 : Type - C
	 *   BIT1 : MTT / STT
	 *   BIT0 : 2 / 4 ports
	 *
	 * Tool string Version 2 or newer :
	 *   Bit4 : Flash dump location
	 *   BIT3 : Type - C
	 *   BIT2 : MTT / STT
	 *   BIT1 : 2 / 4 ports
	 *   BIT0 : QC
	 *
	 * Default use '0'~'F', plus Bit4 may over value, should extract that.
	 *
	 * Bonding for GL3590:
	 *   Bit7 : Flash dump location, 0 means bank 0, 1 means bank 1.
	 */
	guint8 bonding;

	guint8 reserved[22];
} FuGenesysDynamicToolString;

typedef enum {
	BANK_MASK_CODE,
	BANK_FIRST,
	BANK_SECOND,
} FuGenesysRunningBank;

#define GL3523_BONDING_VALID_BIT 0x0F
#define GL3590_BONDING_VALID_BIT 0x7F

typedef enum {
	ISP_EXIT,
	ISP_ENTER,
} FuGenesysIspMode;

typedef struct __attribute__((packed)) {
	guint8 tool_version[6]; /* ISP tool defined by itself */
	guint8 address_mode;
	guint8 build_fw_time[12];  /* YYYYMMDDhhmm */
	guint8 update_fw_time[12]; /* YYYYMMDDhhmm */
} FuGenesysFirmwareInfoToolString;

typedef struct __attribute__((packed)) {
	guint8 version[2];
	guint8 supports[29];
} FuGenesysVendorSupportToolString;

typedef struct {
	guint8 req_switch;
	guint8 req_read;
	guint8 req_write;
} FuGenesysVendorCommandSetting;

struct _FuGenesysUsbhubDevice {
	FuUsbDevice parent_instance;
	FuGenesysStaticToolString static_ts;
	FuGenesysDynamicToolString dynamic_ts;
	FuGenesysFirmwareInfoToolString fwinfo_ts;
	FuGenesysVendorSupportToolString vs_ts;
	FuGenesysVendorCommandSetting vcs;
	FuGenesysChip chip;
	guint32 running_bank;
	guint8 bonding;
	guint32 flash_erase_delay;
	guint32 flash_write_delay;
	guint32 flash_block_size;
	guint32 flash_sector_size;
	guint32 flash_rw_size;

	guint32 fw_bank_addr[2];
	guint16 fw_bank_vers[2];
	guint32 code_size; /* 0: get from device */
	guint32 fw_data_total_count;
	guint32 extend_size;
	gboolean read_first_bank;
	gboolean write_recovery_bank;

	guint8 public_key[0x212];
	FuCfiDevice *cfi_device;
};

G_DEFINE_TYPE(FuGenesysUsbhubDevice, fu_genesys_usbhub_device, FU_TYPE_USB_DEVICE)

#if G_USB_CHECK_VERSION(0, 3, 8)
static gboolean
fu_genesys_usbhub_device_mstar_scaler_setup(FuGenesysUsbhubDevice *self, GError **error)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	g_autoptr(FuGenesysScalerDevice) scaler_device = fu_genesys_scaler_device_new(ctx);

	fu_device_add_child(FU_DEVICE(self), FU_DEVICE(scaler_device));

	/* success */
	return TRUE;
}
#endif

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

#if G_USB_CHECK_VERSION(0, 3, 8)
static FuCfiDevice *
fu_genesys_usbhub_device_cfi_setup(FuGenesysUsbhubDevice *self, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(self));
	const guint8 rdid_dummy_addr[] = {0x01, 0x02};
	const guint8 rdid_cmd[] = {0x9f, 0x90, 0xAB, 0x1D, 0x15, 0x4D, 0x4B};

	for (guint8 i = 0; i < G_N_ELEMENTS(rdid_cmd); i++) {
		for (guint8 j = 0; j < G_N_ELEMENTS(rdid_dummy_addr); j++) {
			guint16 val = ((guint16)rdid_cmd[i] << 8) | rdid_dummy_addr[j];
			guint8 buf[2 * 3] = {0}; /* 2 x 3-bytes JEDEC-ID-bytes */
			g_autoptr(GError) error_local = NULL;
			g_autoptr(FuCfiDevice) cfi_device = NULL;
			g_autofree gchar *flash_id = NULL;

			if (!g_usb_device_control_transfer(usb_device,
							   G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
							   G_USB_DEVICE_REQUEST_TYPE_VENDOR,
							   G_USB_DEVICE_RECIPIENT_DEVICE,
							   self->vcs.req_read,
							   val,		/* value */
							   0,		/* idx */
							   buf,		/* data */
							   sizeof(buf), /* data length */
							   NULL,	/* actual length */
							   GENESYS_USBHUB_USB_TIMEOUT,
							   NULL,
							   error)) {
				g_prefix_error(error, "error reading flash chip: ");
				return NULL;
			}

			flash_id = g_strdup_printf("%02X%02X%02X", buf[0], buf[1], buf[2]);
			cfi_device =
			    fu_cfi_device_new(fu_device_get_context(FU_DEVICE(self)), flash_id);
			if (cfi_device == NULL)
				continue;

			if (!fu_device_setup(FU_DEVICE(cfi_device), &error_local)) {
				g_debug("ignoring %s: %s", flash_id, error_local->message);
				continue;
			}

			if (fu_device_get_name(FU_DEVICE(cfi_device)) == NULL)
				continue;

			if (g_getenv("FWUPD_GENESYS_USBHUB_VERBOSE") != NULL) {
				guint len;

				/*
				 * The USB vendor command loops over the JEDEC-ID-bytes.
				 *
				 * Therefore, the CFI is 3-bytes long if the first 3-bytes are
				 * identical to the last 3-bytes.
				 */
				if (buf[0] == buf[3] && buf[1] == buf[4] && buf[2] == buf[5])
					len = 3;
				else
					len = 2;

				fu_common_dump_raw(G_LOG_DOMAIN, "Flash ID", buf, len);
				g_debug("CFI: %s", fu_device_get_name(FU_DEVICE(cfi_device)));
			}

			return g_steal_pointer(&cfi_device);
		}
	}

	/* failure */
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no CFI device found");
	return NULL;
}
#endif

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
				    FWUPD_ERROR_INTERNAL,
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
	guint8 *fwinfo = (guint8 *)&self->fwinfo_ts;

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
		temp_byte ^= fwinfo[i];
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
fu_genesys_usbhub_device_probe(FuDevice *device, GError **error)
{
	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS(fu_genesys_usbhub_device_parent_class)->probe(device, error)) {
		g_prefix_error(error, "error probing device: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_open(FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	/* FuUsbDevice->open */
	if (!FU_DEVICE_CLASS(fu_genesys_usbhub_device_parent_class)->open(device, error)) {
		g_prefix_error(error, "error opening device: ");
		return FALSE;
	}

	if (!g_usb_device_claim_interface(usb_device,
					  0,
					  G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					  error)) {
		g_prefix_error(error, "error claiming interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_close(FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));

	if (!g_usb_device_release_interface(usb_device,
					    0,
					    G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					    error)) {
		return FALSE;
	}

	/* FuUsbDevice->close */
	if (!FU_DEVICE_CLASS(fu_genesys_usbhub_device_parent_class)->close(device, error)) {
		g_prefix_error(error, "error closing device: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

#if G_USB_CHECK_VERSION(0, 3, 8)
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
					    int bank_num,
					    GError **error)
{
	guint8 sig[GENESYS_USBHUB_FW_SIG_LEN] = {0};
	g_return_val_if_fail(bank_num < 2, FALSE);

	if (!fu_genesys_usbhub_device_read_flash(self,
						 self->fw_bank_addr[bank_num] +
						     GENESYS_USBHUB_FW_SIG_OFFSET,
						 sig,
						 GENESYS_USBHUB_FW_SIG_LEN,
						 NULL,
						 error)) {
		g_prefix_error(error,
			       "error getting fw signature (bank %d) from device: ",
			       bank_num);
		return FALSE;
	}
	if (memcmp(sig, GENESYS_USBHUB_FW_SIG_TEXT_HUB, GENESYS_USBHUB_FW_SIG_LEN) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "wrong firmware signature");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* read the code size from the firmware stored in the device */
static gboolean
fu_genesys_usbhub_device_get_code_size(FuGenesysUsbhubDevice *self, int bank_num, GError **error)
{
	guint8 kbs = 0;
	g_return_val_if_fail(bank_num < 2, FALSE);

	if (!fu_genesys_usbhub_device_check_fw_signature(self, bank_num, error))
		return FALSE;

	/* get code size from device */
	if (!fu_genesys_usbhub_device_read_flash(self,
						 self->fw_bank_addr[bank_num] +
						     GENESYS_USBHUB_CODE_SIZE_OFFSET,
						 &kbs,
						 1,
						 NULL,
						 error)) {
		g_prefix_error(error, "error getting fw size from device: ");
		return FALSE;
	}
	self->code_size = 1024 * kbs;

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
#endif

static gboolean
fu_genesys_usbhub_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	if (fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY)) {
		if (!fu_genesys_usbhub_device_authenticate(self, error))
			return FALSE;
	}
	if (!fu_genesys_usbhub_device_set_isp_mode(self, ISP_ENTER, error))
		return FALSE;

	/* success */
	return TRUE;
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
	gsize address = self->fw_bank_addr[0];
	gsize size = self->code_size + self->extend_size;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autofree guint8 *buf = NULL;

	if (self->fw_bank_vers[0] == 0 && fu_device_has_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE))
		address = self->fw_bank_addr[1];

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 99);

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
						 address,
						 buf,
						 size,
						 fu_progress_get_child(progress),
						 error))
		return NULL;
	fu_progress_step_done(progress);

	return g_bytes_new_take(g_steal_pointer(&buf), size);
}

static gboolean
fu_genesys_usbhub_device_setup(FuDevice *device, GError **error)
{
#if G_USB_CHECK_VERSION(0, 3, 8)
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	gsize bufsz;
	guint32 block_size;
	guint32 sector_size;
	guint16 version_raw;
	guint8 static_idx = 0;
	guint8 dynamic_idx = 0;
	gchar rev[3] = {0};
	gint tool_string_version = 0;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) static_buf = NULL;
	g_autoptr(GBytes) dynamic_buf = NULL;
	g_autoptr(GBytes) fw_buf = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autofree guint8 *buf = NULL;
	g_autofree gchar *ic_type = NULL;

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
	if (!fu_genesys_usbhub_device_get_descriptor_data(static_buf,
							  (guint8 *)&self->static_ts,
							  sizeof(FuGenesysStaticToolString),
							  error)) {
		g_prefix_error(error, "failed to get static tool info from device: ");
		return FALSE;
	}
	if (g_getenv("FWUPD_GENESYS_USBHUB_VERBOSE") != NULL) {
		fu_common_dump_raw(G_LOG_DOMAIN,
				   "Static info",
				   (guint8 *)&self->static_ts,
				   sizeof(FuGenesysStaticToolString));
	}

	if (memcmp(self->static_ts.mask_project_ic_type, "3523", 4) == 0) {
		self->chip.model = ISP_MODEL_HUB_GL3523;
	} else if (memcmp(self->static_ts.mask_project_ic_type, "3590", 4) == 0) {
		self->chip.model = ISP_MODEL_HUB_GL3590;
	} else {
		ic_type = fu_common_strsafe((const gchar *)&self->static_ts.mask_project_ic_type,
					    sizeof(self->static_ts.mask_project_ic_type));
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "IC type %s not supported",
			    ic_type);
		return FALSE;
	}
	memcpy(rev, &self->static_ts.mask_project_ic_type[4], 2);
	self->chip.revision = fu_common_strtoull(rev);

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
	if (!fu_genesys_usbhub_device_get_descriptor_data(dynamic_buf,
							  (guint8 *)&self->dynamic_ts,
							  sizeof(FuGenesysDynamicToolString),
							  error)) {
		g_prefix_error(error, "failed to get dynamic tool info from device: ");
		return FALSE;
	}
	if (g_getenv("FWUPD_GENESYS_USBHUB_VERBOSE") != NULL) {
		fu_common_dump_raw(G_LOG_DOMAIN,
				   "Dynamic info",
				   (guint8 *)&self->dynamic_ts,
				   sizeof(FuGenesysDynamicToolString));
	}

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
	if (!fu_genesys_usbhub_device_get_descriptor_data(fw_buf,
							  (guint8 *)&self->fwinfo_ts,
							  sizeof(FuGenesysFirmwareInfoToolString),
							  error)) {
		g_prefix_error(error, "failed to get firmware info from device: ");
		return FALSE;
	}
	if (g_getenv("FWUPD_GENESYS_USBHUB_VERBOSE") != NULL) {
		fu_common_dump_raw(G_LOG_DOMAIN,
				   "Fw info",
				   (guint8 *)&self->fwinfo_ts,
				   sizeof(FuGenesysFirmwareInfoToolString));
	}

	tool_string_version = fu_genesys_tsdigit_value(self->static_ts.tool_string_version);
	if (tool_string_version >= TOOL_STRING_VERSION_VENDOR_SUPPORT) {
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
		if (!fu_genesys_usbhub_device_get_descriptor_data(
			vendor_buf,
			(guint8 *)&self->vs_ts,
			sizeof(FuGenesysVendorSupportToolString),
			error)) {
			g_prefix_error(error, "failed to get vendor support info from device: ");
			return FALSE;
		}
		if (g_getenv("FWUPD_GENESYS_USBHUB_VERBOSE") != NULL) {
			fu_common_dump_raw(G_LOG_DOMAIN,
					   "Vendor support",
					   (guint8 *)&self->vs_ts,
					   sizeof(FuGenesysVendorSupportToolString));
		}
	}

	if (fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY)) {
		if (!fu_genesys_usbhub_device_authenticate(self, error))
			return FALSE;
	}
	if (!fu_genesys_usbhub_device_set_isp_mode(self, ISP_ENTER, error))
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
	switch (self->chip.model) {
	case ISP_MODEL_HUB_GL3523: {
		gint bonding = 0;
		self->fw_bank_addr[0] = 0x0000;
		self->fw_bank_addr[1] = 0x8000;

		if (fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY))
			self->extend_size = GL3523_PUBLIC_KEY_LEN + GL3523_SIG_LEN;

		if (self->chip.revision == 50) {
			self->fw_data_total_count = 0x8000;
			if (!fu_genesys_usbhub_device_get_code_size(self, 0, error))
				return FALSE;
		} else {
			self->fw_data_total_count = 0x6000;
			self->code_size = self->fw_data_total_count;
		}

		bonding = fu_genesys_tsdigit_value((gchar)self->dynamic_ts.bonding);
		if (bonding == -1) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "GL3523 bonding value(0x%02x) is wrong",
				    self->dynamic_ts.bonding);
			return FALSE;
		}
		if (tool_string_version < TOOL_STRING_VERSION_BONDING_QC)
			bonding <<= 1;
		self->bonding = (bonding & GL3523_BONDING_VALID_BIT);

		if (self->dynamic_ts.running_mode == 'M') {
			self->running_bank = BANK_MASK_CODE;
		} else if ((bonding & 0x10) > 0) {
			self->running_bank = BANK_SECOND;
		} else {
			self->running_bank = BANK_FIRST;
		}
		break;
	}
	case ISP_MODEL_HUB_GL3590:
		if (!fu_genesys_usbhub_device_get_code_size(self, 0, error))
			return FALSE;
		self->fw_bank_addr[0] = 0x0000;
		self->fw_bank_addr[1] = 0x10000;
		self->fw_data_total_count = 0x10000;
		self->bonding = (self->dynamic_ts.bonding & GL3590_BONDING_VALID_BIT);

		if (self->dynamic_ts.running_mode == 'M') {
			self->running_bank = BANK_MASK_CODE;
		} else if ((self->dynamic_ts.bonding & 0x80) > 0) {
			self->running_bank = BANK_SECOND;
		} else {
			self->running_bank = BANK_FIRST;
		}
		break;
	default:
		break;
	}
	fu_device_set_firmware_size_max(device, self->fw_data_total_count + self->extend_size);

	/* verify firmware integrity */
	bufsz = self->fw_data_total_count + self->extend_size;
	buf = g_malloc0(bufsz);
	if (!fu_genesys_usbhub_device_read_flash(self,
						 self->fw_bank_addr[0],
						 buf,
						 bufsz,
						 NULL,
						 error))
		return FALSE;
	blob = g_bytes_new_take(g_steal_pointer(&buf), bufsz);
	firmware = fu_genesys_usbhub_firmware_new();
	if (!fu_firmware_parse(firmware, blob, FWUPD_INSTALL_FLAG_NONE, &error_local)) {
		g_debug("ignoring firmware: %s", error_local->message);
		self->fw_bank_vers[0] = 0;
	} else {
		version_raw = fu_firmware_get_version_raw(firmware);
		if (version_raw != 0xffff)
			self->fw_bank_vers[0] = version_raw;
	}

	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE)) {
		gsize address = self->fw_bank_addr[0];
		gsize bufsz_dual;
		g_autoptr(FuFirmware) firmware_dual = NULL;
		g_autoptr(GError) error_local_dual = NULL;
		g_autoptr(GBytes) blob_dual = NULL;
		g_autofree guint8 *buf_dual = NULL;

		/* verify dual firmware integrity */
		bufsz_dual = self->fw_data_total_count + self->extend_size;
		buf_dual = g_malloc0(bufsz_dual);
		if (!fu_genesys_usbhub_device_read_flash(self,
							 self->fw_bank_addr[1],
							 buf_dual,
							 bufsz_dual,
							 NULL,
							 error))
			return FALSE;
		blob_dual = g_bytes_new_take(g_steal_pointer(&buf_dual), bufsz_dual);
		firmware_dual = fu_genesys_usbhub_firmware_new();
		if (!fu_firmware_parse(firmware_dual,
				       blob_dual,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error_local_dual)) {
			g_debug("ignoring recovery firmware: %s", error_local_dual->message);
			self->fw_bank_vers[1] = 0;
		} else {
			version_raw = fu_firmware_get_version_raw(firmware_dual);
			if (version_raw != 0xffff)
				self->fw_bank_vers[1] = version_raw;
		}

		/* write recovery needed? */
		if (self->fw_bank_vers[0] == 0 && self->fw_bank_vers[1] == 0) {
			/* first bank and recovery are both blanks: write fw on both */
			address = self->fw_bank_addr[1];
		} else if (self->fw_bank_vers[0] > self->fw_bank_vers[1]) {
			/* first bank is more recent than recovery: write fw on recovery first */
			address = self->fw_bank_addr[1];
		} else {
			/* recovery is more recent than first bank: write fw on first bank only */
			address = self->fw_bank_addr[0];
		}

		self->read_first_bank =
		    (self->chip.model == ISP_MODEL_HUB_GL3523) && self->fw_bank_vers[0] != 0;
		self->write_recovery_bank = address == self->fw_bank_addr[1];
	}

	/* has public key */
	if (fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY)) {
		g_autofree gchar *guid = NULL;
		if (!fu_memcpy_safe(self->public_key,
				    sizeof(self->public_key),
				    0, /* dst */
				    g_bytes_get_data(blob, NULL),
				    g_bytes_get_size(blob),
				    self->fw_data_total_count, /* src */
				    sizeof(self->public_key),
				    error))
			return FALSE;
		guid = fwupd_guid_hash_data(self->public_key,
					    sizeof(self->public_key),
					    FWUPD_GUID_FLAG_NONE);
		fu_device_add_instance_strup(device, "PUBKEY", guid);
	}

	/* add specific product info */
	ic_type = fu_common_strsafe((const gchar *)self->static_ts.mask_project_ic_type,
				    sizeof(self->static_ts.mask_project_ic_type));
	fu_device_add_instance_str(device, "IC", ic_type);
	fu_device_add_instance_u8(device, "BONDING", self->bonding);

	if (self->running_bank != BANK_MASK_CODE) {
		const gchar *vendor = fwupd_device_get_vendor(FWUPD_DEVICE(device));
		guint16 port_num =
		    (self->dynamic_ts.ss_port_number << 8) + self->dynamic_ts.hs_port_number;
		g_autofree gchar *guid = NULL;
		guid = fwupd_guid_hash_data((const guint8 *)&self->vs_ts,
					    sizeof(self->vs_ts),
					    FWUPD_GUID_FLAG_NONE);
		fu_device_add_instance_strup(device, "VENDOR", vendor);
		fu_device_add_instance_u16(device, "PORTNUM", port_num);
		fu_device_add_instance_strup(device, "VENDORSUP", guid);
	}

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
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "GUsb version is too old, "
			    "fwupd needs to be rebuilt against 0.3.8 or later");
	return FALSE;
#endif
}

static void
fu_genesys_usbhub_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	fu_common_string_append_kx(str, idt, "FlashEraseDelay", self->flash_erase_delay);
	fu_common_string_append_kx(str, idt, "FlashWriteDelay", self->flash_write_delay);
	fu_common_string_append_kx(str, idt, "FlashBlockSize", self->flash_block_size);
	fu_common_string_append_kx(str, idt, "FlashSectorSize", self->flash_sector_size);
	fu_common_string_append_kx(str, idt, "FlashRwSize", self->flash_rw_size);
	fu_common_string_append_kx(str, idt, "FwBank0Addr", self->fw_bank_addr[0]);
	fu_common_string_append_kx(str, idt, "FwBank0Vers", self->fw_bank_vers[0]);
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE)) {
		fu_common_string_append_kx(str, idt, "FwBank1Addr", self->fw_bank_addr[1]);
		fu_common_string_append_kx(str, idt, "FwBank1Vers", self->fw_bank_vers[1]);
	}
	fu_common_string_append_kx(str, idt, "CodeSize", self->code_size);
	fu_common_string_append_kx(str, idt, "FwDataTotalCount", self->fw_data_total_count);
	fu_common_string_append_kx(str, idt, "ExtendSize", self->extend_size);
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

	/* has public-key */
	if (g_bytes_get_size(fw) >= fu_firmware_get_size(firmware) + sizeof(self->public_key)) {
		gsize bufsz = 0;
		const guint8 *buf = g_bytes_get_data(fw, &bufsz);

		if (g_getenv("FWUPD_GENESYS_USBHUB_VERBOSE") != NULL)
			fu_common_dump_raw(G_LOG_DOMAIN, "Footer", buf, bufsz);
		if (memcmp(buf + fu_firmware_get_size(firmware),
			   self->public_key,
			   sizeof(self->public_key)) != 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_SIGNATURE_INVALID,
					    "mismatch public-key");
			return NULL;
		}
	}

	/* check size */
	if (g_bytes_get_size(fw) > fu_device_get_firmware_size_max(device)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware too large, got 0x%x, expected <= 0x%x",
			    (guint)g_bytes_get_size(fw),
			    (guint)fu_device_get_firmware_size_max(device));
		return NULL;
	}

	return fu_firmware_new_from_bytes(fw);
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

static gboolean
fu_genesys_usbhub_device_write_recovery(FuGenesysUsbhubDevice *self,
					GBytes *blob,
					FuProgress *progress,
					GError **error)
{
	gsize bufsz = 0;
	g_autofree guint8 *buf = NULL;
	g_autofree guint8 *buf_verify = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	if (self->read_first_bank)
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 20);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 30);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 20);

	/* reuse fw on first bank for GL3523 */
	if (self->read_first_bank) {
		bufsz = self->code_size;
		if (bufsz == 0) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_FAILED,
					    "code size is zero");
			return FALSE;
		}

		buf = g_malloc0(bufsz);
		if (!fu_genesys_usbhub_device_read_flash(self,
							 self->fw_bank_addr[0],
							 buf,
							 bufsz,
							 fu_progress_get_child(progress),
							 error))
			return FALSE;
		fu_progress_step_done(progress);
	} else {
		bufsz = g_bytes_get_size(blob);
		buf = fu_memdup_safe(g_bytes_get_data(blob, NULL), bufsz, error);
		if (buf == NULL)
			return FALSE;
	}

	/* erase */
	if (!fu_genesys_usbhub_device_erase_flash(self,
						  self->fw_bank_addr[1],
						  bufsz,
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write */
	if (!fu_genesys_usbhub_device_write_flash(self,
						  self->fw_bank_addr[1],
						  buf,
						  bufsz,
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	buf_verify = g_malloc0(bufsz);
	if (!fu_genesys_usbhub_device_read_flash(self,
						 self->fw_bank_addr[1],
						 buf_verify,
						 bufsz,
						 fu_progress_get_child(progress),
						 error))
		return FALSE;
	if (!fu_common_bytes_compare_raw(buf_verify, bufsz, buf, bufsz, error))
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
	g_autoptr(GBytes) blob = NULL;
	g_autofree guint8 *buf_verify = NULL;

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	if (self->write_recovery_bank) {
		if (self->read_first_bank)
			fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 120);
		else
			fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100);
	}
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 30);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 20);

	/* write fw to recovery bank first? */
	if (self->write_recovery_bank) {
		if (!fu_genesys_usbhub_device_write_recovery(self,
							     blob,
							     fu_progress_get_child(progress),
							     error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* write fw to first bank then */
	if (!fu_genesys_usbhub_device_erase_flash(self,
						  self->fw_bank_addr[0],
						  g_bytes_get_size(blob),
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_genesys_usbhub_device_write_flash(self,
						  self->fw_bank_addr[0],
						  g_bytes_get_data(blob, NULL),
						  g_bytes_get_size(blob),
						  fu_progress_get_child(progress),
						  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* verify */
	buf_verify = g_malloc0(g_bytes_get_size(blob));
	if (!fu_genesys_usbhub_device_read_flash(self,
						 self->fw_bank_addr[0],
						 buf_verify,
						 g_bytes_get_size(blob),
						 fu_progress_get_child(progress),
						 error))
		return FALSE;
	if (!fu_common_bytes_compare_raw(buf_verify,
					 g_bytes_get_size(blob),
					 g_bytes_get_data(blob, NULL),
					 g_bytes_get_size(blob),
					 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_genesys_usbhub_device_set_progress(FuDevice *device, FuProgress *progress)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);

	fu_progress_set_id(progress, G_STRLOC);
	if (self->write_recovery_bank) {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* detach */
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 30);	/* write */
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* attach */
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 70);	/* reload */
	} else {
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* detach */
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 15);	/* write */
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* attach */
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 85);	/* reload */
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
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->flash_rw_size = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysUsbhubSwitchRequest") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		self->vcs.req_switch = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysUsbhubReadRequest") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		self->vcs.req_read = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysUsbhubWriteRequest") == 0) {
		if (!fu_common_strtoull_full(value, &tmp, 0, G_MAXUINT8, error))
			return FALSE;
		self->vcs.req_write = tmp;

		/* success */
		return TRUE;
	}

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
	fu_device_retry_set_delay(FU_DEVICE(self), 30);	   /* ms */
	fu_device_set_remove_delay(FU_DEVICE(self), 5000); /* ms */
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_GENESYS_USBHUB_FLAG_HAS_MSTAR_SCALER,
					"has-mstar-scaler");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY,
					"has-public-key");
	self->vcs.req_switch = GENESYS_USBHUB_GL_HUB_SWITCH;
	self->vcs.req_read = GENESYS_USBHUB_GL_HUB_READ;
	self->vcs.req_write = GENESYS_USBHUB_GL_HUB_WRITE;
	self->flash_erase_delay = 8000;
	self->flash_write_delay = 500;
	self->flash_block_size = 0x10000; /* 64KB */
	self->flash_sector_size = 0x1000; /* 4KB */
	self->flash_rw_size = 0x40;	  /* 64B */
}

static void
fu_genesys_usbhub_device_class_init(FuGenesysUsbhubDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_genesys_usbhub_device_probe;
	klass_device->open = fu_genesys_usbhub_device_open;
	klass_device->close = fu_genesys_usbhub_device_close;
	klass_device->setup = fu_genesys_usbhub_device_setup;
	klass_device->dump_firmware = fu_genesys_usbhub_device_dump_firmware;
	klass_device->prepare_firmware = fu_genesys_usbhub_device_prepare_firmware;
	klass_device->write_firmware = fu_genesys_usbhub_device_write_firmware;
	klass_device->set_progress = fu_genesys_usbhub_device_set_progress;
	klass_device->detach = fu_genesys_usbhub_device_detach;
	klass_device->attach = fu_genesys_usbhub_device_attach;
	klass_device->to_string = fu_genesys_usbhub_device_to_string;
	klass_device->set_quirk_kv = fu_genesys_usbhub_device_set_quirk_kv;
}
