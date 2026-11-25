/*
 * Copyright 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 * Copyright 2022 Gaël PORTAY <gael.portay@collabora.com>
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-genesys-common.h"
#include "fu-genesys-hubhid-device.h"
#include "fu-genesys-scaler-device.h"
#include "fu-genesys-usbhub-codesign-firmware.h"
#include "fu-genesys-usbhub-device.h"
#include "fu-genesys-usbhub-firmware.h"
#include "fu-genesys-usbhub-struct.h"

/*
 * NOTE: DO NOT ALLOW ANY MORE MAGIC CONSTANTS IN THIS FILE
 * nocheck:magic-inlines=85
 * nocheck:magic-defines=16
 */

#define FU_GENESYS_USBHUB_FLAG_HAS_MSTAR_SCALER "has-mstar-scaler"
#define FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY	"has-public-key"
#define FU_GENESYS_USBHUB_FLAG_IS_GL352350	"is-gl352350" /* unique codesigning */
#define FU_GENESYS_USBHUB_FLAG_HAS_CODESIGN	"has-codesign"
#define FU_GENESYS_USBHUB_FLAG_HAS_HW_CODESIGN	"has-hw-codesign"

#define GENESYS_USBHUB_STATIC_TOOL_DESC_IDX_USB_3_0  0x84
#define GENESYS_USBHUB_DYNAMIC_TOOL_DESC_IDX_USB_3_0 0x85
#define GENESYS_USBHUB_STATIC_TOOL_DESC_IDX_USB_2_0  0x81
#define GENESYS_USBHUB_DYNAMIC_TOOL_DESC_IDX_USB_2_0 0x82
#define GENESYS_USBHUB_FW_INFO_DESC_IDX		     0x83
#define GENESYS_USBHUB_VENDOR_SUPPORT_DESC_IDX	     0x86
#define GENESYS_USBHUB_BRAND_PROJECT_DESC_IDX	     0x8A

#define GENESYS_USBHUB_GL_HUB_VERIFY	  0x71
#define GENESYS_USBHUB_GL_HUB_SWITCH	  0x81
#define GENESYS_USBHUB_GL_HUB_READ	  0x82
#define GENESYS_USBHUB_GL_HUB_WRITE	  0x83
#define GENESYS_USBHUB_GL_HUB_HW_SECURITY 0xAC

#define GENESYS_USBHUB_ENCRYPT_REGION_START 0x01
#define GENESYS_USBHUB_ENCRYPT_REGION_END   0x15

#define GENESYS_USBHUB_USB_TIMEOUT	   5000 /* ms */
#define GENESYS_USBHUB_FLASH_WRITE_TIMEOUT 500	/* ms */

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

typedef enum {
	FW_BANK_1,
	FW_BANK_2,

	FW_BANK_COUNT
} FuGenesysFwBank;

typedef struct {
	FuGenesysChip chip;
	gboolean support_dual_bank;
	gboolean support_code_size;
	guint32 fw_bank_addr[FW_BANK_COUNT][FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT];
	guint32 fw_bank_capacity[FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT];
	guint32 fw_data_max_count;
} FuGenesysIspModelSpec;

struct _FuGenesysUsbhubDevice {
	FuUsbDevice parent_instance;
	FuStructGenesysTsStatic *st_static_ts;
	FuStructGenesysTsFirmwareInfo *st_fwinfo_ts;
	FuStructGenesysTsVendorSupport *st_vendor_ts;
	FuStructGenesysTsBrandProject *st_project_ts;
	FuGenesysVendorCommandSetting vcs;
	FuGenesysIspModelSpec spec;

	FuGenesysTsVersion tool_string_version;
	FuGenesysFwStatus running_bank;
	guint8 bonding;

	FuCfiDevice *cfi_device;
	guint32 flash_erase_delay;
	guint32 flash_write_delay;
	guint32 flash_block_size;
	guint32 flash_sector_size;
	guint32 flash_rw_size;

	guint32 fw_bank_code_sizes[FW_BANK_COUNT][FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT];
	guint16 fw_bank_vers[FW_BANK_COUNT][FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT];
	FuGenesysUsbhubBankState fw_bank_states[FW_BANK_COUNT][FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT];
	FuGenesysFwBank update_fw_banks[FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT];

	/**
	 * GL3523 hub default boot up on fw bank1 - even bank2's version is higher.
	 * It only boots up on bank2 when bank1 is broken and bank2 is right.
	 *
	 * Therefore, we want bank1 always be last updated firmware.
	 * Also, to fulfill dual bank mechanism, we shall keep at last one fw bank is right.
	 *
	 * For this purpose, we usually backup bank1 (right fw) to bank2, and update fw to bank1.
	 * In rare case - boot-up on bank2, we can update fw to bank1 and skip backup.
	 */
	gboolean backup_hub_fw_bank1;
	GBytes *hub_fw_bank1_data; /* restore hub bank1 fw for backup */

	/* codesign info */
	FuGenesysVsCodesignCheck codesign_check;
	FuGenesysFwCodesign codesign;
	GByteArray *st_codesign; /* codesign info, may need to backup for GL352350 */
	FuStructGenesysFwRsaPublicKeyText *st_rsa_pubkey;
	FuStructGenesysFwEcdsaPublicKey *st_ecdsa_pubkey;
	FuStructGenesysFwCodesignInfoEcdsa *st_codesign_ecdsa;
	GByteArray *st_public_key;
	gchar *hid_proxy_id;
	guint16 parent_hub_pid;
	guint16 root_hub_pid;
	FuGenesysUsbhubCtrlHook ctrl_hook;
	gpointer ctrl_hook_data;
};

G_DEFINE_TYPE(FuGenesysUsbhubDevice, fu_genesys_usbhub_device, FU_TYPE_USB_DEVICE)

void
fu_genesys_usbhub_device_set_ctrl_hook(FuGenesysUsbhubDevice *self,
				       FuGenesysUsbhubCtrlHook hook,
				       gpointer user_data)
{
	g_return_if_fail(FU_IS_GENESYS_USBHUB_DEVICE(self));
	self->ctrl_hook = hook;
	self->ctrl_hook_data = user_data;
}

gboolean
fu_genesys_usbhub_device_check_parent_hub(guint16 expected_parent_pid,
					  guint16 hub_vid,
					  FuDevice *parent,
					  GError **error)
{
	guint16 parent_vid;
	guint16 parent_pid;

	if (expected_parent_pid == 0)
		return TRUE;
	if (parent == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "parent hub cannot be resolved");
		return FALSE;
	}
	parent_vid = fu_device_get_vid(parent);
	parent_pid = fu_device_get_pid(parent);
	if (parent_vid != hub_vid || parent_pid != expected_parent_pid) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "parent hub is %04x:%04x, expected %04x:%04x",
			    parent_vid,
			    parent_pid,
			    hub_vid,
			    expected_parent_pid);
		return FALSE;
	}
	return TRUE;
}

/* resolved again on every use; if a logical parent is already linked, verify it matches too */
static gboolean
fu_genesys_usbhub_device_ensure_parent_hub(FuGenesysUsbhubDevice *self, GError **error)
{
	FuDevice *linked_parent;
	g_autoptr(FuDevice) parent = NULL;
	g_autoptr(GError) error_local = NULL;

	if (self->parent_hub_pid == 0)
		return TRUE;
	parent = fu_device_get_backend_parent(FU_DEVICE(self), &error_local);
	if (parent == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "parent hub topology cannot be resolved: %s",
			    error_local->message);
		return FALSE;
	}
	if (!fu_genesys_usbhub_device_check_parent_hub(self->parent_hub_pid,
						       fu_device_get_vid(FU_DEVICE(self)),
						       parent,
						       error))
		return FALSE;

	linked_parent = fu_device_get_parent(FU_DEVICE(self), NULL);
	if (linked_parent != NULL) {
		const gchar *backend_phys = fu_device_get_physical_id(parent);
		const gchar *linked_phys = fu_device_get_physical_id(linked_parent);

		if (fu_device_has_private_flag(linked_parent, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "linked parent hub is no longer connected");
			return FALSE;
		}
		if (backend_phys == NULL || backend_phys[0] == '\0' || linked_phys == NULL ||
		    linked_phys[0] == '\0') {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "parent hub physical ID cannot be verified");
			return FALSE;
		}
		if (fu_device_get_vid(linked_parent) != fu_device_get_vid(parent) ||
		    fu_device_get_pid(linked_parent) != fu_device_get_pid(parent) ||
		    g_strcmp0(linked_phys, backend_phys) != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "linked parent hub (%04x:%04x %s) does not match current USB "
				    "parent (%04x:%04x %s)",
				    fu_device_get_vid(linked_parent),
				    fu_device_get_pid(linked_parent),
				    linked_phys,
				    fu_device_get_vid(parent),
				    fu_device_get_pid(parent),
				    backend_phys);
			return FALSE;
		}
	}
	return TRUE;
}

gboolean
fu_genesys_usbhub_device_check_root_hub(guint16 expected_root_pid,
					guint16 hub_vid,
					FuDevice *device,
					GError **error)
{
	FuDevice *ancestor = device;

	if (expected_root_pid == 0)
		return TRUE;
	for (guint depth = 0; depth < 4; depth++) {
		ancestor = fu_device_get_parent(ancestor, NULL);
		if (ancestor == NULL)
			break;
		if (fu_device_has_private_flag(ancestor, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED))
			break;
		if (fu_device_get_vid(ancestor) == hub_vid &&
		    fu_device_get_pid(ancestor) == expected_root_pid)
			return TRUE;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "hub %04x:%04x is not linked below %04x:%04x, topology cannot be verified",
		    hub_vid,
		    fu_device_get_pid(device),
		    hub_vid,
		    expected_root_pid);
	return FALSE;
}

/* walk the live USB backend tree; when both parent_hub_pid and root_hub_pid are set, require
 * root_hub_pid to be the direct USB parent of the hub's direct USB parent (exact 2-hop chain) */
static gboolean
fu_genesys_usbhub_device_ensure_backend_root_hub(FuGenesysUsbhubDevice *self, GError **error)
{
	guint16 hub_vid = fu_device_get_vid(FU_DEVICE(self));
	g_autoptr(FuDevice) cursor = g_object_ref(FU_DEVICE(self));

	if (self->root_hub_pid == 0)
		return TRUE;
	if (self->parent_hub_pid != 0) {
		g_autoptr(FuDevice) parent = fu_device_get_backend_parent(FU_DEVICE(self), NULL);
		g_autoptr(FuDevice) grandparent = NULL;
		if (parent != NULL)
			grandparent = fu_device_get_backend_parent(parent, NULL);
		if (grandparent != NULL && fu_device_get_vid(grandparent) == hub_vid &&
		    fu_device_get_pid(grandparent) == self->root_hub_pid)
			return TRUE;
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "hub %04x:%04x is not directly below %04x:%04x via its parent hub, "
			    "topology cannot be verified",
			    hub_vid,
			    fu_device_get_pid(FU_DEVICE(self)),
			    hub_vid,
			    self->root_hub_pid);
		return FALSE;
	}
	for (guint depth = 0; depth < 4; depth++) {
		g_autoptr(FuDevice) parent = fu_device_get_backend_parent(cursor, NULL);
		if (parent == NULL)
			break;
		if (fu_device_get_vid(parent) == hub_vid &&
		    fu_device_get_pid(parent) == self->root_hub_pid)
			return TRUE;
		g_set_object(&cursor, parent);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "hub %04x:%04x is not below %04x:%04x, topology cannot be verified",
		    hub_vid,
		    fu_device_get_pid(FU_DEVICE(self)),
		    hub_vid,
		    self->root_hub_pid);
	return FALSE;
}

static gboolean
fu_genesys_usbhub_device_ensure_hub_chain(FuGenesysUsbhubDevice *self, GError **error)
{
	FuDevice *ancestor = FU_DEVICE(self);

	if (!fu_genesys_usbhub_device_ensure_parent_hub(self, error))
		return FALSE;

	/* re-verify every linked Genesys ancestor hub against its current USB backend parent */
	for (guint depth = 0; depth < 4; depth++) {
		ancestor = fu_device_get_parent(ancestor, NULL);
		if (ancestor == NULL)
			break;
		if (FU_IS_GENESYS_USBHUB_DEVICE(ancestor) &&
		    !fu_genesys_usbhub_device_ensure_parent_hub(FU_GENESYS_USBHUB_DEVICE(ancestor),
								error))
			return FALSE;
	}

	if (self->root_hub_pid != 0) {
		if (!fu_genesys_usbhub_device_ensure_backend_root_hub(self, error))
			return FALSE;
		if (fu_device_get_parent(FU_DEVICE(self), NULL) != NULL &&
		    !fu_genesys_usbhub_device_check_root_hub(self->root_hub_pid,
							     fu_device_get_vid(FU_DEVICE(self)),
							     FU_DEVICE(self),
							     error))
			return FALSE;
	}
	return TRUE;
}

