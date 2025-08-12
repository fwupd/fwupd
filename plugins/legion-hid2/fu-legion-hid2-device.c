/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-hid2-bl-device.h"
#include "fu-legion-hid2-device.h"
#include "fu-legion-hid2-firmware.h"
#include "fu-legion-hid2-sipo-device.h"
#include "fu-legion-hid2-struct.h"

struct _FuLegionHid2Device {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuLegionHid2Device, fu_legion_hid2_device, FU_TYPE_HIDRAW_DEVICE)

#define FU_LEGION_HID2_DEVICE_TIMEOUT 200 /* ms */

static gboolean
fu_legion_hid2_device_transfer(FuLegionHid2Device *self,
			       GByteArray *req,
			       GByteArray *res,
			       GError **error)
{
	if (req != NULL) {
		if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
					  req->data,
					  req->len,
					  FU_LEGION_HID2_DEVICE_TIMEOUT,
					  FU_IO_CHANNEL_FLAG_NONE,
					  error)) {
			g_prefix_error_literal(error, "failed to write packet: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (!fu_udev_device_read(FU_UDEV_DEVICE(self),
					 res->data,
					 res->len,
					 NULL,
					 FU_LEGION_HID2_DEVICE_TIMEOUT,
					 FU_IO_CHANNEL_FLAG_NONE,
					 error)) {
			g_prefix_error_literal(error, "failed to read packet: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gchar *
fu_legion_hid2_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static gboolean
fu_legion_hid2_device_ensure_version(FuLegionHid2Device *self, GError **error)
{
	g_autoptr(GByteArray) cmd = fu_struct_legion_get_version_new();
	g_autoptr(GByteArray) result = fu_struct_legion_version_new();

	if (!fu_legion_hid2_device_transfer(self, cmd, result, error))
		return FALSE;
	fu_device_set_version_raw(FU_DEVICE(self), fu_struct_legion_version_get_version(result));

	return TRUE;
}

/*
 * older MCU firmware doesn't support TP child commands, so setup needs to
 * to be non-fatal or the MCU won't enumerate.
 */
static void
fu_legion_hid2_device_setup_touchpad_direct(FuLegionHid2Device *self)
{
	g_autoptr(GByteArray) cmd = fu_struct_legion_get_pl_test_new();
	g_autoptr(GByteArray) tp_man = fu_struct_legion_get_pl_test_result_new();
	g_autoptr(GByteArray) tp_ver = fu_struct_legion_get_pl_test_result_new();
	g_autoptr(FuDevice) child = NULL;
	g_autoptr(GError) error_child = NULL;

	/* determine which vendor touchpad */
	fu_struct_legion_get_pl_test_set_index(cmd, FU_LEGION_HID2_PL_TEST_TP_MANUFACTURER);
	if (!fu_legion_hid2_device_transfer(self, cmd, tp_man, &error_child)) {
		g_debug("failed to get touchpad manufacturer: %s", error_child->message);
		return;
	}
	switch (fu_struct_legion_get_pl_test_result_get_content(tp_man)) {
	case FU_LEGION_HID2_TP_MAN_BETTER_LIFE:
		child = fu_legion_hid2_bl_device_new(FU_DEVICE(self));
		break;
	case FU_LEGION_HID2_TP_MAN_SIPO:
		child = fu_legion_hid2_sipo_device_new(FU_DEVICE(self));
		break;
	default:
	case FU_LEGION_HID2_TP_MAN_NONE:
		g_info("no touchpad found, skipping child device setup");
		return;
	}

	/* lookup firmware from MCU (*NOT* from touchpad directly) */
	fu_struct_legion_get_pl_test_set_index(cmd, FU_LEGION_HID2_PL_TEST_TP_VERSION);
	if (!fu_legion_hid2_device_transfer(self, cmd, tp_ver, &error_child)) {
		g_debug("failed to get touchpad version: %s", error_child->message);
		return;
	}

	fu_device_set_version_raw(child, fu_struct_legion_get_pl_test_result_get_content(tp_ver));

	fu_device_add_child(FU_DEVICE(self), child);
}

static gboolean
fu_legion_hid2_device_setup_touchpad(FuLegionHid2Device *self, GError **error)
{
	guint64 version = 0;
	g_autoptr(FuDevice) child = NULL;
	g_autofree gchar *tp_version = NULL;
	g_autofree gchar *manufacturer = NULL;
	g_autoptr(FuDevice) hid_device = NULL;

	/* get parent */
	hid_device = fu_device_get_backend_parent_with_subsystem(FU_DEVICE(self), "hid", error);
	if (hid_device == NULL)
		return FALSE;

	manufacturer = fu_udev_device_read_property(FU_UDEV_DEVICE(hid_device),
						    "LEGOS_TP_MANUFACTURER",
						    error);
	if (manufacturer == NULL)
		return FALSE;
	tp_version =
	    fu_udev_device_read_property(FU_UDEV_DEVICE(hid_device), "LEGOS_TP_VERSION", error);
	if (tp_version == NULL)
		return FALSE;

	if (g_strcmp0(manufacturer, "SIPO") == 0) {
		child = fu_legion_hid2_sipo_device_new(FU_DEVICE(self));
	} else if (g_strcmp0(manufacturer, "BetterLife") == 0) {
		child = fu_legion_hid2_bl_device_new(FU_DEVICE(self));
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unknown touchpad manufacturer '%s'",
			    manufacturer);
		return FALSE;
	}

	if (!fu_strtoull(tp_version, &version, 0x0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, error)) {
		return FALSE;
	}

	fu_device_set_version_raw(child, version);
	fu_device_add_child(FU_DEVICE(self), child);

	return TRUE;
}

static gboolean
fu_legion_hid2_device_setup_version(FuLegionHid2Device *self, GError **error)
{
	FuDevice *device = FU_DEVICE(self);

	/* compatibility with older releases that used USB Instance ID */
	fu_device_add_instance_u16(device, "VID", fu_device_get_vid(device));
	fu_device_add_instance_u16(device, "PID", fu_device_get_pid(device));
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_GENERIC |
					     FU_DEVICE_INSTANCE_FLAG_VISIBLE |
					     FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "USB",
					 "VID",
					 "PID",
					 NULL);

	/* version set from kernel core */
	if (fu_device_get_version_raw(device) != 0)
		return TRUE;

	/* fallback to direct communication */
	if (!fu_legion_hid2_device_ensure_version(self, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_legion_hid2_device_validate_descriptor(FuDevice *device, GError **error)
{
	g_autoptr(FuHidDescriptor) descriptor = NULL;
	g_autoptr(FuHidReport) report = NULL;
	g_autoptr(GPtrArray) imgs = NULL;

	descriptor = fu_hidraw_device_parse_descriptor(FU_HIDRAW_DEVICE(device), error);
	if (descriptor == NULL)
		return FALSE;
	report = fu_hid_descriptor_find_report(descriptor,
					       error,
					       "usage-page",
					       0xFFA0,
					       "usage",
					       0x01,
					       "collection",
					       0x01,
					       NULL);
	if (report == NULL)
		return FALSE;

	imgs = fu_firmware_get_images(FU_FIRMWARE(descriptor));
	if (imgs->len != 4) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "HID descriptor does not contain exactly 4 reports");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_legion_hid2_device_setup(FuDevice *device, GError **error)
{
	g_autoptr(GError) error_touchpad = NULL;

	if (!fu_legion_hid2_device_validate_descriptor(device, error))
		return FALSE;

	if (!fu_legion_hid2_device_setup_version(FU_LEGION_HID2_DEVICE(device), error))
		return FALSE;

	if (!fu_legion_hid2_device_setup_touchpad(FU_LEGION_HID2_DEVICE(device), &error_touchpad)) {
		g_debug("failed to setup touchpad from HID properties: %s",
			error_touchpad->message);
		fu_legion_hid2_device_setup_touchpad_direct(FU_LEGION_HID2_DEVICE(device));
	}

	/* success */
	return TRUE;
}

static FuFirmware *
fu_legion_hid2_device_prepare_firmware(FuDevice *device,
				       GInputStream *stream,
				       FuProgress *progress,
				       FuFirmwareParseFlags flags,
				       GError **error)
{
	guint32 version;
	g_autoptr(FuFirmware) firmware = fu_legion_hid2_firmware_new();

	/* sanity check */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	version = fu_legion_hid2_firmware_get_version(firmware);
	if (fu_device_get_version_raw(device) > version) {
		g_autofree gchar *version_str =
		    fu_version_from_uint32(version, FWUPD_VERSION_FORMAT_QUAD);
		g_info("downgrading to firmware %s", version_str);
	}

	return g_steal_pointer(&firmware);
}

static gboolean
fu_legion_hid2_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autoptr(GByteArray) cmd = NULL;
	g_autoptr(GByteArray) result = NULL;
	g_autoptr(GError) error_local = NULL;

	cmd = fu_struct_legion_start_iap_new();
	result = fu_struct_legion_iap_result_new();

	if (!fu_legion_hid2_device_transfer(FU_LEGION_HID2_DEVICE(device),
					    cmd,
					    result,
					    &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT)) {
			g_debug("%s", error_local->message);
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

static void
fu_legion_hid2_device_init(FuLegionHid2Device *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.legion-hid2");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

static void
fu_legion_hid2_device_class_init(FuLegionHid2DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_legion_hid2_device_setup;
	device_class->prepare_firmware = fu_legion_hid2_device_prepare_firmware;
	device_class->convert_version = fu_legion_hid2_device_convert_version;
	device_class->detach = fu_legion_hid2_device_detach;
}
