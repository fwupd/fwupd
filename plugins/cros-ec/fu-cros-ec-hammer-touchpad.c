/*
 * Copyright 2025 Hamed Elgizery
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-cros-ec-common.h"
#include "fu-cros-ec-hammer-touchpad-firmware.h"
#include "fu-cros-ec-hammer-touchpad.h"
#include "fu-cros-ec-struct.h"
#include "fu-cros-ec-usb-device.h"

#define SHA256_DIGEST_LENGTH 32

#define ST_VENDOR_ID   0x0483
#define ELAN_VENDOR_ID 0x04f3

typedef struct {
	guint16 vendor;
	guint32 fw_address;
	guint32 fw_size;
	guint8 *allowed_fw_hash;
	guint16 id;
	guint16 fw_version;
	guint16 fw_checksum;
	gboolean is_remunerated; // Data can be populated only when ec is in runtime mode.
} FuCrosEcHammerTouchpadPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCrosEcHammerTouchpad, fu_cros_ec_hammer_touchpad, FU_TYPE_DEVICE)
#define GET_PRIVATE(o) (fu_cros_ec_hammer_touchpad_get_instance_private(o))

guint32
fu_cros_ec_hammer_touchpad_get_fw_address(FuCrosEcHammerTouchpad *self)
{
	FuCrosEcHammerTouchpadPrivate *priv = GET_PRIVATE(self);
	return priv->fw_address;
}

guint32
fu_cros_ec_hammer_touchpad_get_fw_size(FuCrosEcHammerTouchpad *self)
{
	FuCrosEcHammerTouchpadPrivate *priv = GET_PRIVATE(self);
	return priv->fw_size;
}

static gboolean
fu_cros_ec_hammer_touchpad_set_metadata(FuCrosEcHammerTouchpad *self, GError **error)
{
	FuCrosEcHammerTouchpadPrivate *priv = GET_PRIVATE(self);
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));

	gchar *base_fw_ver;
	g_autofree gchar *instance_id = NULL;
	g_autofree gchar *vendor_name = NULL;
	g_autofree gchar *device_name = NULL;
	switch (priv->vendor) {
	case ST_VENDOR_ID:
		base_fw_ver = g_strdup_printf("%d.%d",
					      priv->fw_version & 0x00ff,
					      (priv->fw_version & 0xff00) >> 8);
		vendor_name = g_strdup("STMicroelectronics");
		break;
	case ELAN_VENDOR_ID:
		base_fw_ver = g_strdup_printf("%d.0", priv->fw_version);
		vendor_name = g_strdup("ELAN");
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid touchpad vendor id received");
		return FALSE;
	}
	device_name = g_strdup_printf("%s Touchpad", vendor_name);
	fu_device_set_name(FU_DEVICE(self), device_name);
	fu_device_set_version(FU_DEVICE(self), base_fw_ver);
	instance_id = g_strdup_printf("USB\\VID_%04X&PID_%04X&VENDORNAME_%s",
				      fu_device_get_vid(FU_DEVICE(proxy)),
				      fu_device_get_pid(FU_DEVICE(proxy)),
				      vendor_name);
	fu_device_add_instance_id(FU_DEVICE(self), instance_id);

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_hammer_touchpad_get_info(FuCrosEcHammerTouchpad *self, GError **error)
{
	FuCrosEcHammerTouchpadPrivate *priv = GET_PRIVATE(self);
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	gsize bufsz;
	guint32 error_code;
	guint16 subcommand = FU_CROS_EC_UPDATE_EXTRA_CMD_TOUCHPAD_INFO;
	guint8 command_body[2] = {0x0}; /* max command body size */
	gsize command_body_size = 0;
	g_autoptr(FuStructCrosEcTouchpadGetInfoResponsePdu) tpi_rpdu =
	    fu_struct_cros_ec_touchpad_get_info_response_pdu_new();
	guint8 *response = tpi_rpdu->data;
	gsize response_size = tpi_rpdu->len;
	g_autoptr(GError) error_local = NULL;
	guint8 *buffer = NULL;

	g_return_val_if_fail(FU_IS_CROS_EC_USB_DEVICE(proxy), FALSE);

	if (!fu_cros_ec_usb_device_send_subcommand(FU_CROS_EC_USB_DEVICE(proxy),
						   subcommand,
						   command_body,
						   command_body_size,
						   response,
						   &response_size,
						   FALSE,
						   &error_local)) {
		g_prefix_error(error, "failed to probe touchpad");
		return FALSE;
	}

	error_code = fu_struct_cros_ec_touchpad_get_info_response_pdu_get_status(tpi_rpdu);
	if (error_code != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "target touchpad reporting error %u",
			    error_code);
		return FALSE;
	}

	priv->vendor = fu_struct_cros_ec_touchpad_get_info_response_pdu_get_vendor(tpi_rpdu);
	priv->fw_address =
	    fu_struct_cros_ec_touchpad_get_info_response_pdu_get_fw_address(tpi_rpdu);
	priv->fw_size = fu_struct_cros_ec_touchpad_get_info_response_pdu_get_fw_size(tpi_rpdu);
	buffer =
	    (guint8 *)fu_struct_cros_ec_touchpad_get_info_response_pdu_get_allowed_fw_hash(tpi_rpdu,
											   &bufsz);
	priv->allowed_fw_hash = g_memdup2(buffer, bufsz);
	priv->id = fu_struct_cros_ec_touchpad_get_info_response_pdu_get_id(tpi_rpdu);
	priv->fw_version =
	    fu_struct_cros_ec_touchpad_get_info_response_pdu_get_fw_version(tpi_rpdu);
	priv->fw_checksum =
	    fu_struct_cros_ec_touchpad_get_info_response_pdu_get_fw_checksum(tpi_rpdu);

	priv->is_remunerated = TRUE;

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_hammer_touchpad_probe(FuDevice *device, GError **error)
{
	FuCrosEcHammerTouchpad *self = FU_CROS_EC_HAMMER_TOUCHPAD(device);
	FuCrosEcHammerTouchpadPrivate *priv = GET_PRIVATE(self);
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self));
	g_autofree gchar *instance_id = NULL;

	g_return_val_if_fail(FU_IS_CROS_EC_USB_DEVICE(proxy), FALSE);

	priv->is_remunerated = FALSE;

	instance_id = g_strdup_printf("USB\\VID_%04X&PID_%04X&TP",
				      fu_device_get_vid(FU_DEVICE(proxy)),
				      fu_device_get_pid(FU_DEVICE(proxy)));
	fu_device_add_instance_id(FU_DEVICE(self), instance_id);

	if (fu_cros_ec_usb_device_get_in_bootloader(proxy)) {
		g_debug("skipping enumeration: ec is in bootloader mode");
		return TRUE;
	}

	if (!fu_cros_ec_hammer_touchpad_get_info(self, error))
		return FALSE;

	fu_cros_ec_hammer_touchpad_set_metadata(self, error);

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_hammer_touchpad_firmware_validate(FuDevice *device, FuFirmware *firmware, GError **error)
{
	FuCrosEcHammerTouchpad *self = FU_CROS_EC_HAMMER_TOUCHPAD(device);
	FuCrosEcHammerTouchpadPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GError) error_local = NULL;
	GBytes *payload = NULL;
	gsize fwsize;
	const guchar *fw = NULL;
	GChecksum *checksum = NULL;
	guint8 digest[SHA256_DIGEST_LENGTH];
	gsize digest_len = sizeof(digest);

	payload = fu_firmware_get_bytes(firmware, error);
	fw = g_bytes_get_data(payload, &fwsize);

	if ((gsize)priv->fw_size != fwsize) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "Local touchpad binary doesn't match remote IC size."
			    "Local = %" G_GSIZE_FORMAT " bytes."
			    "Remote = %" G_GSIZE_FORMAT " bytes.",
			    fwsize,
			    (gsize)priv->fw_size);
		return FALSE;
	}

	checksum = g_checksum_new(G_CHECKSUM_SHA256);
	g_checksum_update(checksum, fw, fwsize);
	g_checksum_get_digest(checksum, digest, &digest_len);
	fu_dump_full(G_LOG_DOMAIN,
		     "Computed local touchpad firmware hash",
		     digest,
		     digest_len,
		     80,
		     FU_DUMP_FLAGS_NONE);

	if (memcmp(digest, priv->allowed_fw_hash, SHA256_DIGEST_LENGTH) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "Touchpad firmware mismatches hash in RW EC.");
		return FALSE;
	}

	// TODO: Check product if product id matches
	/*
	 * In hammerd, this was done by comparing the product_id
	 * from the touchpad firmware file name, and compare it with
	 * the product id saved within the EC.
	 * There is no direct way to do the comparison here, and is probably
	 * not needed since we already do match the firmware file with the device
	 * through GUIDs... Also there is no files to compare with instead the updates
	 * is usually done through cab files. Suggesting to skip this.
	 */
	return TRUE;
}