gboolean
fu_genesys_usbhub_device_verify_hid_proxy(FuGenesysUsbhubDevice *self,
					  FuDevice *proxy,
					  GError **error)
{
	const gchar *hub_phys;
	const gchar *parent_phys;
	g_autoptr(FuDevice) hid_parent = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_GENESYS_USBHUB_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (proxy == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "HID proxy is required for this hub");
		return FALSE;
	}
	if (fu_device_has_private_flag(proxy, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "HID proxy is no longer connected");
		return FALSE;
	}
	if (self->hid_proxy_id != NULL) {
		g_autofree gchar *actual_id = g_strdup_printf("USB\\VID_%04X&PID_%04X",
							      fu_device_get_vid(proxy),
							      fu_device_get_pid(proxy));
		if (g_ascii_strcasecmp(actual_id, self->hid_proxy_id) != 0 &&
		    !fu_device_has_instance_id(proxy,
					       self->hid_proxy_id,
					       FU_DEVICE_INSTANCE_FLAG_VISIBLE)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "HID proxy %s does not match expected %s",
				    actual_id,
				    self->hid_proxy_id);
			return FALSE;
		}
	}

	/* re-verify the live USB backend parent of the HID proxy on every use */
	hid_parent = fu_device_get_backend_parent(proxy, &error_local);
	if (hid_parent == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "HID proxy topology cannot be resolved: %s",
			    error_local->message);
		return FALSE;
	}
	hub_phys = fu_device_get_physical_id(FU_DEVICE(self));
	parent_phys = fu_device_get_physical_id(hid_parent);
	if (hub_phys == NULL || hub_phys[0] == '\0' || parent_phys == NULL ||
	    parent_phys[0] == '\0') {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "HID proxy parent physical ID cannot be verified");
		return FALSE;
	}
	if (fu_device_get_vid(hid_parent) != fu_device_get_vid(FU_DEVICE(self)) ||
	    fu_device_get_pid(hid_parent) != fu_device_get_pid(FU_DEVICE(self)) ||
	    g_strcmp0(parent_phys, hub_phys) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "HID proxy parent (%04x:%04x %s) does not match hub (%04x:%04x %s)",
			    fu_device_get_vid(hid_parent),
			    fu_device_get_pid(hid_parent),
			    parent_phys,
			    fu_device_get_vid(FU_DEVICE(self)),
			    fu_device_get_pid(FU_DEVICE(self)),
			    hub_phys);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_ensure_update_gate(FuGenesysUsbhubDevice *self, GError **error)
{
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_PROXY_REQUIRED)) {
		FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self), NULL);
		if (!fu_genesys_usbhub_device_verify_hid_proxy(self, proxy, error))
			return FALSE;
	}
	return fu_genesys_usbhub_device_ensure_hub_chain(self, error);
}

guint16
fu_genesys_usbhub_device_get_parent_hub_pid(FuGenesysUsbhubDevice *self)
{
	g_return_val_if_fail(FU_IS_GENESYS_USBHUB_DEVICE(self), 0);
	return self->parent_hub_pid;
}

static gboolean
fu_genesys_usbhub_device_ctrl_transfer(FuGenesysUsbhubDevice *self,
				       FuProgress *progress,
				       FuUsbDirection direction,
				       FuUsbRequestType request_type,
				       FuUsbRecipient recipient,
				       guint8 request,
				       guint16 value,
				       guint16 idx,
				       guint8 *data,
				       gsize length,
				       gsize *actual_length,
				       guint timeout,
				       GError **error)
{
	FuDevice *proxy;

	proxy = fu_device_get_proxy(FU_DEVICE(self), NULL);
	if (proxy != NULL &&
	    fu_device_has_private_flag(proxy, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "HID proxy is no longer connected");
		return FALSE;
	}
	if (proxy == NULL &&
	    fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_PROXY_REQUIRED)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "HID proxy is required for vendor transfers");
		return FALSE;
	}

	if (self->ctrl_hook != NULL)
		return self->ctrl_hook(direction,
				       request,
				       value,
				       data,
				       length,
				       self->ctrl_hook_data,
				       error);

	if (proxy != NULL) {
		return fu_genesys_hubhid_device_send_report(FU_GENESYS_HUBHID_DEVICE(proxy),
							    progress,
							    direction,
							    request_type,
							    recipient,
							    request,
							    value,
							    idx,
							    data,
							    length,
							    error);
	}
	return fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					      direction,
					      request_type,
					      recipient,
					      request,
					      value,
					      idx,
					      data,
					      length,
					      actual_length,
					      timeout,
					      error);
}

static gboolean
fu_genesys_usbhub_device_mstar_scaler_setup(FuGenesysUsbhubDevice *self, GError **error)
{
	g_autoptr(FuGenesysScalerDevice) scaler_device =
	    fu_genesys_scaler_device_new(FU_DEVICE(self));

	fu_device_add_child(FU_DEVICE(self), FU_DEVICE(scaler_device));

	/* success */
	return TRUE;
}

static void
fu_genesys_usbhub_device_set_flash_rw_size(FuGenesysUsbhubDevice *self, guint32 flash_rw_size)
{
	g_return_if_fail(FU_IS_GENESYS_USBHUB_DEVICE(self));
	self->flash_rw_size = flash_rw_size;
}

