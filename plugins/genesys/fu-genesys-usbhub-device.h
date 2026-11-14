/*
 * Copyright 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GENESYS_USBHUB_DEVICE		    (fu_genesys_usbhub_device_get_type())
#define FU_GENESYS_USBHUB_FLAG_STRICT_PROJECT_CHECK "strict-project-check"
#define FU_GENESYS_USBHUB_FLAG_HID_TRANSPORT	    "hid-transport"
#define FU_GENESYS_USBHUB_FLAG_REQUIRE_HID_PROXY    "require-hid-proxy"
#define FU_GENESYS_USBHUB_FLAG_SINGLE_HID_PROXY	    "single-hid-proxy"
#define FU_GENESYS_USBHUB_INHIBIT_HID_PROXY	    "hid-proxy"
#define FU_GENESYS_USBHUB_INHIBIT_HID_AMBIGUOUS	    "hid-proxy-ambiguous"
#define FU_GENESYS_USBHUB_INHIBIT_FLASH_SETUP	    "flash-setup"

/* what is known about the firmware stored in a bank */
typedef enum {
	FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN, /* not read, or the read failed */
	FU_GENESYS_USBHUB_BANK_STATE_VALID,   /* read, and the firmware is valid */
	FU_GENESYS_USBHUB_BANK_STATE_INVALID, /* read, and there is no valid firmware */
} FuGenesysUsbhubBankState;

G_DECLARE_FINAL_TYPE(FuGenesysUsbhubDevice,
		     fu_genesys_usbhub_device,
		     FU,
		     GENESYS_USBHUB_DEVICE,
		     FuUsbDevice)

typedef gboolean (*FuGenesysUsbhubCtrlHook)(FuUsbDirection direction,
					    guint8 request,
					    guint16 value,
					    guint8 *data,
					    gsize length,
					    gpointer user_data,
					    GError **error);

gboolean
fu_genesys_usbhub_device_set_proxy(FuGenesysUsbhubDevice *self,
				   FuDevice *proxy,
				   const gchar *hid_parent_physical_id,
				   GError **error);

void
fu_genesys_usbhub_device_add_model_quirk_id(FuGenesysUsbhubDevice *self);

void
fu_genesys_usbhub_device_refresh_hid_proxy_inhibit(FuGenesysUsbhubDevice *self);

gboolean
fu_genesys_usbhub_device_verify_hid_proxy(FuGenesysUsbhubDevice *self,
					  FuDevice *proxy,
					  const gchar *hid_parent_physical_id,
					  GError **error);

gboolean
fu_genesys_usbhub_device_check_parent_hub(guint16 expected_parent_pid,
					  guint16 hub_vid,
					  FuDevice *parent,
					  GError **error);

gboolean
fu_genesys_usbhub_device_check_hid_binding(const gchar *hub_physical_id,
					   guint16 hub_vid,
					   guint16 expected_hid_pid,
					   const gchar *hid_parent_physical_id,
					   guint16 hid_vid,
					   guint16 hid_pid,
					   GError **error);

void
fu_genesys_usbhub_device_begin_flash_setup(FuGenesysUsbhubDevice *self);

void
fu_genesys_usbhub_device_finish_flash_setup(FuGenesysUsbhubDevice *self);

gboolean
fu_genesys_usbhub_device_setup_flash_late(FuGenesysUsbhubDevice *self, GError **error);

gboolean
fu_genesys_usbhub_device_setup_flash_late_opened(FuGenesysUsbhubDevice *self, GError **error);

void
fu_genesys_usbhub_device_set_ctrl_hook(FuGenesysUsbhubDevice *self,
				       FuGenesysUsbhubCtrlHook hook,
				       gpointer user_data);

FuGenesysUsbhubBankState
fu_genesys_usbhub_device_bank_state_from_error(const GError *error);

FuGenesysUsbhubBankState
fu_genesys_usbhub_device_bank_state_from_image(const guint8 *buf, gsize bufsz);

gboolean
fu_genesys_usbhub_device_select_update_bank2(gboolean is_gl3523,
					     guint16 bank1_version,
					     guint16 bank2_version,
					     gboolean *backup_bank1);

gboolean
fu_genesys_usbhub_device_check_project_compatibility(gboolean strict,
						     GBytes *device_project,
						     const gchar *device_mask_project_ic_type,
						     GBytes *firmware_project,
						     const gchar *firmware_mask_project_ic_type,
						     GError **error);

gboolean
fu_genesys_usbhub_device_check_bank_runs_target(guint16 target_version,
						guint16 other_bank_version,
						GError **error);

void
fu_genesys_usbhub_device_set_gl3525_bank_state(FuGenesysUsbhubDevice *self,
					       guint16 bank1_version,
					       guint16 bank2_version,
					       gboolean update_bank2,
					       FuGenesysUsbhubBankState bank1_state,
					       FuGenesysUsbhubBankState bank2_state);

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
					   GError **error);

GPtrArray *
fu_genesys_usbhub_device_chunks_new(const guint8 *data,
				    gsize data_sz,
				    gsize addr_offset,
				    gsize page_sz,
				    gsize packet_sz,
				    gboolean mutable,
				    GError **error);
