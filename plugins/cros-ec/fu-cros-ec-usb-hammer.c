/*
 * Copyright 2025 Hamed Elgizery
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-cros-ec-common.h"
#include "fu-cros-ec-firmware.h"
#include "fu-cros-ec-hammer-touchpad-firmware.h"
#include "fu-cros-ec-struct.h"
#include "fu-cros-ec-usb-device.h"
#include "fu-cros-ec-usb-hammer.h"

struct _FuCrosEcUsbHammer {
	FuCrosEcUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuCrosEcUsbHammer, fu_cros_ec_usb_hammer, FU_TYPE_CROS_EC_USB_DEVICE)

typedef struct {
	FuChunk *block;
	FuProgress *progress;
} FuCrosEcUsbBlockHelper;

gboolean
fu_cros_ec_usb_hammer_write_touchpad_firmware(FuDevice *device,
					      FuFirmware *firmware,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      FuDevice *tp_device,
					      GError **error)
{
	FuCrosEcUsbHammer *self = FU_CROS_EC_USB_HAMMER(device);
	FuCrosEcHammerTouchpad *touchpad = FU_CROS_EC_HAMMER_TOUCHPAD(tp_device);
	gsize data_len = 0;
	guint32 maximum_pdu_size = 0;
	guint32 tp_fw_size = 0;
	guint32 tp_fw_address = 0;
	const guint8 *data_ptr = NULL;
	g_autoptr(GBytes) img_bytes = NULL;
	g_autoptr(GPtrArray) blocks = NULL;
	g_autoptr(FuStructCrosEcFirstResponsePdu) st_rpdu =
	    fu_struct_cros_ec_first_response_pdu_new();

	/* send start request */
	if (!fu_device_retry(device,
			     fu_cros_ec_usb_device_start_request_cb,
			     FU_CROS_EC_SETUP_RETRY_CNT,
			     st_rpdu,
			     error)) {
		g_prefix_error(error, "touchpad: failed to send start request: ");
		return FALSE;
	}

	fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_UPDATING_TP);

	/*
	 * Probably, can be replaced with the CrosEcUsbHammer's maximum_pdu_size,
	 * but opting for independence here.
	 */
	maximum_pdu_size = fu_struct_cros_ec_first_response_pdu_get_maximum_pdu_size(st_rpdu);
	img_bytes = fu_firmware_get_bytes(firmware, error);
	data_ptr = (const guint8 *)g_bytes_get_data(img_bytes, &data_len);
	tp_fw_address = fu_cros_ec_hammer_touchpad_get_fw_address(touchpad);
	tp_fw_size = fu_cros_ec_hammer_touchpad_get_fw_size(touchpad);

	if (tp_fw_address != (1u << 31)) { // This is for testing safety only, should be removed
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "CRITICAL DEVICE ABOUT TO BE BRICKED HALTING");
		return FALSE;
	}

	if (data_ptr == NULL || data_len != tp_fw_size) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "touchpad: image and section sizes do not match: image = %" G_GSIZE_FORMAT
		    " bytes vs touchpad section size = %" G_GSIZE_FORMAT " bytes",
		    data_len,
		    (gsize)tp_fw_size);
		return FALSE;
	}

	g_debug("touchpad: sending 0x%x bytes to 0x%x", (guint)data_len, tp_fw_address);

	blocks =
	    /* send in chunks of PDU size */
	    fu_chunk_array_new(data_ptr, data_len, tp_fw_address, 0x0, maximum_pdu_size);
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, blocks->len);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < blocks->len; i++) {
		FuCrosEcUsbBlockHelper helper = {
		    .block = g_ptr_array_index(blocks, i),
		    .progress = fu_progress_get_child(progress),
		};
		if (!fu_device_retry(FU_DEVICE(self),
				     fu_cros_ec_usb_device_transfer_block_cb,
				     FU_CROS_EC_MAX_BLOCK_XFER_RETRIES,
				     &helper,
				     error)) {
			g_prefix_error(error, "touchpad: failed to transfer block 0x%x: ", i);
			return FALSE;
		}
		fu_progress_step_done(progress);
	}

	fu_device_remove_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_UPDATING_TP);
	return TRUE;
}