static gboolean
fu_genesys_usbhub_device_read_flash(FuGenesysUsbhubDevice *self,
				    guint start_addr,
				    guint8 *buf,
				    guint bufsz,
				    FuProgress *progress,
				    GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_mutable_new(buf,
					    bufsz,
					    start_addr,
					    self->flash_block_size,
					    self->flash_rw_size,
					    error);
	if (chunks == NULL)
		return FALSE;
	if (progress != NULL) {
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_set_steps(progress, chunks->len);
	}
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		FuProgress *progress_child = NULL;

		if (progress != NULL)
			progress_child = fu_progress_get_child(progress);

		if (!fu_genesys_usbhub_device_ctrl_transfer(
			self,
			progress_child, /* progress */
			FU_USB_DIRECTION_DEVICE_TO_HOST,
			FU_USB_REQUEST_TYPE_VENDOR,
			FU_USB_RECIPIENT_DEVICE,
			self->vcs.req_read,
			(fu_chunk_get_page(chk) & 0x000f) << 12, /* value */
			fu_chunk_get_address(chk) & 0xffff,	 /* idx */
			fu_chunk_get_data_out(chk),		 /* data */
			fu_chunk_get_data_sz(chk),		 /* data length */
			NULL,					 /* actual length */
			GENESYS_USBHUB_USB_TIMEOUT,
			error)) {
			g_prefix_error(error,
				       "error reading flash at 0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
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
	guint read_addr = 0;
	guint read_size = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GByteArray) read_buf = g_byte_array_new();
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

	fu_byte_array_set_size(read_buf, self->flash_rw_size, 0xFF);
	fu_byte_array_set_size(blank_buf, self->flash_rw_size, 0xFF);

	chunks = fu_chunk_array_new(NULL,
				    read_size,
				    read_addr,
				    self->flash_block_size,
				    self->flash_rw_size,
				    error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!fu_genesys_usbhub_device_ctrl_transfer(
			self,
			fu_progress_get_child(progress), /* progress */
			FU_USB_DIRECTION_DEVICE_TO_HOST,
			FU_USB_REQUEST_TYPE_VENDOR,
			FU_USB_RECIPIENT_DEVICE,
			self->vcs.req_read,
			(fu_chunk_get_page(chk) & 0x000f) << 12, /* value */
			fu_chunk_get_address(chk) & 0xffff,	 /* idx */
			read_buf->data,				 /* data */
			fu_chunk_get_data_sz(chk),		 /* data length */
			NULL,					 /* actual length */
			GENESYS_USBHUB_USB_TIMEOUT,
			error)) {
			g_prefix_error(error,
				       "error reading flash at 0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}
		if (!fu_memcmp_safe(read_buf->data,
				    read_buf->len,
				    0x0,
				    blank_buf->data,
				    blank_buf->len,
				    0x0,
				    fu_chunk_get_data_sz(chk),
				    error)) {
			g_prefix_error(error,
				       "compare flash blank at 0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
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
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GByteArray) read_buf = g_byte_array_new();

	fu_byte_array_set_size(read_buf, self->flash_rw_size, 0xFF);

	chunks = fu_chunk_array_new(buf,
				    bufsz,
				    start_addr,
				    self->flash_block_size,
				    self->flash_rw_size,
				    error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		if (!fu_genesys_usbhub_device_ctrl_transfer(
			self,
			fu_progress_get_child(progress), /* progress */
			FU_USB_DIRECTION_DEVICE_TO_HOST,
			FU_USB_REQUEST_TYPE_VENDOR,
			FU_USB_RECIPIENT_DEVICE,
			self->vcs.req_read,
			(fu_chunk_get_page(chk) & 0x000f) << 12, /* value */
			fu_chunk_get_address(chk) & 0xffff,	 /* idx */
			read_buf->data,				 /* data */
			fu_chunk_get_data_sz(chk),		 /* data length */
			NULL,					 /* actual length */
			GENESYS_USBHUB_USB_TIMEOUT,
			error)) {
			g_prefix_error(error,
				       "error reading flash at 0x%04x: ",
				       (guint)fu_chunk_get_address(chk));
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
				       (guint)fu_chunk_get_address(chk));
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
	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_HOST_TO_DEVICE,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    self->vcs.req_switch,
						    0x0003, /* value */
						    0,	    /* idx */
						    NULL,   /* data */
						    0,	    /* data length */
						    NULL,   /* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
						    error)) {
		g_prefix_error_literal(error, "error resetting device: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static FuCfiDevice *
fu_genesys_usbhub_device_cfi_setup(FuGenesysUsbhubDevice *self, GError **error)
{
	const FuGenesysQueryRdidFormat rdid[] = {
	    {.cmd = 0x1D02, .len = 0x02},
	    {.cmd = 0x4B01, .len = 0x02},
	    {.cmd = 0x9001, .len = 0x02},
	    {.cmd = 0x9f02, .len = 0x03},
	    {.cmd = 0xAB01, .len = 0x02},
	};

	for (gsize i = 0; i < G_N_ELEMENTS(rdid); i++) {
		guint8 buf[3] = {0};
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FuCfiDevice) cfi_device = NULL;
		g_autofree gchar *flash_id = NULL;

		if (!fu_genesys_usbhub_device_ctrl_transfer(self,
							    NULL, /* progress */
							    FU_USB_DIRECTION_DEVICE_TO_HOST,
							    FU_USB_REQUEST_TYPE_VENDOR,
							    FU_USB_RECIPIENT_DEVICE,
							    self->vcs.req_read,
							    rdid[i].cmd, /* value */
							    0,		 /* idx */
							    buf,	 /* data */
							    rdid[i].len, /* data length */
							    NULL,	 /* actual length */
							    GENESYS_USBHUB_USB_TIMEOUT,
							    error)) {
			g_prefix_error_literal(error, "error reading flash chip: ");
			return NULL;
		}

		if (rdid[i].len == 2) {
			flash_id = g_strdup_printf("%02X%02X", buf[0], buf[1]);
		} else if (rdid[i].len == 3) {
			flash_id = g_strdup_printf("%02X%02X%02X", buf[0], buf[1], buf[2]);
		}

		cfi_device = fu_cfi_device_new(FU_DEVICE(self), flash_id);
		if (cfi_device == NULL)
			continue;

		if (!fu_device_setup(FU_DEVICE(cfi_device), &error_local)) {
			g_debug("ignoring %s: %s", flash_id, error_local->message);
			continue;
		}

		if (fu_device_get_name(FU_DEVICE(cfi_device)) == NULL) {
			g_debug("read 0x%x: %s", rdid[i].cmd, flash_id);
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
	guint8 status = 0;
	FuGenesysWaitFlashRegisterHelper *helper = user_data;

	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_DEVICE_TO_HOST,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    self->vcs.req_read,
						    helper->reg << 8 | 0x02, /* value */
						    0,			     /* idx */
						    &status,		     /* data */
						    1,			     /* data length */
						    NULL,		     /* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
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
	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_HOST_TO_DEVICE,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    self->vcs.req_switch,
						    mode, /* value */
						    0,	  /* idx */
						    NULL, /* data */
						    0,	  /* data length */
						    NULL, /* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
						    error)) {
		g_prefix_error(error,
			       "error setting isp mode - "
			       "control transfer error (reg 0x%02x): ",
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
			g_prefix_error_literal(error, "error setting isp mode: ");
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
	guint8 buf = 0;

	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_DEVICE_TO_HOST,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    GENESYS_USBHUB_GL_HUB_VERIFY,
						    (offset_end << 8) | offset_start, /* value */
						    0,				      /* idx */
						    &buf,			      /* data */
						    1,	  /* data length */
						    NULL, /* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
						    error)) {
		g_prefix_error(error,
			       "control transfer error (req: 0x%0x): ",
			       (guint)GENESYS_USBHUB_GL_HUB_VERIFY);
		return FALSE;
	}

	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_DEVICE_TO_HOST,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    GENESYS_USBHUB_GL_HUB_VERIFY,
						    (offset_end << 8) | offset_start, /* value */
						    1 | (data_check << 8),	      /* idx */
						    &buf,			      /* data */
						    1,	  /* data length */
						    NULL, /* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
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
	guint16 release = fu_usb_device_get_release(FU_USB_DEVICE(self));
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

	low_byte = release & 0xff;
	high_byte = (release & 0xff00) >> 8;
	temp_byte = low_byte ^ high_byte;

	offset_start = g_random_int_range(GENESYS_USBHUB_ENCRYPT_REGION_START, /* nocheck:blocked */
					  GENESYS_USBHUB_ENCRYPT_REGION_END - 1);
	offset_end = g_random_int_range(offset_start + 1, /* nocheck:blocked */
					GENESYS_USBHUB_ENCRYPT_REGION_END);
	if (!fu_xor8_safe(self->st_fwinfo_ts->buf->data,
			  self->st_fwinfo_ts->buf->len,
			  offset_start,
			  offset_end - offset_start + 1,
			  &temp_byte,
			  error))
		return FALSE;
	if (!fu_genesys_usbhub_device_authentication_request(self,
							     offset_start,
							     offset_end,
							     temp_byte,
							     error)) {
		g_prefix_error_literal(error, "error authenticating device: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_enter_isp_mode(FuGenesysUsbhubDevice *self, GError **error)
{
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_HAS_CODESIGN)) {
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

	memset(dst, 0, dst_size);

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
	guint8 sig[FU_STRUCT_GENESYS_FIRMWARE_HDR_SIZE_MAGIC] = {0};
	const gchar *sig_txt = NULL;

	g_return_val_if_fail(fw_type < FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT, FALSE);
	g_return_val_if_fail(bank_num < FW_BANK_COUNT, FALSE);

	/* get firmware signature from device */
	if (!fu_genesys_usbhub_device_read_flash(self,
						 self->spec.fw_bank_addr[bank_num][fw_type] +
						     FU_STRUCT_GENESYS_FIRMWARE_HDR_OFFSET_MAGIC,
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
		sig_txt = FU_STRUCT_GENESYS_FIRMWARE_HDR_DEFAULT_MAGIC;
		break;
	case FU_GENESYS_FW_TYPE_DEV_BRIDGE:
		sig_txt = FU_STRUCT_GENESYS_DEV_FIRMWARE_HDR_DEFAULT_MAGIC;
		break;
	case FU_GENESYS_FW_TYPE_PD:
		sig_txt = FU_STRUCT_GENESYS_PD_FIRMWARE_HDR_DEFAULT_MAGIC;
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

gboolean
fu_genesys_usbhub_device_validate_bank_code_size(guint32 code_size,
						 guint32 capacity,
						 GError **error)
{
	if (code_size < sizeof(guint16)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid firmware bank code size 0x%x (minimum 0x%zx)",
			    code_size,
			    sizeof(guint16));
		return FALSE;
	}
	if (capacity > 0 && code_size > capacity) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid firmware bank code size 0x%x exceeds bank capacity 0x%x",
			    code_size,
			    capacity);
		return FALSE;
	}
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
	guint32 code_size;

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
		if (!fu_genesys_usbhub_device_validate_bank_code_size(
			self->spec.fw_bank_capacity[fw_type],
			self->spec.fw_bank_capacity[fw_type],
			error))
			return FALSE;
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
		g_prefix_error_literal(error, "error getting fw size from device: ");
		return FALSE;
	}
	code_size = 1024u * kbs;
	if (!fu_genesys_usbhub_device_validate_bank_code_size(code_size,
							      self->spec.fw_bank_capacity[fw_type],
							      error)) {
		g_prefix_error(error,
			       "firmware %s bank%d: ",
			       fu_genesys_fw_type_to_string(fw_type),
			       bank_num + 1);
		return FALSE;
	}
	self->fw_bank_code_sizes[bank_num][fw_type] = code_size;

	/* success */
	return TRUE;
}

/* read the version from the firmware stored in the device */
/* only a failed signature check says the bank is bad; any other failure says nothing */
FuGenesysUsbhubBankState
fu_genesys_usbhub_device_bank_state_from_error(const GError *error)
{
	if (error == NULL)
		return FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN;
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID))
		return FU_GENESYS_USBHUB_BANK_STATE_INVALID;
	return FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN;
}

/* what a bank image that was read completely says about the firmware: only a checksum that
 * does not match is a proof that it is bad, an image too small to hold the checksum is not */
FuGenesysUsbhubBankState
fu_genesys_usbhub_device_bank_state_from_image(const guint8 *buf, gsize bufsz)
{
	guint16 fw_checksum = 0;

	if (buf == NULL || bufsz < sizeof(fw_checksum))
		return FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN;
	fw_checksum = fu_memread_uint16(buf + bufsz - sizeof(fw_checksum), G_BIG_ENDIAN);
	if (fu_sum16(buf, bufsz - sizeof(fw_checksum)) != fw_checksum)
		return FU_GENESYS_USBHUB_BANK_STATE_INVALID;
	return FU_GENESYS_USBHUB_BANK_STATE_VALID;
}

gboolean
fu_genesys_usbhub_device_select_update_bank2(gboolean is_gl3523,
					     guint16 bank1_version,
					     guint16 bank2_version,
					     gboolean *backup_bank1)
{
	if (backup_bank1 != NULL)
		*backup_bank1 = FALSE;
	if (bank1_version > bank2_version) {
		if (is_gl3523) {
			if (backup_bank1 != NULL)
				*backup_bank1 = TRUE;
			return FALSE;
		}
		return TRUE;
	}
	return FALSE;
}

static gboolean
fu_genesys_usbhub_device_get_fw_bank_version(FuGenesysUsbhubDevice *self,
					     FuGenesysFwType fw_type,
					     int bank_num,
					     FuProgress *progress,
					     GError **error)
{
	gsize bufsz = 0;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autofree guint8 *buf = NULL;

	g_return_val_if_fail(fw_type < FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT, FALSE);
	g_return_val_if_fail(bank_num < FW_BANK_COUNT, FALSE);

	self->fw_bank_states[bank_num][fw_type] = FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN;

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
		FuGenesysUsbhubBankState state =
		    fu_genesys_usbhub_device_bank_state_from_error(error_local);
		if (state != FU_GENESYS_USBHUB_BANK_STATE_INVALID) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		g_debug("ignoring firmware %s bank%d: %s",
			fu_genesys_fw_type_to_string(fw_type),
			bank_num + 1,
			error_local->message);
		self->fw_bank_vers[bank_num][fw_type] = 0;
		self->fw_bank_states[bank_num][fw_type] = state;
		return TRUE;
	}

	/* get bank firmware from device */
	bufsz = self->fw_bank_code_sizes[bank_num][fw_type];
	if (!fu_genesys_usbhub_device_validate_bank_code_size(bufsz,
							      self->spec.fw_bank_capacity[fw_type],
							      error)) {
		g_prefix_error(error,
			       "firmware %s bank%d: ",
			       fu_genesys_fw_type_to_string(fw_type),
			       bank_num + 1);
		return FALSE;
	}
	buf = g_malloc0(bufsz);
	if (!fu_genesys_usbhub_device_read_flash(self,
						 self->spec.fw_bank_addr[bank_num][fw_type],
						 buf,
						 bufsz,
						 progress,
						 error))
		return FALSE;

	/* verify bank firmware integrity */
	blob = g_bytes_new_take(g_steal_pointer(&buf), bufsz);
	switch (fu_genesys_usbhub_device_bank_state_from_image(g_bytes_get_data(blob, NULL),
							       g_bytes_get_size(blob))) {
	case FU_GENESYS_USBHUB_BANK_STATE_VALID:
		break;
	case FU_GENESYS_USBHUB_BANK_STATE_INVALID:
		g_debug("ignoring firmware %s bank%d: checksum mismatch",
			fu_genesys_fw_type_to_string(fw_type),
			bank_num + 1);
		self->fw_bank_vers[bank_num][fw_type] = 0;
		self->fw_bank_states[bank_num][fw_type] = FU_GENESYS_USBHUB_BANK_STATE_INVALID;
		return TRUE;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware %s bank%d is too small to verify",
			    fu_genesys_fw_type_to_string(fw_type),
			    bank_num + 1);
		return FALSE;
	}

	/* get firmware version from bank firmware */
	if (!fu_memread_uint16_safe(g_bytes_get_data(blob, NULL),
				    g_bytes_get_size(blob),
				    GENESYS_USBHUB_VERSION_OFFSET,
				    &self->fw_bank_vers[bank_num][fw_type],
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error_literal(error, "failed to get version: ");
		return FALSE;
	}
	self->fw_bank_states[bank_num][fw_type] = FU_GENESYS_USBHUB_BANK_STATE_VALID;

	/* keep hub bank 1 fw that may needs backup to bank2 later */
	if (self->spec.chip.model == FU_GENESYS_ISP_MODEL_HUB_GL3523 && bank_num == FW_BANK_1)
		self->hub_fw_bank1_data = g_steal_pointer(&blob);

	/* success */
	return TRUE;
}

/* read the public-key from the firmware stored in the device */
static gboolean
fu_genesys_usbhub_device_ensure_public_key(FuGenesysUsbhubDevice *self,
					   int bank_num,
					   GError **error)
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
		g_prefix_error_literal(error, "error getting public-key from device: ");
		return FALSE;
	}

	/* validate and parse public-key */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_HAS_HW_CODESIGN)) {
		/* initiator device has ECDSA key and signature only */
		if (fu_struct_genesys_fw_ecdsa_public_key_validate(buf, bufsz, 0, NULL)) {
			self->codesign = FU_GENESYS_FW_CODESIGN_ECDSA;
			self->st_ecdsa_pubkey =
			    fu_struct_genesys_fw_ecdsa_public_key_parse(buf, bufsz, 0, error);
			if (self->st_ecdsa_pubkey == NULL)
				return FALSE;
			self->st_codesign = g_byte_array_ref(self->st_ecdsa_pubkey->buf);
			self->st_public_key = g_byte_array_ref(self->st_ecdsa_pubkey->buf);
		} else {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "device does not have public-key");
			return FALSE;
		}
	} else {
		if (fu_struct_genesys_fw_rsa_public_key_text_validate(buf, bufsz, 0, NULL)) {
			self->codesign = FU_GENESYS_FW_CODESIGN_RSA;
			self->st_rsa_pubkey =
			    fu_struct_genesys_fw_rsa_public_key_text_parse(buf, bufsz, 0, error);
			if (self->st_rsa_pubkey == NULL)
				return FALSE;
			self->st_codesign = g_byte_array_ref(self->st_rsa_pubkey->buf);
			self->st_public_key = g_byte_array_ref(self->st_rsa_pubkey->buf);
		} else if (fu_struct_genesys_fw_codesign_info_ecdsa_validate(buf, bufsz, 0, NULL)) {
			/* target device has completely ECDSA codesign info */
			gsize keysz = 0;
			const guint8 *key = NULL;

			self->codesign = FU_GENESYS_FW_CODESIGN_ECDSA;
			self->st_codesign_ecdsa =
			    fu_struct_genesys_fw_codesign_info_ecdsa_parse(buf, bufsz, 0, error);
			if (self->st_codesign_ecdsa == NULL)
				return FALSE;
			self->st_codesign = g_byte_array_ref(self->st_codesign_ecdsa->buf);
			key = fu_struct_genesys_fw_codesign_info_ecdsa_get_key(
			    self->st_codesign_ecdsa,
			    &keysz);
			self->st_public_key = g_byte_array_new();
			g_byte_array_append(self->st_public_key, key, keysz);
		} else {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "device does not have public-key");
			return FALSE;
		}
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
fu_genesys_usbhub_device_tsdigit_value(gchar c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 10;
	return g_ascii_digit_value(c);
}

static gboolean
fu_genesys_usbhub_device_validate_token(FuGenesysUsbhubDevice *self, GError **error)
{
	const gchar *valid_tokens[] = {"GL 3.0 Hub", "GL 3.1 Hub", NULL};
	g_autofree gchar *token = NULL;
	g_autoptr(GBytes) token_blob = NULL;
	g_autoptr(GBytes) token_blob_trunc = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get 0x80 string descriptor */
	token_blob =
	    fu_usb_device_get_string_descriptor_bytes_full(FU_USB_DEVICE(self),
							   0x80,
							   FU_USB_LANGID_ENGLISH_UNITED_STATES,
							   64,
							   &error_local);
	if (token_blob == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "cannot get 0x80 string descriptor: %s",
				    error_local->message);
			return FALSE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	token_blob_trunc =
	    fu_bytes_new_offset(token_blob, 0x2, g_bytes_get_size(token_blob) - 0x2, error);
	if (token_blob_trunc == NULL)
		return FALSE;
	token = fu_utf16_to_utf8_bytes(token_blob_trunc, G_LITTLE_ENDIAN, error);
	if (token == NULL)
		return FALSE;

	/* supported strings */
	if (g_strv_contains(valid_tokens, token))
		return TRUE;

	/* failed */
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "is not a valid hub: %s", token);
	return FALSE;
}

