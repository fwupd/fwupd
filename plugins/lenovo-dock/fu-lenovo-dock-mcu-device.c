/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

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
fu_lenovo_dock_mcu_device_get_status(FuLenovoDockMcuDevice *self, GError **error)
{
	guint8 inbuf[64] = {0};
	guint8 outbuf[64] = {USB_HID_REPORT_ID2,
			     0x4 /* length */,
			     0xFE /* tag1: MCU defined */,
			     0xFF /* tag2: MCU defined */,
			     USBUID_ISP_DEVICE_CMD_MCU_STATUS};
	outbuf[63] = TAG_TAG2_CMD_MCU;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      USB_HID_REPORT_ID2,
				      outbuf,
				      sizeof(outbuf),
				      FU_LENOVO_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      USB_HID_REPORT_ID2,
				      inbuf,
				      sizeof(inbuf),
				      FU_LENOVO_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		return FALSE;
	}

	/* TODO: not exactly sure what I'm looking at */
	return TRUE;
}

static gboolean
fu_lenovo_dock_mcu_device_enumerate_children(FuLenovoDockMcuDevice *self, GError **error)
{
	guint8 inbuf[64] = {0};
	guint8 outbuf[64] = {USB_HID_REPORT_ID2,
			     0x5 /* length */,
			     0xFE /* tag1: MCU defined */,
			     0xFF /* tag2: MCU defined */,
			     USBUID_ISP_DEVICE_CMD_READ_MCU_VERSIONPAGE};
	struct {
		const gchar *name;
		guint8 firmware_idx;
		FwupdVersionFormat verfmt;
		gsize offset;
	} components[] = {
	    {"DMC",
	     LENOVO_DOCK_FIRMWARE_IDX_DMC_PD,
	     FWUPD_VERSION_FORMAT_TRIPLET,
	     G_STRUCT_OFFSET(IspVersionInMcu_t, DMC)},
	    {"PD",
	     LENOVO_DOCK_FIRMWARE_IDX_DP,
	     FWUPD_VERSION_FORMAT_TRIPLET,
	     G_STRUCT_OFFSET(IspVersionInMcu_t, PD)},
	    {"DP5x", FWUPD_VERSION_FORMAT_TRIPLET, G_STRUCT_OFFSET(IspVersionInMcu_t, DP5x)},
	    {"DP6x", FWUPD_VERSION_FORMAT_TRIPLET, G_STRUCT_OFFSET(IspVersionInMcu_t, DP6x)},
	    {"TBT4",
	     LENOVO_DOCK_FIRMWARE_IDX_TBT4,
	     FWUPD_VERSION_FORMAT_TRIPLET,
	     G_STRUCT_OFFSET(IspVersionInMcu_t, TBT4)},
	    {"USB3",
	     LENOVO_DOCK_FIRMWARE_IDX_USB3,
	     FWUPD_VERSION_FORMAT_NUMBER,
	     G_STRUCT_OFFSET(IspVersionInMcu_t, USB3)},
	    {"USB2",
	     LENOVO_DOCK_FIRMWARE_IDX_USB2,
	     FWUPD_VERSION_FORMAT_PLAIN,
	     G_STRUCT_OFFSET(IspVersionInMcu_t, USB2)},
	    {"AUDIO",
	     LENOVO_DOCK_FIRMWARE_IDX_AUDIO,
	     FWUPD_VERSION_FORMAT_PLAIN,
	     G_STRUCT_OFFSET(IspVersionInMcu_t, AUDIO)},
	    {"I255",
	     LENOVO_DOCK_FIRMWARE_IDX_I225,
	     FWUPD_VERSION_FORMAT_TRIPLET,
	     G_STRUCT_OFFSET(IspVersionInMcu_t, I255)},
	    {"MCU",
	     LENOVO_DOCK_FIRMWARE_IDX_MCU,
	     FWUPD_VERSION_FORMAT_PLAIN,
	     G_STRUCT_OFFSET(IspVersionInMcu_t, MCU)},
	    {"bcdVersion",
	     LENOVO_DOCK_FIRMWARE_IDX_NONE,
	     FWUPD_VERSION_FORMAT_TRIPLET,
	     G_STRUCT_OFFSET(IspVersionInMcu_t, bcdVersion)},
	    {NULL, 0}};

	/* assume in-use */
	outbuf[6] = DP_VERSION_FROM_MCU | NIC_VERSION_FROM_MCU;
	outbuf[63] = TAG_TAG2_CMD_MCU;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      USB_HID_REPORT_ID2,
				      outbuf,
				      sizeof(outbuf),
				      FU_LENOVO_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error))
		return FALSE;
	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      USB_HID_REPORT_ID2,
				      inbuf,
				      sizeof(inbuf),
				      FU_LENOVO_DOCK_MCU_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		return FALSE;
	}

	/* TODO: not exactly sure what I'm looking at */
	for (guint i = 0; components[i].name != NULL; i++) {
		const guint8 *buf = inbuf + components[i].offset + 5;
		g_autofree gchar *version = NULL;
		g_autofree gchar *instance_id = NULL;
		g_autoptr(FuDevice) child = NULL;

		if (g_strcmp0(components[i].name, "bcdVersion") == 0) {
			if (buf[0] != 0x00 || buf[1] != 0x00) {
				version = g_strdup_printf("%x.%x.%02x",
							  buf[0] & 0xFu,
							  buf[0] >> 4,
							  buf[1]);
			}
		}
		if (g_strcmp0(components[i].name, "DMC") == 0 ||
		    g_strcmp0(components[i].name, "PD") == 0) {
			if (buf[0] != 0x00 || buf[1] != 0x00) {
				version =
				    g_strdup_printf("%d.%d.%d", buf[0] >> 4, buf[0] & 0xF, buf[1]);
			}
		}
		if (g_strcmp0(components[i].name, "TBT4") == 0) {
			if (buf[1] != 0x00 || buf[2] != 0x00 || buf[3] != 0x00) {
				version = g_strdup_printf("%02x.%02x.%02x", buf[1], buf[2], buf[3]);
			}
		}
		if (g_strcmp0(components[i].name, "DP5x") == 0 ||
		    g_strcmp0(components[i].name, "DP6x") == 0) {
			if (buf[2] != 0x00 || buf[3] != 0x00 || buf[4] != 0x00) {
				version = g_strdup_printf("%d.%02d.%03d", buf[2], buf[3], buf[4]);
			}
		}
		if (g_strcmp0(components[i].name, "USB3") == 0) {
			if (buf[3] != 0x00 || buf[4] != 0x00) {
				version = g_strdup_printf("%02X%02X", buf[3], buf[4]);
			}
		}
		if (g_strcmp0(components[i].name, "USB2") == 0) {
			if (buf[0] != 0x00 || buf[1] != 0x00 || buf[2] != 0x00 || buf[3] != 0x00 ||
			    buf[4] != 0x00) {
				version = g_strdup_printf("%c%c%c%c%c",
							  buf[0],
							  buf[1],
							  buf[2],
							  buf[3],
							  buf[4]);
			}
		}
		if (g_strcmp0(components[i].name, "AUDIO") == 0) {
			if (buf[2] != 0x00 || buf[3] != 0x00 || buf[4] != 0x00) {
				version = g_strdup_printf("%02X-%02X-%02X", buf[2], buf[3], buf[4]);
			}
		}
		if (g_strcmp0(components[i].name, "I255") == 0) {
			if (buf[2] != 0x00 || buf[3] != 0x00 || buf[4] != 0x00) {
				version = g_strdup_printf("%x.%x.%x", buf[2] >> 4, buf[3], buf[4]);
			}
		}
		if (g_strcmp0(components[i].name, "MCU") == 0) {
			if (buf[3] != 0x00 || buf[4] != 0x00) {
				version = g_strdup_printf("%X.%X", buf[3], buf[4]);
			}
		}

		/* not important */
		if (version == NULL)
			continue;
		if (components[i].firmware_idx == LENOVO_DOCK_FIRMWARE_IDX_NONE) {
			g_debug("ignoring %s --> %s", components[i].name, version);
			continue;
		}

		/* add virtual device */
		child = fu_device_new_with_context(fu_device_get_context(FU_DEVICE(self)));
		instance_id = g_strdup_printf("USB\\VID_%04X&PID_%04X&CID_%s",
					      fu_usb_device_get_vid(FU_USB_DEVICE(self)),
					      fu_usb_device_get_pid(FU_USB_DEVICE(self)),
					      components[i].name);
		fu_device_add_instance_id(child, instance_id);
		fu_device_set_name(child, components[i].name);
		fu_device_set_logical_id(child, components[i].name);
		fu_device_set_version(child, version);
		fu_device_set_version_format(child, components[i].verfmt);
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

static gboolean
fu_lenovo_dock_mcu_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	// TODO
	return TRUE;
}

static gboolean
fu_lenovo_dock_mcu_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	// TODO
	return TRUE;
}

static gboolean
fu_lenovo_dock_mcu_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	// TODO
	return TRUE;
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
	fu_hid_device_add_flag(FU_HID_DEVICE(self), FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.dock");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_retry_set_delay(FU_DEVICE(self), 100);
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