static gboolean
fu_cros_ec_usb_hammer_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuCrosEcUsbHammer *self = FU_CROS_EC_USB_HAMMER(device);
	g_autoptr(GPtrArray) sections = NULL;
	FuCrosEcFirmware *cros_ec_firmware = FU_CROS_EC_FIRMWARE(firmware);

	fu_device_remove_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL);

	if (fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO)) {
		g_autoptr(FuStructCrosEcFirstResponsePdu) st_rpdu =
		    fu_struct_cros_ec_first_response_pdu_new();

		fu_device_remove_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_REBOOTING_TO_RO);
		if (!fu_cros_ec_usb_device_stay_in_ro(FU_CROS_EC_USB_DEVICE(self), error)) {
			g_prefix_error(error, "failed to send stay-in-ro subcommand: ");
			return FALSE;
		}

		/* flush all data from endpoint to recover in case of error */
		if (!fu_cros_ec_usb_device_recovery(FU_CROS_EC_USB_DEVICE(self), error)) {
			g_prefix_error(error, "failed to flush device to idle state: ");
			return FALSE;
		}

		/* send start request */
		if (!fu_device_retry(device,
				     fu_cros_ec_usb_device_start_request_cb,
				     FU_CROS_EC_SETUP_RETRY_CNT,
				     st_rpdu,
				     error)) {
			g_prefix_error(error, "failed to send start request: ");
			return FALSE;
		}
	}

	if (fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN) &&
	    fu_cros_ec_usb_device_get_in_bootloader(FU_CROS_EC_USB_DEVICE(self))) {
		/*
		 * We had previously written to the rw region (while we were
		 * booted from ro region), but somehow landed in ro again after
		 * a reboot. Since we wrote rw already, we wanted to jump
		 * to the new rw so we could evaluate ro.
		 *
		 * This is a transitory state due to the fact that we have to
		 * boot through RO to get to RW. Set another write required to
		 * allow the RO region to auto-jump to RW.
		 *
		 * Special flow: write phase skips actual write -> attach skips
		 * send of reset command, just sets wait for replug, and
		 * device restart status.
		 */
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_SPECIAL);
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
		return TRUE;
	}

	/*
	 * If it is turn to update RW section, two conditions need
	 * to be satisfied:
	 *   1. ec has to be in bootloader mode
	 *   2. RW Flash protection has to be disabled (enabled by default).
	 *
	 * We set another ANOTHER_WRITE_REQUIRED if any of the conditions is not met.
	 *
	 * Proceed with unlocking the the RW flash protection and rebooting
	 * the device, attempting to land in bootloader mode with RW flash
	 * protection disabled with the next write attempt.
	 */
	if (!fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN) &&
	    (!fu_cros_ec_usb_device_get_in_bootloader(FU_CROS_EC_USB_DEVICE(self)) ||
	     (fu_cros_ec_usb_device_get_flash_protection(FU_CROS_EC_USB_DEVICE(self)) & (1 << 8)) !=
		 0)) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
		if (!fu_cros_ec_usb_device_unlock_rw(FU_CROS_EC_USB_DEVICE(self), error))
			return FALSE;
		return TRUE;
	}

	sections = fu_cros_ec_firmware_get_needed_sections(cros_ec_firmware, error);
	if (sections == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, sections->len);
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < sections->len; i++) {
		FuCrosEcFirmwareSection *section = g_ptr_array_index(sections, i);
		g_autoptr(GError) error_local = NULL;

		if (!fu_cros_ec_usb_device_transfer_section(FU_CROS_EC_USB_DEVICE(self),
							    firmware,
							    section,
							    fu_progress_get_child(progress),
							    &error_local)) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ)) {
				g_debug("failed to transfer section, trying another write, "
					"ignoring error: %s",
					error_local->message);
				fu_device_add_flag(device,
						   FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
				fu_progress_finished(progress);
				return TRUE;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}

		// TODO: Fix me, section->version.triplet has no data...
		if (fu_cros_ec_usb_device_get_in_bootloader(FU_CROS_EC_USB_DEVICE(self))) {
			fu_device_set_version(device, section->version.triplet);
		} else {
			fu_device_set_version_bootloader(device, section->version.triplet);
		}

		fu_progress_step_done(progress);
	}

	/* send done */
	fu_cros_ec_usb_device_send_done(FU_CROS_EC_USB_DEVICE(self));

	if (fu_cros_ec_usb_device_get_in_bootloader(FU_CROS_EC_USB_DEVICE(self)))
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN);
	else
		fu_device_add_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN);

	/* logical XOR */
	if (fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RW_WRITTEN) !=
	    fu_device_has_private_flag(device, FU_CROS_EC_USB_DEVICE_FLAG_RO_WRITTEN))
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_hammer_ensure_children(FuCrosEcUsbHammer *self, GError **error)
{
	FuDevice *device = FU_DEVICE(self);
	g_autoptr(FuCrosEcHammerTouchpad) touchpad = NULL;

	if (!fu_device_has_private_flag(device, FU_CROS_EC_DEVICE_FLAG_HAS_TOUCHPAD))
		return TRUE;

	touchpad = fu_cros_ec_hammer_touchpad_new(FU_DEVICE(device));
	fu_device_add_child(FU_DEVICE(device), FU_DEVICE(touchpad));

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_hammer_probe(FuDevice *device, GError **error)
{
	if (!fu_cros_ec_usb_device_probe(device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_hammer_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (!fu_cros_ec_usb_device_attach(device, progress, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_hammer_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (!fu_cros_ec_usb_device_detach(device, progress, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_cros_ec_usb_hammer_replace(FuDevice *device, FuDevice *donor)
{
	fu_cros_ec_usb_device_replace(device, donor);
}

static gboolean
fu_cros_ec_usb_hammer_cleanup(FuDevice *device,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	if (!fu_cros_ec_usb_device_cleanup(device, progress, flags, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_hammer_reload(FuDevice *device, GError **error)
{
	if (!fu_cros_ec_usb_device_reload(device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_cros_ec_usb_hammer_to_string(FuDevice *device, guint idt, GString *str)
{
	fu_cros_ec_usb_device_to_string(device, idt, str);
}

static FuFirmware *
fu_cros_ec_usb_hammer_prepare_firmware(FuDevice *device,
				       GInputStream *stream,
				       FuProgress *progress,
				       FuFirmwareParseFlags flags,
				       GError **error)
{
	return fu_cros_ec_usb_device_prepare_firmware(device, stream, progress, flags, error);
}

static gboolean
fu_cros_ec_usb_hammer_setup(FuDevice *device, GError **error)
{
	g_warning("HELL YEAH!");
	if (!fu_cros_ec_usb_device_setup(device, error))
		return FALSE;

	if (!fu_cros_ec_usb_hammer_ensure_children(FU_CROS_EC_USB_HAMMER(device), error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_cros_ec_usb_hammer_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_cros_ec_usb_device_set_progress(self, progress);
}

static void
fu_cros_ec_usb_hammer_init(FuCrosEcUsbHammer *self)
{
}

static void
fu_cros_ec_usb_hammer_class_init(FuCrosEcUsbHammerClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	device_class->attach = fu_cros_ec_usb_hammer_attach;
	device_class->detach = fu_cros_ec_usb_hammer_detach;
	device_class->prepare_firmware = fu_cros_ec_usb_hammer_prepare_firmware;
	device_class->setup = fu_cros_ec_usb_hammer_setup;
	device_class->to_string = fu_cros_ec_usb_hammer_to_string;
	device_class->write_firmware = fu_cros_ec_usb_hammer_write_firmware;
	device_class->probe = fu_cros_ec_usb_hammer_probe;
	device_class->set_progress = fu_cros_ec_usb_hammer_set_progress;
	device_class->reload = fu_cros_ec_usb_hammer_reload;
	device_class->replace = fu_cros_ec_usb_hammer_replace;
	device_class->cleanup = fu_cros_ec_usb_hammer_cleanup;
}