static gboolean
fu_genesys_usbhub_device_get_info_from_static_ts(FuGenesysUsbhubDevice *self,
						 const guint8 *buf,
						 gsize bufsz,
						 GError **error)
{
	g_autofree gchar *project_ic_type = NULL;
	g_autofree gchar *project_ic = NULL;

	self->st_static_ts = fu_struct_genesys_ts_static_parse(buf, bufsz, 0, error);
	if (self->st_static_ts == NULL) {
		g_prefix_error_literal(error, "failed to parse static tool info: ");
		return FALSE;
	}

	project_ic_type = fu_struct_genesys_ts_static_get_mask_project_ic_type(self->st_static_ts);
	if (project_ic_type == NULL || strlen(project_ic_type) < 6) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid mask project IC type");
		return FALSE;
	}

	/* verify chip model and revision */
	self->spec.chip.revision = (10 * (project_ic_type[4] - '0')) + (project_ic_type[5] - '0');

	if (memcmp(project_ic_type, "3521", 4) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "ic type %s already EOL and not supported",
			    project_ic_type);
		return FALSE;
	}
	if (memcmp(project_ic_type, "3523", 4) == 0) {
		if (self->spec.chip.revision >= 60) {
			self->spec.chip.model = FU_GENESYS_ISP_MODEL_HUB_GL3523_PLUS;
		} else {
			self->spec.chip.model = FU_GENESYS_ISP_MODEL_HUB_GL3523;
			if (self->spec.chip.revision == 50) {
				fu_device_add_private_flag(FU_DEVICE(self),
							   FU_GENESYS_USBHUB_FLAG_IS_GL352350);
			}
		}
	} else if (memcmp(project_ic_type, "3590", 4) == 0) {
		self->spec.chip.model = FU_GENESYS_ISP_MODEL_HUB_GL3590;
	} else if (memcmp(project_ic_type, "3525", 4) == 0) {
		self->spec.chip.model = FU_GENESYS_ISP_MODEL_HUB_GL3525;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported ic type %s",
			    project_ic_type);
		return FALSE;
	}

	/* convert tool string version */
	self->tool_string_version =
	    fu_struct_genesys_ts_static_get_tool_string_version(self->st_static_ts);

	/* add IC product instance and load quirk kv */
	project_ic = g_strndup(project_ic_type, 4);
	fu_device_add_instance_str(FU_DEVICE(self), "GENESYS_USBHUB\\IC", project_ic);
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "GENESYS_USBHUB",
					      "GENESYS_USBHUB\\IC",
					      NULL))
		return FALSE;

	fu_device_add_instance_str(FU_DEVICE(self), "IC", project_ic_type);
	if (!fu_device_build_instance_id_full(FU_DEVICE(self),
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "GENESYS_USBHUB",
					      "IC",
					      NULL))
		return FALSE;

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
	g_autofree gchar *rm_st = NULL;
	g_autofree gchar *ss_st = NULL;
	g_autofree gchar *hs_st = NULL;
	g_autofree gchar *bonding_st = NULL;

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
	case FU_GENESYS_ISP_MODEL_HUB_GL3523:
	case FU_GENESYS_ISP_MODEL_HUB_GL3523_PLUS: {
		g_autoptr(FuStructGenesysTsDynamicGl3523) st_dynamic_ts = NULL;
		st_dynamic_ts = fu_struct_genesys_ts_dynamic_gl3523_parse(buf, bufsz, 0, error);
		if (st_dynamic_ts == NULL) {
			g_prefix_error_literal(error, "failed to parse dynamic tool info: ");
			return FALSE;
		}

		rm_st = fu_struct_genesys_ts_dynamic_gl3523_get_running_mode(st_dynamic_ts);
		ss_st = fu_struct_genesys_ts_dynamic_gl3523_get_ss_port_number(st_dynamic_ts);
		hs_st = fu_struct_genesys_ts_dynamic_gl3523_get_hs_port_number(st_dynamic_ts);
		bonding_st = fu_struct_genesys_ts_dynamic_gl3523_get_bonding(st_dynamic_ts);

		bonding = fu_genesys_usbhub_device_tsdigit_value(bonding_st[0]);
		if (self->tool_string_version < FU_GENESYS_TS_VERSION_BONDING_QC)
			bonding <<= 1;
		self->bonding = bonding & GL3523_BONDING_VALID_BIT;
		flash_dump_location_bit = (bonding & GL3523_BONDING_FLASH_DUMP_LOCATION_BIT) > 0;
		break;
	}
	case FU_GENESYS_ISP_MODEL_HUB_GL3590: {
		if (self->spec.chip.revision == 30) {
			g_autoptr(FuStructGenesysTsDynamicGl359030) st_dynamic_ts = NULL;
			st_dynamic_ts =
			    fu_struct_genesys_ts_dynamic_gl359030_parse(buf, bufsz, 0, error);
			if (st_dynamic_ts == NULL) {
				g_prefix_error_literal(error,
						       "failed to parse dynamic tool info: ");
				return FALSE;
			}

			rm_st =
			    fu_struct_genesys_ts_dynamic_gl359030_get_running_mode(st_dynamic_ts);
			ss_st =
			    fu_struct_genesys_ts_dynamic_gl359030_get_ss_port_number(st_dynamic_ts);
			hs_st =
			    fu_struct_genesys_ts_dynamic_gl359030_get_hs_port_number(st_dynamic_ts);
			self->bonding =
			    fu_struct_genesys_ts_dynamic_gl359030_get_bonding(st_dynamic_ts);
			flash_dump_location_bit =
			    fu_struct_genesys_ts_dynamic_gl359030_get_hub_fw_status(
				st_dynamic_ts) == FU_GENESYS_FW_STATUS_BANK2;
		} else {
			g_autoptr(FuStructGenesysTsDynamicGl3590) st_dynamic_ts = NULL;
			st_dynamic_ts =
			    fu_struct_genesys_ts_dynamic_gl3590_parse(buf, bufsz, 0, error);
			if (st_dynamic_ts == NULL) {
				g_prefix_error_literal(error,
						       "failed to parse dynamic tool info: ");
				return FALSE;
			}

			rm_st = fu_struct_genesys_ts_dynamic_gl3590_get_running_mode(st_dynamic_ts);
			ss_st =
			    fu_struct_genesys_ts_dynamic_gl3590_get_ss_port_number(st_dynamic_ts);
			hs_st =
			    fu_struct_genesys_ts_dynamic_gl3590_get_hs_port_number(st_dynamic_ts);
			bonding = fu_struct_genesys_ts_dynamic_gl3590_get_bonding(st_dynamic_ts);
			self->bonding = bonding & GL3590_BONDING_VALID_BIT;
			flash_dump_location_bit =
			    (bonding & GL3590_BONDING_FLASH_DUMP_LOCATION_BIT) > 0;
		}
		break;
	}
	case FU_GENESYS_ISP_MODEL_HUB_GL3525: {
		g_autoptr(FuStructGenesysTsDynamicGl3525) st_dynamic_ts = NULL;
		st_dynamic_ts = fu_struct_genesys_ts_dynamic_gl3525_parse(buf, bufsz, 0, error);
		if (st_dynamic_ts == NULL) {
			g_prefix_error_literal(error, "failed to parse dynamic tool info: ");
			return FALSE;
		}

		rm_st = fu_struct_genesys_ts_dynamic_gl3525_get_running_mode(st_dynamic_ts);
		ss_st = fu_struct_genesys_ts_dynamic_gl3525_get_ss_port_number(st_dynamic_ts);
		hs_st = fu_struct_genesys_ts_dynamic_gl3525_get_hs_port_number(st_dynamic_ts);
		self->bonding = fu_struct_genesys_ts_dynamic_gl3525_get_bonding(st_dynamic_ts);
		flash_dump_location_bit = fu_struct_genesys_ts_dynamic_gl3525_get_hub_fw_status(
					      st_dynamic_ts) == FU_GENESYS_FW_STATUS_BANK2;
		break;
	}
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported model 0x%02x",
			    self->spec.chip.model);
		return FALSE;
	}

	running_mode = rm_st[0];
	ss_port_number = fu_genesys_usbhub_device_tsdigit_value(ss_st[0]);
	hs_port_number = fu_genesys_usbhub_device_tsdigit_value(hs_st[0]);

	if (running_mode == 'M') {
		self->running_bank = FU_GENESYS_FW_STATUS_MASK;
	} else if (flash_dump_location_bit) {
		self->running_bank = FU_GENESYS_FW_STATUS_BANK2;
	} else {
		self->running_bank = FU_GENESYS_FW_STATUS_BANK1;
	}

	portnum = ss_port_number << 4 | hs_port_number;

	/* add specific product info */
	fu_device_add_instance_str(FU_DEVICE(self), "RUNMODE", rm_st);
	fu_device_add_instance_u8(FU_DEVICE(self), "PORTNUM", portnum);
	fu_device_add_instance_u8(FU_DEVICE(self), "BONDING", self->bonding);

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_get_info_from_vendor_ts(FuGenesysUsbhubDevice *self,
						 const guint8 *buf,
						 gsize bufsz,
						 GError **error)
{
	self->st_vendor_ts = fu_struct_genesys_ts_vendor_support_parse(buf, bufsz, 0, error);
	if (self->st_vendor_ts == NULL) {
		g_prefix_error_literal(error, "failed to parse vendor support tool info: ");
		return FALSE;
	}

	self->codesign_check =
	    fu_struct_genesys_ts_vendor_support_get_codesign_check(self->st_vendor_ts);

	if (self->codesign_check > FU_GENESYS_VS_CODESIGN_CHECK_UNSUPPORTED) {
		self->vcs.req_switch = 0xA1;
		self->vcs.req_read = 0xA2;
		self->vcs.req_write = 0xA3;
		fu_device_add_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_HAS_CODESIGN);
		if (self->codesign_check == FU_GENESYS_VS_CODESIGN_CHECK_HW) {
			fu_device_add_private_flag(FU_DEVICE(self),
						   FU_GENESYS_USBHUB_FLAG_HAS_HW_CODESIGN);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_get_info_from_project_ts(FuGenesysUsbhubDevice *self,
						  const guint8 *buf,
						  gsize bufsz,
						  GError **error)
{
	g_autofree gchar *guid = NULL;

	self->st_project_ts = fu_struct_genesys_ts_brand_project_parse(buf, bufsz, 0, error);
	if (self->st_project_ts == NULL) {
		g_prefix_error_literal(error, "failed to parse brand project tool info: ");
		return FALSE;
	}

	/* add specific product info */
	guid = fwupd_guid_hash_data(self->st_project_ts->buf->data,
				    self->st_project_ts->buf->len,
				    FWUPD_GUID_FLAG_NONE);
	fu_device_add_instance_strup(FU_DEVICE(self), "PROJECT", guid);

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);

	/* refuse before ISP_ENTER is sent */
	if (!fu_genesys_usbhub_device_ensure_update_gate(self, error))
		return FALSE;
	return fu_genesys_usbhub_device_enter_isp_mode(self, error);
}

static gboolean
fu_genesys_usbhub_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);

	/* verify HID proxy identity before sending vendor reset, without blocking on upstream
	 * hub-chain topology checks needed only for update entries */
	if (fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_PROXY_REQUIRED)) {
		FuDevice *proxy = fu_device_get_proxy(device, NULL);
		if (!fu_genesys_usbhub_device_verify_hid_proxy(self, proxy, error))
			return FALSE;
	}
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
	gsize size;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autofree guint8 *buf = NULL;

	/* refuse before anything is sent, a dump also needs detach -> attach */
	if (!fu_genesys_usbhub_device_ensure_update_gate(self, error))
		return NULL;

	/* sanity check */
	if (self->cfi_device == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no CFI device");
		return NULL;
	}

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

	size = fu_cfi_device_get_size(self->cfi_device);
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
fu_genesys_usbhub_device_ensure_hid_proxy(FuGenesysUsbhubDevice *self, GError **error)
{
	g_autoptr(FuDevice) device_child = NULL;
	g_autoptr(FuDevice) device_usbhub = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	device_child = fu_device_get_backend_child(FU_DEVICE(self),
						   "usb:usb_device",
						   self->hid_proxy_id,
						   error);
	if (device_child == NULL)
		return FALSE;
	device_usbhub =
	    fu_device_incorporate_from_donor(FU_TYPE_GENESYS_HUBHID_DEVICE, device_child, error);
	if (device_usbhub == NULL)
		return FALSE;

	/* probe this "early" so that we can be sure this is our HID proxy */
	locker = fu_device_locker_new(device_usbhub, error);
	if (locker == NULL)
		return FALSE;

	/* align usb max length(0xffff) to usb2 packet size(0x40) */
	fu_genesys_usbhub_device_set_flash_rw_size(self, 0xffc0);
	fu_device_set_proxy(FU_DEVICE(self), device_usbhub);
	fu_device_set_target(device_usbhub, FU_DEVICE(self));

	/* okay for the next *time* we open, but we've already chosen the path now */
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_ensure_hid_proxy_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	return fu_genesys_usbhub_device_ensure_hid_proxy(self, error);
}

