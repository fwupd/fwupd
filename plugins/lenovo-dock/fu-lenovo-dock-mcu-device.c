/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-lenovo-dock-child-device.h"
#include "fu-lenovo-dock-common.h"
#include "fu-lenovo-dock-dmc-device.h"
#include "fu-lenovo-dock-firmware.h"
#include "fu-lenovo-dock-mcu-device.h"

struct _FuLenovoDockMcuDevice {
	FuHidDevice parent_instance;
};

G_DEFINE_TYPE(FuLenovoDockMcuDevice, fu_lenovo_dock_mcu_device, FU_TYPE_HID_DEVICE)

#define FU_LENOVO_DOCK_MCU_DEVICE_TIMEOUT 5000 /* ms */

static gboolean
fu_lenovo_dock_mcu_device_txrx(FuLenovoDockMcuDevice *self,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      USB_HID_REPORT_ID2,
				      buf,
				      bufsz,
				      FU_LENOVO_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;
	memset(buf, 0x0, bufsz);
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      USB_HID_REPORT_ID2,
				      buf,
				      bufsz,
				      FU_LENOVO_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_lenovo_dock_mcu_device_get_status(FuLenovoDockMcuDevice *self, GError **error)
{
	guint8 buf[64] = {USB_HID_REPORT_ID2,
			  0x4 /* length */,
			  0xFE /* tag1: MCU defined */,
			  0xFF /* tag2: MCU defined */,
			  USBUID_ISP_DEVICE_CMD_MCU_STATUS};
	buf[63] = TAG_TAG2_CMD_MCU;
	if (!fu_lenovo_dock_mcu_device_txrx(self, buf, sizeof(buf), error))
		return FALSE;

	/* TODO: not exactly sure what I'm looking at */
	return TRUE;
}

static gboolean
fu_lenovo_dock_mcu_device_enumerate_children(FuLenovoDockMcuDevice *self, GError **error)
{
	guint8 buf[64] = {USB_HID_REPORT_ID2,
			  0x5 /* length */,
			  0xFE /* tag1: MCU defined */,
			  0xFF /* tag2: MCU defined */,
			  USBUID_ISP_DEVICE_CMD_READ_MCU_VERSIONPAGE};
	struct {
		const gchar *name;
		guint8 chip_idx;
		gsize offset;
	} components[] = {
	    {"DMC", FIRMWARE_IDX_DMC_PD, G_STRUCT_OFFSET(IspVersionInMcu_t, DMC)},
	    {"PD", FIRMWARE_IDX_DP, G_STRUCT_OFFSET(IspVersionInMcu_t, PD)},
	    {"DP5x", FIRMWARE_IDX_NONE, G_STRUCT_OFFSET(IspVersionInMcu_t, DP5x)},
	    {"DP6x", FIRMWARE_IDX_NONE, G_STRUCT_OFFSET(IspVersionInMcu_t, DP6x)},
	    {"TBT4", FIRMWARE_IDX_TBT4, G_STRUCT_OFFSET(IspVersionInMcu_t, TBT4)},
	    {"USB3", FIRMWARE_IDX_USB3, G_STRUCT_OFFSET(IspVersionInMcu_t, USB3)},
	    {"USB2", FIRMWARE_IDX_USB2, G_STRUCT_OFFSET(IspVersionInMcu_t, USB2)},
	    {"AUDIO", FIRMWARE_IDX_AUDIO, G_STRUCT_OFFSET(IspVersionInMcu_t, AUDIO)},
	    {"I255", FIRMWARE_IDX_I225, G_STRUCT_OFFSET(IspVersionInMcu_t, I255)},
	    {"MCU", FIRMWARE_IDX_MCU, G_STRUCT_OFFSET(IspVersionInMcu_t, MCU)},
	    {"bcdVersion", FIRMWARE_IDX_NONE, G_STRUCT_OFFSET(IspVersionInMcu_t, bcdVersion)},
	    {NULL, 0, 0}};

	/* assume in-use */
	buf[6] = DP_VERSION_FROM_MCU | NIC_VERSION_FROM_MCU;
	buf[63] = TAG_TAG2_CMD_MCU;
	if (!fu_lenovo_dock_mcu_device_txrx(self, buf, sizeof(buf), error))
		return FALSE;

	/* TODO: not exactly sure what I'm looking at */
	for (guint i = 0; components[i].name != NULL; i++) {
		const guint8 *val = buf + components[i].offset + 5;
		g_autofree gchar *version = NULL;
		g_autofree gchar *instance_id = NULL;
		g_autoptr(FuDevice) child = NULL;

		child = fu_lenovo_dock_child_new(fu_device_get_context(FU_DEVICE(self)));
		if (g_strcmp0(components[i].name, "bcdVersion") == 0) {
			if (val[0] == 0x00 && val[1] == 0x00) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%x.%x.%02x", val[0] & 0xFu, val[0] >> 4, val[1]);
			g_debug("ignoring %s --> %s", components[i].name, version);
			continue;
		} else if (g_strcmp0(components[i].name, "DMC") == 0) {
			if (val[0] == 0x00 && val[1] == 0x00) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%d.%d.%d", val[0] >> 4, val[0] & 0xF, val[1]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
			fu_device_set_version(child, version);
			fu_device_set_name(child, "Dock Management Controller");
		} else if (g_strcmp0(components[i].name, "PD") == 0) {
			if (val[0] == 0x00 && val[1] == 0x00) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%d.%d.%d", val[0] >> 4, val[0] & 0xF, val[1]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
			fu_device_set_version(child, version);
			fu_device_set_name(child, "Power Delivery");
		} else if (g_strcmp0(components[i].name, "TBT4") == 0) {
			if (val[1] == 0x00 && val[2] == 0x00 && val[3] == 0x00) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%02x.%02x.%02x", val[1], val[2], val[3]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
			fu_device_set_version(child, version);
			fu_device_add_icon(child, "thunderbolt");
			fu_device_set_name(child, "Thunderbolt 4 Controller");
		} else if (g_strcmp0(components[i].name, "DP5x") == 0) {
			if (val[2] == 0x00 && val[3] == 0x00 && val[4] == 0x00) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%d.%02d.%03d", val[2], val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
			fu_device_set_version(child, version);
			fu_device_add_icon(child, "video-display");
			fu_device_set_name(child, "Display Port 5");
		} else if (g_strcmp0(components[i].name, "DP6x") == 0) {
			if (val[2] == 0x00 && val[3] == 0x00 && val[4] == 0x00) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%d.%02d.%03d", val[2], val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
			fu_device_set_version(child, version);
			fu_device_add_icon(child, "video-display");
			fu_device_set_name(child, "Display Port 6");
		} else if (g_strcmp0(components[i].name, "USB3") == 0) {
			if (val[3] == 0x00 && val[4] == 0x00) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%02X%02X", val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_NUMBER);
			fu_device_set_version(child, version);
			fu_device_set_name(child, "USB 3 Hub");
		} else if (g_strcmp0(components[i].name, "USB2") == 0) {
			if (val[0] == 0x00 && val[1] == 0x00 && val[2] == 0x00 && val[3] == 0x00 &&
			    val[4] == 0x00) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version =
			    g_strdup_printf("%c%c%c%c%c", val[0], val[1], val[2], val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_set_version(child, version);
			fu_device_set_name(child, "USB 2 Hub");
		} else if (g_strcmp0(components[i].name, "AUDIO") == 0) {
			if (val[2] == 0x00 && val[3] == 0x00 && val[4] == 0x00) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%02X-%02X-%02X", val[2], val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_set_version(child, version);
			fu_device_set_name(child, "Audio Controller");
		} else if (g_strcmp0(components[i].name, "I255") == 0) {
			if (val[2] == 0x00 && val[3] == 0x00 && val[4] == 0x00) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%x.%x.%x", val[2] >> 4, val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_TRIPLET);
			fu_device_set_version(child, version);
			fu_device_add_icon(child, "network-wired");
			fu_device_set_name(child, "Ethernet Adapter");
		} else if (g_strcmp0(components[i].name, "MCU") == 0) {
			if (val[3] == 0x00 && val[4] == 0x00) {
				g_debug("ignoring %s", components[i].name);
				continue;
			}
			version = g_strdup_printf("%X.%X", val[3], val[4]);
			fu_device_set_version_format(child, FWUPD_VERSION_FORMAT_PLAIN);
			fu_device_set_version(child, version);
			fu_device_set_name(child, "Dock Management Controller");
		} else {
			g_warning("unhandled %s", components[i].name);
		}

		/* add virtual device */
		instance_id = g_strdup_printf("USB\\VID_%04X&PID_%04X&CID_%s",
					      fu_usb_device_get_vid(FU_USB_DEVICE(self)),
					      fu_usb_device_get_pid(FU_USB_DEVICE(self)),
					      components[i].name);
		fu_device_add_instance_id(child, instance_id);
		if (fu_device_get_name(child) == NULL)
			fu_device_set_name(child, components[i].name);
		fu_device_set_logical_id(child, components[i].name);
		fu_lenovo_dock_child_device_set_chip_idx(FU_LENOVO_DOCK_CHILD_DEVICE(child),
							 components[i].chip_idx);
		fu_device_add_child(FU_DEVICE(self), child);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_lenovo_dock_mcu_device_setup(FuDevice *device, GError **error)
{
	FuLenovoDockMcuDevice *self = FU_LENOVO_DOCK_MCU_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_lenovo_dock_mcu_device_parent_class)->setup(device, error))
		return FALSE;

	/* get status and component versions */
	if (!fu_lenovo_dock_mcu_device_get_status(self, error))
		return FALSE;
	if (!fu_lenovo_dock_mcu_device_enumerate_children(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static FuFirmware *
fu_lenovo_dock_mcu_device_prepare_firmware(FuDevice *device,
					   GBytes *fw,
					   FwupdInstallFlags flags,
					   GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_lenovo_dock_firmware_new();
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

gboolean
fu_lenovo_dock_mcu_device_write_firmware_with_idx(FuLenovoDockMcuDevice *self,
						  FuFirmware *firmware,
						  guint8 chip_idx,
						  FuProgress *progress,
						  FwupdInstallFlags flags,
						  GError **error)
{
	// TODO -- this needs to actually send the data to the device!
	guint8 buf[64] = {USB_HID_REPORT_ID2, 4, 0xfe, 0xff, USBUID_ISP_INTERNAL_FW_CMD_UPDATE_FW};
	buf[5] = chip_idx;
	buf[6] = DP_VERSION_FROM_MCU | NIC_VERSION_FROM_MCU;
	buf[63] = TAG_TAG2_CMD_MCU; /* for ISP_AP */
	if (!fu_lenovo_dock_mcu_device_txrx(self, buf, sizeof(buf), error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_lenovo_dock_mcu_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	return fu_lenovo_dock_mcu_device_write_firmware_with_idx(FU_LENOVO_DOCK_MCU_DEVICE(device),
								 firmware,
								 0xFF, /* all */
								 progress,
								 flags,
								 error);
}

static gboolean
fu_lenovo_dock_mcu_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	// TODO -- what needs to be done before the update is started so we can send the firmware?
	return TRUE;
}

static gboolean
fu_lenovo_dock_mcu_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuLenovoDockMcuDevice *self = FU_LENOVO_DOCK_MCU_DEVICE(device);

	// TODO -- is this what we do after the update has completed to reboot the dock?
	guint8 buf[64] = {USB_HID_REPORT_ID2, 4, 0xfe, 0xff, USBUID_ISP_INTERNAL_FW_CMD_ISP_END};
	buf[63] = TAG_TAG2_CMD_MCU; /* for ISP_AP */
	return fu_lenovo_dock_mcu_device_txrx(self, buf, sizeof(buf), error);
}

static void
fu_lenovo_dock_mcu_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94);	/* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2);	/* reload */
}

static void
fu_lenovo_dock_mcu_device_init(FuLenovoDockMcuDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_INHIBIT_CHILDREN);
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.dock");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_retry_set_delay(FU_DEVICE(self), 100);
	fu_device_add_icon(FU_DEVICE(self), "dock");
}

static void
fu_lenovo_dock_mcu_device_class_init(FuLenovoDockMcuDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->prepare_firmware = fu_lenovo_dock_mcu_device_prepare_firmware;
	klass_device->write_firmware = fu_lenovo_dock_mcu_device_write_firmware;
	klass_device->attach = fu_lenovo_dock_mcu_device_attach;
	klass_device->detach = fu_lenovo_dock_mcu_device_detach;
	klass_device->setup = fu_lenovo_dock_mcu_device_setup;
	klass_device->set_progress = fu_lenovo_dock_mcu_device_set_progress;
}