static FuFirmware *
fu_cros_ec_hammer_touchpad_prepare_firmware(FuDevice *device,
					    GInputStream *stream,
					    FuProgress *progress,
					    FuFirmwareParseFlags flags,
					    GError **error)
{
	FuCrosEcHammerTouchpad *self = FU_CROS_EC_HAMMER_TOUCHPAD(device);
	FuCrosEcHammerTouchpadPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuFirmware) firmware = fu_cros_ec_hammer_touchpad_firmware_new();

	/*
	 * Touchpad is updated after both the EC's RO & RW regions are updated.
	 *
	 * Each EC firmware expects a specific touchpad firmware.
	 *
	 * So, before we start flashing the touchpad firmware, we need to make
	 * sure it matches the EC's expected touchpad firmware. We do that
	 * by querying the EC board for touchpad info again (the first time was during the setup),
	 * which includes allowed touchpad firmware hash.
	 */

	/* device should've already been probed */
	if (!priv->is_remunerated) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "touchpad info missing");
		return NULL;
	}

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	if (!fu_cros_ec_hammer_touchpad_firmware_validate(device, firmware, error))
		return NULL;

	return g_steal_pointer(&firmware);
}

static gboolean
fu_cros_ec_hammer_touchpad_write_firmware(FuDevice *device,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuCrosEcHammerTouchpad *self = FU_CROS_EC_HAMMER_TOUCHPAD(device);
	FuCrosEcHammerTouchpadPrivate *priv = GET_PRIVATE(device);
	FuDevice *parent = fu_device_get_parent(FU_DEVICE(self));

	/*
	 * Update is done through the parent device (the EC base),
	 * so we call this and let EC handle the updating.
	 */

	/* device should've already been probed */
	if (!priv->is_remunerated) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "touchpad info missing");
		return FALSE;
	}

	if (!fu_cros_ec_usb_device_write_touchpad_firmware(parent,
							   firmware,
							   progress,
							   flags,
							   device,
							   error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_cros_ec_hammer_touchpad_finalize(GObject *object)
{
	FuCrosEcHammerTouchpad *self = FU_CROS_EC_HAMMER_TOUCHPAD(object);
	FuCrosEcHammerTouchpadPrivate *priv = GET_PRIVATE(self);
	g_free(priv->allowed_fw_hash);
	G_OBJECT_CLASS(fu_cros_ec_hammer_touchpad_parent_class)->finalize(object);
}

static void
fu_cros_ec_hammer_touchpad_init(FuCrosEcHammerTouchpad *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.google.usb.crosec");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_DETACH_PREPARE_FIRMWARE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FALLBACK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_INSTALL_PARENT_FIRST);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
}

static void
fu_cros_ec_hammer_touchpad_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCrosEcHammerTouchpad *self = FU_CROS_EC_HAMMER_TOUCHPAD(device);
	FuCrosEcHammerTouchpadPrivate *priv = GET_PRIVATE(self);
	// TODO: Retake a look on what is important
	fwupd_codec_string_append_int(str, idt, "Vendor", priv->vendor);
	fwupd_codec_string_append_hex(str, idt, "FwAddress", priv->fw_address);
	fwupd_codec_string_append_int(str, idt, "RawVersion", priv->fw_version);
}

static void
fu_cros_ec_hammer_touchpad_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_cros_ec_hammer_touchpad_class_init(FuCrosEcHammerTouchpadClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_cros_ec_hammer_touchpad_finalize;
	device_class->probe = fu_cros_ec_hammer_touchpad_probe;
	device_class->to_string = fu_cros_ec_hammer_touchpad_to_string;
	device_class->prepare_firmware = fu_cros_ec_hammer_touchpad_prepare_firmware;
	device_class->write_firmware = fu_cros_ec_hammer_touchpad_write_firmware;
	device_class->set_progress = fu_cros_ec_hammer_touchpad_set_progress;
}

FuCrosEcHammerTouchpad *
fu_cros_ec_hammer_touchpad_new(FuDevice *proxy)
{
	FuCrosEcHammerTouchpad *self = NULL;
	FuContext *ctx = fu_device_get_context(proxy);

	self = g_object_new(FU_TYPE_CROS_EC_HAMMER_TOUCHPAD, "context", ctx, "proxy", proxy, NULL);
	fu_device_incorporate(FU_DEVICE(self), proxy, FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
	fu_device_set_logical_id(FU_DEVICE(self), "cros-ec-hammer-touchpad");
	return self;
}