static gboolean
fu_genesys_usbhub_device_probe(FuDevice *device, GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS(fu_genesys_usbhub_device_parent_class)->probe(device, error))
		return FALSE;

	if (fu_usb_device_get_class(FU_USB_DEVICE(device)) != FU_USB_CLASS_HUB) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "is not a usb hub");
		return FALSE;
	}
	if (fu_usb_device_get_spec(FU_USB_DEVICE(device)) < 0x210) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported USB2 hub");
		return FALSE;
	}
	if (fu_usb_device_get_spec(FU_USB_DEVICE(device)) >= 0x300) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported USB3 hub");
		return FALSE;
	}

	/* verify upstream parent/root hub chain before probing any child HID or entering ISP */
	if (!fu_genesys_usbhub_device_ensure_hub_chain(self, error))
		return FALSE;

	/* can we find a HID proxy to use? */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_PROXY_REQUIRED)) {
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_genesys_usbhub_device_ensure_hid_proxy_cb,
					  50,
					  100,
					  NULL,
					  error)) {
			g_prefix_error_literal(error, "no suitable HID proxy: ");
			return FALSE;
		}
	} else {
		if (!fu_genesys_usbhub_device_ensure_hid_proxy(self, &error_local))
			g_debug("no suitable HID proxy: %s", error_local->message);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_setup(FuDevice *device, GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
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
		g_prefix_error_literal(error, "error setting up device: ");
		return FALSE;
	}

	/* a hub below the wrong parent or root hub is never talked to or entered into ISP */
	if (!fu_genesys_usbhub_device_ensure_hub_chain(self, error))
		return FALSE;

	/* validate by string token */
	if (!fu_genesys_usbhub_device_validate_token(self, error))
		return FALSE;

	/* [DEBUG] - additional info from device:
	 * release version: fu_usb_device_get_release(FU_USB_DEVICE(device))
	 */

	/* read standard string descriptors */
	if (fu_usb_device_get_spec(FU_USB_DEVICE(device)) >= 0x300) {
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
	    fu_usb_device_get_string_descriptor_bytes_full(FU_USB_DEVICE(device),
							   static_idx,
							   FU_USB_LANGID_ENGLISH_UNITED_STATES,
							   64,
							   error);
	if (static_buf == NULL) {
		g_prefix_error(error,
			       "failed to get static tool info from device (idx=0x%02x): ",
			       static_idx);
		return FALSE;
	}
	if (!fu_genesys_usbhub_device_get_descriptor_data(static_buf, buf, bufsz, error)) {
		g_prefix_error_literal(error, "failed to get static tool info from device: ");
		return FALSE;
	}
	if (!fu_genesys_usbhub_device_get_info_from_static_ts(self, buf, bufsz, error))
		return FALSE;

	/* parse dynamic tool string */
	dynamic_buf =
	    fu_usb_device_get_string_descriptor_bytes_full(FU_USB_DEVICE(device),
							   dynamic_idx,
							   FU_USB_LANGID_ENGLISH_UNITED_STATES,
							   64,
							   error);
	if (dynamic_buf == NULL) {
		g_prefix_error(error,
			       "failed to get dynamic tool info from device (idx=0x%02x): ",
			       dynamic_idx);
		return FALSE;
	}
	if (!fu_genesys_usbhub_device_get_descriptor_data(dynamic_buf, buf, bufsz, error)) {
		g_prefix_error_literal(error, "failed to get dynamic tool info from device: ");
		return FALSE;
	}
	if (!fu_genesys_usbhub_device_get_info_from_dynamic_ts(self, buf, bufsz, error))
		return FALSE;

	/* parse firmware info tool string */
	fw_buf = fu_usb_device_get_string_descriptor_bytes_full(FU_USB_DEVICE(device),
								GENESYS_USBHUB_FW_INFO_DESC_IDX,
								FU_USB_LANGID_ENGLISH_UNITED_STATES,
								64,
								error);
	if (fw_buf == NULL) {
		g_prefix_error_literal(error, "failed to get firmware tool info from device: ");
		return FALSE;
	}
	if (!fu_genesys_usbhub_device_get_descriptor_data(fw_buf, buf, bufsz, error)) {
		g_prefix_error_literal(error, "failed to get firmware tool info from device: ");
		return FALSE;
	}
	self->st_fwinfo_ts = fu_struct_genesys_ts_firmware_info_parse(buf, bufsz, 0, error);
	if (self->st_fwinfo_ts == NULL) {
		g_prefix_error_literal(error, "failed to parse firmware tool info: ");
		return FALSE;
	}

	/* parse vendor support tool string */
	if (self->tool_string_version >= FU_GENESYS_TS_VERSION_VENDOR_SUPPORT) {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GBytes) vendor_buf = fu_usb_device_get_string_descriptor_bytes_full(
		    FU_USB_DEVICE(device),
		    GENESYS_USBHUB_VENDOR_SUPPORT_DESC_IDX,
		    FU_USB_LANGID_ENGLISH_UNITED_STATES,
		    64,
		    error);
		if (vendor_buf == NULL) {
			g_prefix_error_literal(
			    error,
			    "failed to get vendor support tool info from device: ");
			return FALSE;
		}
		if (!fu_genesys_usbhub_device_get_descriptor_data(vendor_buf,
								  buf,
								  bufsz,
								  &error_local)) {
			g_debug("ignoring vendor support tool info: %s", error_local->message);
			self->st_vendor_ts = fu_struct_genesys_ts_vendor_support_new();
		} else if (!fu_genesys_usbhub_device_get_info_from_vendor_ts(self,
									     buf,
									     bufsz,
									     error)) {
			return FALSE;
		}
	} else {
		self->st_vendor_ts = fu_struct_genesys_ts_vendor_support_new();
	}

	/* parse brand project tool string */
	if (self->tool_string_version >= FU_GENESYS_TS_VERSION_BRAND_PROJECT) {
		g_autoptr(GBytes) project_buf = fu_usb_device_get_string_descriptor_bytes_full(
		    FU_USB_DEVICE(device),
		    GENESYS_USBHUB_BRAND_PROJECT_DESC_IDX,
		    FU_USB_LANGID_ENGLISH_UNITED_STATES,
		    64,
		    error);
		if (project_buf == NULL) {
			g_prefix_error_literal(
			    error,
			    "failed to get brand project tool info from device: ");
			return FALSE;
		}
		if (!fu_genesys_usbhub_device_get_descriptor_data(project_buf, buf, bufsz, error)) {
			g_prefix_error_literal(
			    error,
			    "failed to get brand project tool info from device: ");
			return FALSE;
		}
		if (!fu_genesys_usbhub_device_get_info_from_project_ts(self, buf, bufsz, error))
			return FALSE;
	}

	if (fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY))
		fu_device_add_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_HAS_CODESIGN);

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
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_HAS_CODESIGN)) {
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

		if (!fu_genesys_usbhub_device_ensure_public_key(self, bank, error))
			return FALSE;
	}

	/* add specific product info */
	if (self->running_bank != FU_GENESYS_FW_STATUS_MASK) {
		const guint idx = fu_usb_device_get_manufacturer_index(FU_USB_DEVICE(device));
		g_autofree gchar *guid = NULL;

		if (idx != 0x00) {
			/* get manufacturer */
			g_autofree gchar *tmp = NULL;
			g_autoptr(GError) error_local = NULL;
			tmp = fu_usb_device_get_string_descriptor(FU_USB_DEVICE(device),
								  idx,
								  &error_local);
			if (tmp != NULL)
				fu_device_add_instance_strup(device, "VENDOR", g_strchomp(tmp));
			else
				g_debug("failed to load manufacturer string: %s",
					error_local->message);
		} else {
			/* use default vendor */
			fu_device_add_instance_strup(device,
						     "VENDOR",
						     fu_device_get_vendor(device));
		}

		guid = fwupd_guid_hash_data(self->st_vendor_ts->buf->data,
					    self->st_vendor_ts->buf->len,
					    FWUPD_GUID_FLAG_NONE);
		fu_device_add_instance_strup(device, "VENDORSUP", guid);
	}

	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "IC", "BONDING", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device,
					 error,
					 "USB",
					 "VID",
					 "PID",
					 "IC",
					 "BONDING",
					 "RUNMODE",
					 NULL))
		return FALSE;
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
	fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "PROJECT", NULL);

	/* have MStar scaler */
	if (fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_HAS_MSTAR_SCALER))
		if (!fu_genesys_usbhub_device_mstar_scaler_setup(self, error))
			return FALSE;

	/* success */
	return TRUE;
}

static void
fu_genesys_usbhub_device_codesign_to_string(FuGenesysUsbhubDevice *self, guint idt, GString *str)
{
	guint64 fw_max_size = fu_device_get_firmware_size_max(FU_DEVICE(self));
	guint32 bank_addr1 = self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_CODESIGN];
	guint32 bank_addr2 = self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_CODESIGN];
	guint idt_detail = idt + 1;

	fwupd_codec_string_append(str,
				  idt,
				  "Codesign",
				  fu_genesys_fw_codesign_to_string(self->codesign));
	fwupd_codec_string_append(str,
				  idt_detail,
				  "CodesignCheck",
				  fu_genesys_vs_codesign_check_to_string(self->codesign_check));

	if (self->spec.support_dual_bank) {
		fwupd_codec_string_append_hex(str, idt_detail, "Bank1Addr", bank_addr1);

		if (fw_max_size <= bank_addr2) /* capacity too small */
			return;
		fwupd_codec_string_append_hex(str, idt_detail, "Bank2Addr", bank_addr2);
	}
}

