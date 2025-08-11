/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-cros-ec-firmware.h"

#define FU_CROS_EC_SETUP_RETRY_CNT 5

#define FU_TYPE_CROS_EC_USB_DEVICE (fu_cros_ec_usb_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuCrosEcUsbDevice,
			 fu_cros_ec_usb_device,
			 FU,
			 CROS_EC_USB_DEVICE,
			 FuUsbDevice)

struct _FuCrosEcUsbDeviceClass {
	FuUsbDeviceClass parent_class;
};

#define FU_CROS_EC_USB_SUBCLASS_GOOGLE_UPDATE 0x53
#define FU_CROS_EC_USB_PROTOCOL_GOOGLE_UPDATE 0xff

#define FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN	   "ro-written"
#define FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN	   "rw-written"
#define FU_CROS_EC_USB_DEVICE_FLAG_UPDATING_TP	   "updating-touchpad"
#define FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO "rebooting-to-ro"
#define FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL	   "special"
#define FU_CROS_EC_DEVICE_FLAG_HAS_TOUCHPAD	   "has-touchpad"

guint8
fu_cros_ec_usb_device_get_iface_idx(FuCrosEcUsbDevice *self);

guint8
fu_cros_ec_usb_device_get_ep_num(FuCrosEcUsbDevice *self);

guint16
fu_cros_ec_usb_device_get_chunk_len(FuCrosEcUsbDevice *self);

gchar *
fu_cros_ec_usb_device_get_raw_version(FuCrosEcUsbDevice *self);

guint32
fu_cros_ec_usb_device_get_maximum_pdu_size(FuCrosEcUsbDevice *self);

guint32
fu_cros_ec_usb_device_get_flash_protection(FuCrosEcUsbDevice *self);

guint32
fu_cros_ec_usb_device_get_writeable_offset(FuCrosEcUsbDevice *self);

guint16
fu_cros_ec_usb_device_get_protocol_version(FuCrosEcUsbDevice *self);

gchar *
fu_cros_ec_usb_device_get_configuration(FuCrosEcUsbDevice *self);

gboolean
fu_cros_ec_usb_device_get_in_bootloader(FuCrosEcUsbDevice *self);

void
fu_cros_ec_usb_device_set_iface_idx(FuCrosEcUsbDevice *self, guint8 iface_idx);

void
fu_cros_ec_usb_device_set_ep_num(FuCrosEcUsbDevice *self, guint8 ep_num);

void
fu_cros_ec_usb_device_set_chunk_len(FuCrosEcUsbDevice *self, guint16 chunk_len);

void
fu_cros_ec_usb_device_set_raw_version(FuCrosEcUsbDevice *self, const gchar *raw_version);

void
fu_cros_ec_usb_device_set_maximum_pdu_size(FuCrosEcUsbDevice *self, guint32 maximum_pdu_size);

void
fu_cros_ec_usb_device_set_flash_protection(FuCrosEcUsbDevice *self, guint32 flash_protection);

void
fu_cros_ec_usb_device_set_writeable_offset(FuCrosEcUsbDevice *self, guint32 writeable_offset);

void
fu_cros_ec_usb_device_set_protocol_version(FuCrosEcUsbDevice *self, guint16 protocol_version);

void
fu_cros_ec_usb_device_set_configuration(FuCrosEcUsbDevice *self, const gchar *configuration);

void
fu_cros_ec_usb_device_set_in_bootloader(FuCrosEcUsbDevice *self, gboolean in_bootloader);

gboolean
fu_cros_ec_usb_device_send_subcommand(FuCrosEcUsbDevice *self,
				      guint16 subcommand,
				      guint8 *cmd_body,
				      gsize body_size,
				      guint8 *resp,
				      gsize *resp_size,
				      gboolean allow_less,
				      GError **error);
gboolean
fu_cros_ec_usb_device_transfer_block_cb(FuDevice *device, gpointer user_data, GError **error);

gboolean
fu_cros_ec_usb_device_start_request_cb(FuDevice *device, gpointer user_data, GError **error);

gboolean
fu_cros_ec_usb_device_probe(FuDevice *device, GError **error);

gboolean
fu_cros_ec_usb_device_recovery(FuCrosEcUsbDevice *self, GError **error);

gboolean
fu_cros_ec_usb_device_flush(FuDevice *device, gpointer user_data, GError **error);

gboolean
fu_cros_ec_usb_device_attach(FuDevice *device, FuProgress *progress, GError **error);

gboolean
fu_cros_ec_usb_device_detach(FuDevice *device, FuProgress *progress, GError **error);

void
fu_cros_ec_usb_device_replace(FuDevice *device, FuDevice *donor);

gboolean
fu_cros_ec_usb_device_cleanup(FuDevice *device,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error);

gboolean
fu_cros_ec_usb_device_reload(FuDevice *device, GError **error);

void
fu_cros_ec_usb_device_to_string(FuDevice *device, guint idt, GString *str);

void
fu_cros_ec_usb_device_set_progress(FuDevice *self, FuProgress *progress);

FuFirmware *
fu_cros_ec_usb_device_prepare_firmware(FuDevice *device,
				       GInputStream *stream,
				       FuProgress *progress,
				       FuFirmwareParseFlags flags,
				       GError **error);

gboolean
fu_cros_ec_usb_device_setup(FuDevice *device, GError **error);

void
fu_cros_ec_usb_device_finalize(GObject *object);

void
fu_cros_ec_usb_device_send_done(FuCrosEcUsbDevice *self);

gboolean
fu_cros_ec_usb_device_unlock_rw(FuCrosEcUsbDevice *self, GError **error);

void
fu_cros_ec_usb_device_reset_to_ro(FuCrosEcUsbDevice *self);

gboolean
fu_cros_ec_usb_device_transfer_section(FuCrosEcUsbDevice *self,
				       FuFirmware *firmware,
				       FuCrosEcFirmwareSection *section,
				       FuProgress *progress,
				       GError **error);
gboolean
fu_cros_ec_usb_device_stay_in_ro(FuCrosEcUsbDevice *self, GError **error);