static void
fu_genesys_usbhub_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);
	guint64 fw_max_size = fu_device_get_firmware_size_max(device);
	guint idt_detail = idt + 1;
	guint idt_bank_detail = idt_detail + 1;

	if (self->cfi_device != NULL) {
		fwupd_codec_string_append(str,
					  idt,
					  "CFI",
					  fu_device_get_name(FU_DEVICE(self->cfi_device)));
		fwupd_codec_string_append_hex(str,
					      idt_detail,
					      "FlashCapacity",
					      fu_cfi_device_get_size(self->cfi_device));
	}
	fwupd_codec_string_append(str, idt, "HidProxyId", self->hid_proxy_id);
	fwupd_codec_string_append_hex(str, idt, "ParentHubPid", self->parent_hub_pid);
	fwupd_codec_string_append_hex(str, idt, "RootHubPid", self->root_hub_pid);
	fwupd_codec_string_append_int(str, idt_detail, "FlashEraseDelay", self->flash_erase_delay);
	fwupd_codec_string_append_int(str, idt_detail, "FlashWriteDelay", self->flash_write_delay);
	fwupd_codec_string_append_hex(str, idt_detail, "FlashBlockSize", self->flash_block_size);
	fwupd_codec_string_append_hex(str, idt_detail, "FlashSectorSize", self->flash_sector_size);
	fwupd_codec_string_append_hex(str, idt_detail, "FlashRwSize", self->flash_rw_size);

	fwupd_codec_string_append(str,
				  idt,
				  "RunningBank",
				  fu_genesys_fw_status_to_string(self->running_bank));
	fwupd_codec_string_append_bool(str, idt, "SupportDualBank", self->spec.support_dual_bank);
	fwupd_codec_string_append_bool(str, idt, "SupportCodeSize", self->spec.support_code_size);

	for (gint i = 0; i < FU_GENESYS_FW_TYPE_INSIDE_HUB_COUNT; i++) {
		if (self->spec.fw_bank_capacity[i] == 0 ||		  /* unsupported fw type */
		    fw_max_size <= self->spec.fw_bank_addr[FW_BANK_1][i]) /* capacity too small */
			continue;

		if (i == FU_GENESYS_FW_TYPE_CODESIGN) {
			if (fu_device_has_private_flag(FU_DEVICE(self),
						       FU_GENESYS_USBHUB_FLAG_HAS_CODESIGN))
				fu_genesys_usbhub_device_codesign_to_string(self, idt + 1, str);
			continue;
		}

		fwupd_codec_string_append(str,
					  idt_detail,
					  "FwBank",
					  fu_genesys_fw_type_to_string(i));
		fwupd_codec_string_append_hex(str,
					      idt_bank_detail,
					      "DataTotalCount",
					      self->spec.fw_bank_capacity[i]);
		fwupd_codec_string_append_int(str,
					      idt_bank_detail,
					      "UpdateBank",
					      self->update_fw_banks[i]);

		if (self->spec.chip.model == FU_GENESYS_ISP_MODEL_HUB_GL3523 &&
		    i == FU_GENESYS_FW_TYPE_HUB)
			fwupd_codec_string_append_bool(str,
						       idt_bank_detail,
						       "BackupHubFwBank1",
						       self->backup_hub_fw_bank1);

		if (self->spec.support_dual_bank) {
			fwupd_codec_string_append_hex(str,
						      idt_bank_detail,
						      "Bank1Addr",
						      self->spec.fw_bank_addr[FW_BANK_1][i]);
			fwupd_codec_string_append_hex(str,
						      idt_bank_detail,
						      "Bank1Ver",
						      self->fw_bank_vers[FW_BANK_1][i]);
			fwupd_codec_string_append_hex(str,
						      idt_bank_detail,
						      "Bank1CodeSize",
						      self->fw_bank_code_sizes[FW_BANK_1][i]);

			if (fw_max_size <=
			    self->spec.fw_bank_addr[FW_BANK_2][i]) /* capacity too small */
				continue;

			fwupd_codec_string_append_hex(str,
						      idt_bank_detail,
						      "Bank2Addr",
						      self->spec.fw_bank_addr[FW_BANK_2][i]);
			fwupd_codec_string_append_hex(str,
						      idt_bank_detail,
						      "Bank2Ver",
						      self->fw_bank_vers[FW_BANK_2][i]);
			fwupd_codec_string_append_hex(str,
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
	gboolean has_codesign = FALSE;
	guint64 fw_max_size = fu_device_get_firmware_size_max(device);
	g_autoptr(GArray) fw_types = g_array_new(FALSE, FALSE, sizeof(FuGenesysFwType));

	/* refuse before the first command, prepare itself enters isp mode */
	if (!fu_genesys_usbhub_device_ensure_update_gate(self, error))
		return FALSE;
	memset(self->fw_bank_states, 0, sizeof(self->fw_bank_states));

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
			has_codesign = TRUE;
			continue;
		}

		g_array_append_val(fw_types, i);
	}

	if (fw_types->len == 0)
		return TRUE;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 5, "detach");
	for (guint i = 0; i < fw_types->len; i++) {
		fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 100, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 100, NULL);
	}

	/* enter isp mode */
	if (!fu_genesys_usbhub_device_enter_isp_mode(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* query each fw bank version of fw type */
	for (guint i = 0; i < fw_types->len; i++) {
		FuGenesysFwType fw_type = g_array_index(fw_types, FuGenesysFwType, i);

		if (!fu_genesys_usbhub_device_get_fw_bank_version(self,
								  fw_type,
								  FW_BANK_1,
								  fu_progress_get_child(progress),
								  error)) {
			g_prefix_error(error,
				       "error getting %s bank1 version: ",
				       fu_genesys_fw_type_to_string(fw_type));
			return FALSE;
		}
		fu_progress_step_done(progress);

		if (!fu_genesys_usbhub_device_get_fw_bank_version(self,
								  fw_type,
								  FW_BANK_2,
								  fu_progress_get_child(progress),
								  error)) {
			g_prefix_error(error,
				       "error getting %s bank2 version: ",
				       fu_genesys_fw_type_to_string(fw_type));
			return FALSE;
		}
		fu_progress_step_done(progress);

		if (fu_genesys_usbhub_device_select_update_bank2(
			self->spec.chip.model == FU_GENESYS_ISP_MODEL_HUB_GL3523,
			self->fw_bank_vers[FW_BANK_1][fw_type],
			self->fw_bank_vers[FW_BANK_2][fw_type],
			fw_type == FU_GENESYS_FW_TYPE_HUB ? &self->backup_hub_fw_bank1 : NULL)) {
			self->update_fw_banks[fw_type] = FW_BANK_2;
		} else {
			self->update_fw_banks[fw_type] = FW_BANK_1;
		}
	}

	/* hub & codesign info must at the same bank */
	if (has_codesign) {
		self->update_fw_banks[FU_GENESYS_FW_TYPE_CODESIGN] =
		    self->update_fw_banks[FU_GENESYS_FW_TYPE_HUB];
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
	g_autoptr(FuInputStream) stream = NULL;

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
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL) {
		g_prefix_error(error,
			       "firmware does not have %s bytes: ",
			       fu_genesys_fw_type_to_string(FU_GENESYS_FW_TYPE_CODESIGN));
		return FALSE;
	}

	/* compare dev and fw public-key */
	if (self->st_rsa_pubkey != NULL) {
		g_autofree gchar *fw_n = NULL;
		g_autofree gchar *fw_e = NULL;
		g_autofree gchar *dev_n = NULL;
		g_autofree gchar *dev_e = NULL;
		g_autoptr(FuStructGenesysFwCodesignInfoRsa) st_codesign = NULL;

		/* parse and validate */
		st_codesign =
		    fu_struct_genesys_fw_codesign_info_rsa_parse_stream(stream, 0x0, error);
		if (st_codesign == NULL) {
			g_prefix_error(error,
				       "failed to parse %s codesgin: ",
				       fu_genesys_fw_codesign_to_string(codesign_type));
			return FALSE;
		}
		fu_dump_raw(G_LOG_DOMAIN,
			    "PublicKey",
			    st_codesign->buf->data,
			    st_codesign->buf->len);

		fw_n = fu_struct_genesys_fw_codesign_info_rsa_get_text_n(st_codesign);
		dev_n = fu_struct_genesys_fw_rsa_public_key_text_get_text_n(self->st_rsa_pubkey);
		if (!fu_memcmp_safe((const guint8 *)fw_n,
				    FU_STRUCT_GENESYS_FW_CODESIGN_INFO_RSA_SIZE_TEXT_N,
				    0,
				    (const guint8 *)dev_n,
				    FU_STRUCT_GENESYS_FW_RSA_PUBLIC_KEY_TEXT_SIZE_TEXT_N,
				    0,
				    FU_STRUCT_GENESYS_FW_RSA_PUBLIC_KEY_TEXT_SIZE_TEXT_N,
				    error)) {
			g_prefix_error_literal(error, "mismatch public-keyN: ");
			return FALSE;
		}

		fw_e = fu_struct_genesys_fw_codesign_info_rsa_get_text_e(st_codesign);
		dev_e = fu_struct_genesys_fw_rsa_public_key_text_get_text_e(self->st_rsa_pubkey);
		if (!fu_memcmp_safe((const guint8 *)fw_e,
				    FU_STRUCT_GENESYS_FW_CODESIGN_INFO_RSA_SIZE_TEXT_E,
				    0,
				    (const guint8 *)dev_e,
				    FU_STRUCT_GENESYS_FW_RSA_PUBLIC_KEY_TEXT_SIZE_TEXT_E,
				    0,
				    FU_STRUCT_GENESYS_FW_RSA_PUBLIC_KEY_TEXT_SIZE_TEXT_E,
				    error)) {
			g_prefix_error_literal(error, "mismatch public-keyE: ");
			return FALSE;
		}
		return TRUE;
	}

	if (self->codesign == FU_GENESYS_FW_CODESIGN_ECDSA) {
		gsize fw_keysz = 0;
		const guint8 *fw_key = NULL;
		g_autoptr(FuStructGenesysFwCodesignInfoEcdsa) st_codesign = NULL;

		/* parse and validate */
		st_codesign =
		    fu_struct_genesys_fw_codesign_info_ecdsa_parse_stream(stream, 0x0, error);
		if (st_codesign == NULL) {
			g_prefix_error(error,
				       "failed to parse %s codesgin: ",
				       fu_genesys_fw_codesign_to_string(codesign_type));
			return FALSE;
		}

		fw_key = fu_struct_genesys_fw_codesign_info_ecdsa_get_key(st_codesign, &fw_keysz);
		if (fw_keysz != FU_STRUCT_GENESYS_FW_ECDSA_PUBLIC_KEY_SIZE) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_SIGNATURE_INVALID,
					    "mismatch public-key size");
			return FALSE;
		}
		if (!fu_memcmp_safe(fw_key,
				    fw_keysz,
				    0,
				    self->st_public_key->data,
				    self->st_public_key->len,
				    0,
				    FU_STRUCT_GENESYS_FW_ECDSA_PUBLIC_KEY_SIZE,
				    error)) {
			g_prefix_error_literal(error, "mismatch public-key: ");
			fu_dump_raw(G_LOG_DOMAIN, "PublicKey", fw_key, fw_keysz);
			return FALSE;
		}
		return TRUE;
	}

	/* does not exist */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOTHING_TO_DO,
		    "unsupported codesign type %s",
		    fu_genesys_fw_codesign_to_string(codesign_type));
	return FALSE;
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
			    "firmware %s too large, got 0x%x, expected <= 0x%x",
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

/* an erased or unprogrammed identity (0x00/0xFF/spaces) must not count as a match */
static gboolean
fu_genesys_usbhub_device_identity_is_blank(const guint8 *buf, gsize bufsz)
{
	for (gsize i = 0; i < bufsz; i++) {
		if (g_ascii_isalnum(buf[i]))
			return FALSE;
	}
	return TRUE;
}

gboolean
fu_genesys_usbhub_device_check_project_compatibility(gboolean strict,
						     GBytes *device_project,
						     const gchar *device_mask_project_ic_type,
						     GBytes *firmware_project,
						     const gchar *firmware_mask_project_ic_type,
						     GError **error)
{
	gsize device_project_sz = 0;
	gsize firmware_project_sz = 0;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!strict)
		return TRUE;

	if (device_project == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device does not expose PROJECT data");
		return FALSE;
	}
	if (firmware_project == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware does not contain PROJECT data");
		return FALSE;
	}
	if (device_mask_project_ic_type == NULL || strlen(device_mask_project_ic_type) != 6) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device does not expose a valid mask IC type");
		return FALSE;
	}
	if (firmware_mask_project_ic_type == NULL || strlen(firmware_mask_project_ic_type) != 6) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware does not contain a valid mask IC type");
		return FALSE;
	}
	(void)g_bytes_get_data(device_project, &device_project_sz);
	(void)g_bytes_get_data(firmware_project, &firmware_project_sz);
	if (device_project_sz != FU_STRUCT_GENESYS_TS_BRAND_PROJECT_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device PROJECT length is %" G_GSIZE_FORMAT ", expected %u",
			    device_project_sz,
			    (guint)FU_STRUCT_GENESYS_TS_BRAND_PROJECT_SIZE);
		return FALSE;
	}
	if (firmware_project_sz != FU_STRUCT_GENESYS_TS_BRAND_PROJECT_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware PROJECT length is %" G_GSIZE_FORMAT ", expected %u",
			    firmware_project_sz,
			    (guint)FU_STRUCT_GENESYS_TS_BRAND_PROJECT_SIZE);
		return FALSE;
	}
	if (fu_genesys_usbhub_device_identity_is_blank(g_bytes_get_data(device_project, NULL),
						       device_project_sz) ||
	    fu_genesys_usbhub_device_identity_is_blank((const guint8 *)device_mask_project_ic_type,
						       strlen(device_mask_project_ic_type))) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device PROJECT or mask IC type is blank");
		return FALSE;
	}
	if (!g_bytes_equal(device_project, firmware_project)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware PROJECT does not match device");
		return FALSE;
	}
	if (g_strcmp0(device_mask_project_ic_type, firmware_mask_project_ic_type) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware mask IC type %s does not match device %s",
			    firmware_mask_project_ic_type,
			    device_mask_project_ic_type);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_genesys_usbhub_device_check_bank_runs_target(guint16 target_version,
						guint16 other_bank_version,
						GError **error)
{
	g_autofree gchar *target_str = NULL;
	g_autofree gchar *other_str = NULL;

	if (target_version >= other_bank_version)
		return TRUE;
	target_str = fu_version_from_uint16_hex(target_version, FWUPD_VERSION_FORMAT_PAIR);
	other_str = fu_version_from_uint16_hex(other_bank_version, FWUPD_VERSION_FORMAT_PAIR);
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "firmware %s is older than %s in the other bank and this hub runs the newest "
		    "bank: it would not become the running version",
		    target_str,
		    other_str);
	return FALSE;
}

gboolean
fu_genesys_usbhub_device_check_update_bank(gboolean banks_compared,
					   gboolean newest_bank_runs,
					   gboolean update_bank2,
					   FuGenesysUsbhubBankState bank1_state,
					   FuGenesysUsbhubBankState bank2_state,
					   guint16 bank1_version,
					   guint16 bank2_version,
					   gboolean other_bank_runs,
					   guint16 target_version,
					   GError **error)
{
	FuGenesysUsbhubBankState target_state = update_bank2 ? bank2_state : bank1_state;
	FuGenesysUsbhubBankState other_state = update_bank2 ? bank1_state : bank2_state;
	guint16 other_version = update_bank2 ? bank1_version : bank2_version;

	if (!banks_compared)
		return TRUE;
	if (bank1_state == FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN ||
	    bank2_state == FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN) {
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "bank versions were not read, cannot tell which firmware would run");
		return FALSE;
	}
	if (!newest_bank_runs)
		return TRUE;
	if (other_state == FU_GENESYS_USBHUB_BANK_STATE_INVALID && other_bank_runs) {
		g_set_error_literal(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "the running bank failed checksum verification, cannot trust its version");
		return FALSE;
	}
	if (target_state == FU_GENESYS_USBHUB_BANK_STATE_VALID &&
	    other_state == FU_GENESYS_USBHUB_BANK_STATE_INVALID) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "refusing to erase the only checksum-valid firmware bank");
		return FALSE;
	}
	if (other_state == FU_GENESYS_USBHUB_BANK_STATE_INVALID)
		return TRUE;
	return fu_genesys_usbhub_device_check_bank_runs_target(target_version,
							       other_version,
							       error);
}

static gboolean
fu_genesys_usbhub_device_check_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuFirmwareParseFlags flags,
					GError **error)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(device);

	if (fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_STRICT_PROJECT_CHECK)) {
		g_autoptr(GBytes) dev_project = NULL;
		g_autofree gchar *dev_ic = NULL;
		g_autoptr(GBytes) fw_project = NULL;
		g_autofree gchar *fw_ic = NULL;

		if (self->st_project_ts != NULL)
			dev_project = g_bytes_new(self->st_project_ts->buf->data,
						  self->st_project_ts->buf->len);
		if (self->st_static_ts != NULL)
			dev_ic = fu_struct_genesys_ts_static_get_mask_project_ic_type(
			    self->st_static_ts);
		if (FU_IS_GENESYS_USBHUB_FIRMWARE(firmware)) {
			fw_project = fu_genesys_usbhub_firmware_get_project_bytes(
			    FU_GENESYS_USBHUB_FIRMWARE(firmware));
			fw_ic = fu_genesys_usbhub_firmware_get_mask_project_ic_type(
			    FU_GENESYS_USBHUB_FIRMWARE(firmware));
		}
		if (!fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									  dev_project,
									  dev_ic,
									  fw_project,
									  fw_ic,
									  error))
			return FALSE;
	}

	/* has codesign */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_HAS_CODESIGN)) {
		g_autoptr(FuFirmware) img = NULL;

		if (self->st_public_key == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "device does not have public-key");
			return FALSE;
		}

		img = fu_firmware_get_image_by_idx(firmware, FU_GENESYS_FW_TYPE_CODESIGN, error);
		if (img == NULL) {
			g_prefix_error(error,
				       "firmware does not have %s: ",
				       fu_genesys_fw_type_to_string(FU_GENESYS_FW_TYPE_CODESIGN));
			return FALSE;
		}

		if (!fu_genesys_usbhub_device_compare_fw_public_key(self, img, error))
			return FALSE;
	}

	/* set write address into each firmware address */
	if (!fu_genesys_usbhub_device_adjust_fw_addr(self, firmware, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_erase_flash(FuGenesysUsbhubDevice *self,
				     guint start_addr,
				     guint len,
				     FuProgress *progress,
				     GError **error)
{
	FuGenesysWaitFlashRegisterHelper helper = {.reg = 5, .expected_val = 0};
	g_autoptr(GPtrArray) chunks = NULL;

	/* validate sector size */
	if (self->flash_sector_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "flash sector size not set");
		return FALSE;
	}

	chunks = fu_chunk_array_new(NULL,
				    len,
				    start_addr,
				    self->flash_block_size,
				    self->flash_sector_size,
				    error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint16 sectornum = fu_chunk_get_address(chk) / self->flash_sector_size;
		guint16 blocknum = fu_chunk_get_page(chk);
		guint16 index = (0x01 << 8) | (sectornum << 4) | blocknum;

		if (!fu_genesys_usbhub_device_ctrl_transfer(self,
							    NULL, /* progress */
							    FU_USB_DIRECTION_HOST_TO_DEVICE,
							    FU_USB_REQUEST_TYPE_VENDOR,
							    FU_USB_RECIPIENT_DEVICE,
							    self->vcs.req_write,
							    0x2001, /* value */
							    index,  /* idx */
							    NULL,   /* data */
							    0,	    /* data length */
							    NULL,   /* actual length */
							    GENESYS_USBHUB_USB_TIMEOUT,
							    error)) {
			g_prefix_error(error,
				       "error erasing flash at sector 0x%02x in block 0x%02x: ",
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
			g_prefix_error_literal(error, "error erasing flash: ");
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
	FuGenesysWaitFlashRegisterHelper helper = {.reg = 5, .expected_val = 0};
	g_autoptr(GPtrArray) chunks = NULL;

	chunks = fu_chunk_array_new(buf,
				    bufsz,
				    start_addr,
				    self->flash_block_size,
				    self->flash_rw_size,
				    error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_autoptr(GByteArray) buf_write = g_byte_array_new();

		g_byte_array_append(buf_write, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));

		if (!fu_genesys_usbhub_device_ctrl_transfer(
			self,
			fu_progress_get_child(progress), /* progress */
			FU_USB_DIRECTION_HOST_TO_DEVICE,
			FU_USB_REQUEST_TYPE_VENDOR,
			FU_USB_RECIPIENT_DEVICE,
			self->vcs.req_write,
			(fu_chunk_get_page(chk) & 0x000f) << 12, /* value */
			fu_chunk_get_address(chk) & 0xffff,	 /* idx */
			buf_write->data,			 /* data */
			buf_write->len,				 /* data length */
			NULL,					 /* actual length */
			GENESYS_USBHUB_USB_TIMEOUT,
			error)) {
			g_prefix_error(error,
				       "error writing flash at 0x%02x%04x: ",
				       fu_chunk_get_page(chk),
				       (guint)fu_chunk_get_address(chk));
			return FALSE;
		}

		/* 5s */
		if (!fu_device_retry(FU_DEVICE(self),
				     fu_genesys_usbhub_device_wait_flash_status_register_cb,
				     self->flash_write_delay / 30,
				     &helper,
				     error)) {
			g_prefix_error_literal(error, "error writing flash: ");
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
 * GL352350's fw bank stores codesign info and code in the same sector and the sector will
 * be erased. Therefore, backup must include codesign info.
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

	if (fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_IS_GL352350) &&
	    fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_HAS_CODESIGN)) {
		/* merge hub fw and codesign info for GL352350 */
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
		bufsz = codesign_offset + self->st_codesign->len;
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
		/* copy codesign info */
		if (!fu_memcpy_safe(buf,
				    bufsz,
				    codesign_offset, /* dst */
				    self->st_codesign->data,
				    self->st_codesign->len,
				    0x0, /* src */
				    self->st_codesign->len,
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

	if (fu_firmware_get_idx(firmware) == FU_GENESYS_FW_TYPE_CODESIGN) {
		/* already erase at FU_GENESYS_FW_TYPE_HUB before */
		if (fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_IS_GL352350))
			skip_erase = TRUE;

		/* jump ECDSA hash on HW codesign */
		if (fu_device_has_private_flag(FU_DEVICE(self),
					       FU_GENESYS_USBHUB_FLAG_HAS_HW_CODESIGN) &&
		    self->codesign == FU_GENESYS_FW_CODESIGN_ECDSA) {
			if (bufsz != FU_STRUCT_GENESYS_FW_CODESIGN_INFO_ECDSA_SIZE) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_SIGNATURE_INVALID,
					    "mismatch codesign length, got 0x%x, expected = 0x%x",
					    (guint)bufsz,
					    (guint)FU_STRUCT_GENESYS_FW_CODESIGN_INFO_ECDSA_SIZE);
				return FALSE;
			}

			buf += FU_STRUCT_GENESYS_FW_CODESIGN_INFO_ECDSA_SIZE_HASH;
			bufsz -= FU_STRUCT_GENESYS_FW_CODESIGN_INFO_ECDSA_SIZE_HASH;
		}
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	if (fu_firmware_get_id(firmware) != NULL)
		fu_progress_set_name(progress, fu_firmware_get_id(firmware));
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
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_HAS_HW_CODESIGN)) {
		if (!fu_genesys_usbhub_device_compare_flash_data(self,
								 start_addr,
								 buf,
								 bufsz,
								 fu_progress_get_child(progress),
								 error))
			return FALSE;
	}
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_setup_hw_module(FuGenesysUsbhubDevice *self, GError **error)
{
	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_HOST_TO_DEVICE,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    GENESYS_USBHUB_GL_HUB_HW_SECURITY,
						    0,	  /* value */
						    0,	  /* idx */
						    NULL, /* data */
						    0,	  /* data length */
						    NULL, /* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
						    error)) {
		g_prefix_error_literal(error, "error setting up HW module: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_send_hash_data_length(FuGenesysUsbhubDevice *self,
					       gsize size_to_hash,
					       GError **error)
{
	guint16 length_by_4k = size_to_hash / 4096;

	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_HOST_TO_DEVICE,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    GENESYS_USBHUB_GL_HUB_HW_SECURITY,
						    0x040b,	  /* value */
						    length_by_4k, /* idx */
						    NULL,	  /* data */
						    0,		  /* data length */
						    NULL,	  /* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
						    error)) {
		g_prefix_error(error, "error sending hash data length by 4k(0x%x): ", length_by_4k);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_send_hash_digest(FuGenesysUsbhubDevice *self,
					  FuStructGenesysFwCodesignInfoEcdsa *st_codesign,
					  GError **error)
{
	gsize hash_bufsz = 0;
	const guint8 *hash_buf = NULL;
	g_autofree guint8 *buf_mut = NULL;

	hash_buf = fu_struct_genesys_fw_codesign_info_ecdsa_get_hash(st_codesign, &hash_bufsz);
	if (hash_buf == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    "failed to get hash digest");
		return FALSE;
	}

	buf_mut = fu_memdup_safe(hash_buf, hash_bufsz, error);
	if (buf_mut == NULL) {
		g_prefix_error_literal(error, "failed to dup hash digest: ");
		return FALSE;
	}

	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_HOST_TO_DEVICE,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    GENESYS_USBHUB_GL_HUB_HW_SECURITY,
						    0x000b,	/* value */
						    0,		/* idx */
						    buf_mut,	/* data */
						    hash_bufsz, /* data length */
						    NULL,	/* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
						    error)) {
		g_prefix_error_literal(error, "error sending hash digest: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_check_hash_digest_verification(FuGenesysUsbhubDevice *self, GError **error)
{
	guint8 verification = 0;

	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_DEVICE_TO_HOST,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    GENESYS_USBHUB_GL_HUB_HW_SECURITY,
						    0x000b,	   /* value */
						    0,		   /* idx */
						    &verification, /* data */
						    1,		   /* data length */
						    NULL,	   /* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
						    error)) {
		g_prefix_error_literal(error, "error getting hash digest verification: ");
		return FALSE;
	}

	if (verification != 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "failed to verify hash digest, got 0x%x, expected 0x01",
			    verification);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_toggle_hw_read_key(FuGenesysUsbhubDevice *self,
					    guint16 key_addr,
					    GError **error)
{
	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_HOST_TO_DEVICE,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    GENESYS_USBHUB_GL_HUB_HW_SECURITY,
						    0x020c,   /* value */
						    key_addr, /* idx */
						    NULL,     /* data */
						    0,	      /* data length */
						    NULL,     /* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
						    error)) {
		g_prefix_error(error, "error sending key addr 0x%x: ", key_addr);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_send_signature(FuGenesysUsbhubDevice *self,
					FuStructGenesysFwCodesignInfoEcdsa *st_codesign,
					GError **error)
{
	gsize sig_bufsz = 0;
	const guint8 *sig_buf = NULL;
	g_autofree guint8 *buf_mut = NULL;

	sig_buf = fu_struct_genesys_fw_codesign_info_ecdsa_get_signature(st_codesign, &sig_bufsz);
	if (sig_buf == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_AUTH_FAILED,
				    "failed to get signature");
		return FALSE;
	}

	buf_mut = fu_memdup_safe(sig_buf, sig_bufsz, error);
	if (buf_mut == NULL) {
		g_prefix_error_literal(error, "failed to dup signature: ");
		return FALSE;
	}

	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_HOST_TO_DEVICE,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    GENESYS_USBHUB_GL_HUB_HW_SECURITY,
						    0x010c,    /* value */
						    0,	       /* idx */
						    buf_mut,   /* data */
						    sig_bufsz, /* data length */
						    NULL,      /* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
						    error)) {
		g_prefix_error_literal(error, "error sending signature: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_check_signature_verification(FuGenesysUsbhubDevice *self, GError **error)
{
	guint8 verification = 0;

	if (!fu_genesys_usbhub_device_ctrl_transfer(self,
						    NULL, /* progress */
						    FU_USB_DIRECTION_DEVICE_TO_HOST,
						    FU_USB_REQUEST_TYPE_VENDOR,
						    FU_USB_RECIPIENT_DEVICE,
						    GENESYS_USBHUB_GL_HUB_HW_SECURITY,
						    0x000c,	   /* value */
						    0,		   /* idx */
						    &verification, /* data */
						    1,		   /* data length */
						    NULL,	   /* actual length */
						    GENESYS_USBHUB_USB_TIMEOUT,
						    error)) {
		g_prefix_error_literal(error, "error getting signature verification: ");
		return FALSE;
	}

	if (verification != 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "failed to verify signature, got 0x%x, expected 0x01",
			    verification);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_genesys_usbhub_device_compare_firmware_flash_data(FuGenesysUsbhubDevice *self,
						     FuFirmware *firmware,
						     FuProgress *progress,
						     GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = NULL;
	const guint start_addr = fu_firmware_get_addr(firmware);
	g_autoptr(GBytes) blob = NULL;

	/* get raw data */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	buf = g_bytes_get_data(blob, &bufsz);

	if (!fu_genesys_usbhub_device_compare_flash_data(self,
							 start_addr,
							 buf,
							 bufsz,
							 progress,
							 error))
		return FALSE;

	/* success */
	return TRUE;
}

/**
 * GL3590 hub has HW module to examine ECDSA codesign.
 */
static gboolean
fu_genesys_usbhub_device_examine_fw_codesign_hw(FuGenesysUsbhubDevice *self,
						FuFirmware *firmware,
						FuProgress *progress,
						GError **error)
{
	gsize codesize_to_hash = 0;
	g_autoptr(FuFirmware) codesign_img = NULL;
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(FuStructGenesysFwCodesignInfoEcdsa) st_codesign = NULL;
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* get fw codesign info */
	codesign_img = fu_firmware_get_image_by_idx(firmware, FU_GENESYS_FW_TYPE_CODESIGN, error);
	if (codesign_img == NULL)
		return FALSE;
	stream = fu_firmware_get_stream(codesign_img, error);
	if (stream == NULL)
		return FALSE;
	st_codesign = fu_struct_genesys_fw_codesign_info_ecdsa_parse_stream(stream, 0x0, error);
	if (st_codesign == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 5, "setup HW codesign engine");
	fu_progress_add_step(progress,
			     FWUPD_STATUS_DEVICE_VERIFY,
			     100,
			     fu_firmware_get_id(firmware));
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (fu_firmware_get_idx(img) != FU_GENESYS_FW_TYPE_CODESIGN)
			fu_progress_add_step(progress,
					     FWUPD_STATUS_DEVICE_VERIFY,
					     100,
					     fu_firmware_get_id(img));
	}
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 5, "verify hash");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 5, "verify signature");

	/* setup HW codesign module */
	if (!fu_genesys_usbhub_device_setup_hw_module(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* compare firmware data */
	if (!fu_genesys_usbhub_device_compare_firmware_flash_data(self,
								  firmware,
								  fu_progress_get_child(progress),
								  error))
		return FALSE;
	fu_progress_step_done(progress);
	codesize_to_hash += fu_firmware_get_size(firmware);

	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (fu_firmware_get_idx(img) != FU_GENESYS_FW_TYPE_CODESIGN) {
			if (!fu_genesys_usbhub_device_compare_firmware_flash_data(
				self,
				img,
				fu_progress_get_child(progress),
				error))
				return FALSE;
			fu_progress_step_done(progress);
			codesize_to_hash += fu_firmware_get_size(img);
		}
	}

	/* compute and verify hash */
	if (!fu_genesys_usbhub_device_send_hash_data_length(self, codesize_to_hash, error))
		return FALSE;
	if (!fu_genesys_usbhub_device_send_hash_digest(self, st_codesign, error))
		return FALSE;
	if (!fu_genesys_usbhub_device_check_hash_digest_verification(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* toggle hw read key */
	if (!fu_genesys_usbhub_device_toggle_hw_read_key(self,
							 fu_firmware_get_addr(codesign_img),
							 error))
		return FALSE;

	/* send and verify signature */
	if (!fu_genesys_usbhub_device_send_signature(self, st_codesign, error))
		return FALSE;
	if (!fu_genesys_usbhub_device_check_signature_verification(self, error))
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
	gboolean banks_compared = self->running_bank != FU_GENESYS_FW_STATUS_MASK &&
				  fu_device_has_flag(device, FWUPD_DEVICE_FLAG_DUAL_IMAGE) &&
				  self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_HUB] != 0 &&
				  fu_device_get_firmware_size_max(device) >
				      self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_HUB];
	gboolean update_bank2 = self->update_fw_banks[FU_GENESYS_FW_TYPE_HUB] == FW_BANK_2;
	gboolean other_bank_runs = update_bank2
				       ? (self->running_bank == FU_GENESYS_FW_STATUS_BANK1)
				       : (self->running_bank == FU_GENESYS_FW_STATUS_BANK2);

	/* refuse before ISP_ENTER and before anything is erased or written */
	if (!fu_genesys_usbhub_device_ensure_update_gate(self, error))
		return FALSE;

	if (!fu_genesys_usbhub_device_check_update_bank(
		banks_compared,
		self->spec.chip.model == FU_GENESYS_ISP_MODEL_HUB_GL3525,
		update_bank2,
		self->fw_bank_states[FW_BANK_1][FU_GENESYS_FW_TYPE_HUB],
		self->fw_bank_states[FW_BANK_2][FU_GENESYS_FW_TYPE_HUB],
		self->fw_bank_vers[FW_BANK_1][FU_GENESYS_FW_TYPE_HUB],
		self->fw_bank_vers[FW_BANK_2][FU_GENESYS_FW_TYPE_HUB],
		other_bank_runs,
		(guint16)fu_firmware_get_version_raw(firmware),
		error))
		return FALSE;

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
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_HAS_HW_CODESIGN)) {
		fu_progress_add_step(progress,
				     FWUPD_STATUS_DEVICE_VERIFY,
				     100,
				     "verify firmware codesign");
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

	/* examine codesign */
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_GENESYS_USBHUB_FLAG_HAS_HW_CODESIGN)) {
		if (!fu_genesys_usbhub_device_examine_fw_codesign_hw(
			self,
			firmware,
			fu_progress_get_child(progress),
			error)) {
			g_prefix_error_literal(error, "failed to verify HW codesign: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static void
fu_genesys_usbhub_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 83, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 17, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
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
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		fu_genesys_usbhub_device_set_flash_rw_size(self, tmp);

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysUsbhubSwitchRequest") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->vcs.req_switch = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysUsbhubReadRequest") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->vcs.req_read = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysUsbhubWriteRequest") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->vcs.req_write = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysUsbhubHidProxyId") == 0) {
		g_set_str(&self->hid_proxy_id, value);
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysParentHubPid") == 0) {
		if (!fu_strtoull(value, &tmp, 1, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->parent_hub_pid = (guint16)tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysRootHubPid") == 0) {
		if (!fu_strtoull(value, &tmp, 1, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->root_hub_pid = (guint16)tmp;
		return TRUE;
	}

	/* fw bank */
	if (g_strcmp0(key, "GenesysSupportDualBank") == 0) {
		if (!fu_strtobool(value, &self->spec.support_dual_bank, error))
			return FALSE;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysSupportCodeSize") == 0) {
		if (!fu_strtobool(value, &self->spec.support_code_size, error))
			return FALSE;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysHubBank1Address") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_HUB] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysHubBank2Address") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_HUB] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysHubBankCapacity") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_HUB] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysDevBank1Address") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_DEV_BRIDGE] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysDevBank2Address") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_DEV_BRIDGE] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysDevBankCapacity") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_DEV_BRIDGE] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysPdBank1Address") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_PD] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysPdBank2Address") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_PD] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysPdBankCapacity") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_PD] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysCodesignBank1Address") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_addr[FW_BANK_1][FU_GENESYS_FW_TYPE_CODESIGN] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysCodesignBank2Address") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_addr[FW_BANK_2][FU_GENESYS_FW_TYPE_CODESIGN] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysCodesignBankCapacity") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_bank_capacity[FU_GENESYS_FW_TYPE_CODESIGN] = tmp;

		/* success */
		return TRUE;
	}
	if (g_strcmp0(key, "GenesysFwDataMaxCount") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->spec.fw_data_max_count = tmp;

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
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_GUIDS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_protocol(FU_DEVICE(self), "com.genesys.usbhub");
	fu_device_retry_set_delay(FU_DEVICE(self), 30);	   /* 30ms */
	fu_device_set_remove_delay(FU_DEVICE(self), 5000); /* 5s */
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_GENESYS_HUBHID_DEVICE);
	fu_device_set_install_duration(FU_DEVICE(self), 9); /* 9 s */
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_GENESYS_USBHUB_FIRMWARE);
	fu_genesys_usbhub_device_set_flash_rw_size(self, 64);

	self->vcs.req_switch = GENESYS_USBHUB_GL_HUB_SWITCH;
	self->vcs.req_read = GENESYS_USBHUB_GL_HUB_READ;
	self->vcs.req_write = GENESYS_USBHUB_GL_HUB_WRITE;
	self->flash_erase_delay = 8000; /* 8s */
	self->flash_write_delay = 500;	/* 500ms */
	self->flash_block_size = 64 * FU_KB;
	self->flash_sector_size = 4 * FU_KB;
	self->codesign_check = FU_GENESYS_VS_CODESIGN_CHECK_UNSUPPORTED;
	self->codesign = FU_GENESYS_FW_CODESIGN_NONE;
}

static void
fu_genesys_usbhub_device_finalize(GObject *object)
{
	FuGenesysUsbhubDevice *self = FU_GENESYS_USBHUB_DEVICE(object);
	g_free(self->hid_proxy_id);
	if (self->st_static_ts != NULL)
		fu_struct_genesys_ts_static_unref(self->st_static_ts);
	if (self->st_fwinfo_ts != NULL)
		fu_struct_genesys_ts_firmware_info_unref(self->st_fwinfo_ts);
	if (self->st_vendor_ts != NULL)
		fu_struct_genesys_ts_vendor_support_unref(self->st_vendor_ts);
	if (self->st_project_ts != NULL)
		fu_struct_genesys_ts_brand_project_unref(self->st_project_ts);
	if (self->st_rsa_pubkey != NULL)
		fu_struct_genesys_fw_rsa_public_key_text_unref(self->st_rsa_pubkey);
	if (self->st_ecdsa_pubkey != NULL)
		fu_struct_genesys_fw_ecdsa_public_key_unref(self->st_ecdsa_pubkey);
	if (self->st_codesign_ecdsa != NULL)
		fu_struct_genesys_fw_codesign_info_ecdsa_unref(self->st_codesign_ecdsa);
	if (self->hub_fw_bank1_data != NULL)
		g_bytes_unref(self->hub_fw_bank1_data);
	if (self->st_codesign != NULL)
		g_byte_array_unref(self->st_codesign);
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
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_genesys_usbhub_device_finalize;
	device_class->probe = fu_genesys_usbhub_device_probe;
	device_class->setup = fu_genesys_usbhub_device_setup;
	device_class->dump_firmware = fu_genesys_usbhub_device_dump_firmware;
	device_class->prepare = fu_genesys_usbhub_device_prepare;
	device_class->check_firmware = fu_genesys_usbhub_device_check_firmware;
	device_class->write_firmware = fu_genesys_usbhub_device_write_firmware;
	device_class->set_progress = fu_genesys_usbhub_device_set_progress;
	device_class->detach = fu_genesys_usbhub_device_detach;
	device_class->attach = fu_genesys_usbhub_device_attach;
	device_class->to_string = fu_genesys_usbhub_device_to_string;
	device_class->set_quirk_kv = fu_genesys_usbhub_device_set_quirk_kv;
	fu_device_register_private_flag(device_class, FU_GENESYS_USBHUB_FLAG_HAS_MSTAR_SCALER);
	fu_device_register_private_flag(device_class, FU_GENESYS_USBHUB_FLAG_HAS_PUBLIC_KEY);
	fu_device_register_private_flag(device_class, FU_GENESYS_USBHUB_FLAG_IS_GL352350);
	fu_device_register_private_flag(device_class, FU_GENESYS_USBHUB_FLAG_HAS_CODESIGN);
	fu_device_register_private_flag(device_class, FU_GENESYS_USBHUB_FLAG_HAS_HW_CODESIGN);
	fu_device_register_private_flag(device_class, FU_GENESYS_USBHUB_FLAG_PROXY_REQUIRED);
	fu_device_register_private_flag(device_class, FU_GENESYS_USBHUB_FLAG_STRICT_PROJECT_CHECK);
}
