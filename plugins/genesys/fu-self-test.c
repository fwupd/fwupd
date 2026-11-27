/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-device-event-private.h"
#include "fu-device-private.h"
#include "fu-genesys-usbhub-device.h"
#include "fu-genesys-usbhub-firmware.h"
#include "fu-genesys-usbhub-struct.h"

/* nocheck:magic-inlines=80 */

/* build a minimal GL3523 hub image with optional brand project tool string */
static GBytes *
fu_genesys_usbhub_firmware_test_build(FuGenesysTsVersion ts_version,
				      const gchar *ic_type,
				      const gchar *project)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;

	fwupd_json_object_add_string(json_obj,
				     "tool_string_version",
				     fu_genesys_ts_version_to_string(ts_version));
	fwupd_json_object_add_string(json_obj, "mask_project_code", "ABCD");
	fwupd_json_object_add_string(json_obj, "mask_project_ic_type", ic_type);
	if (project != NULL)
		fwupd_json_object_add_string(json_obj, "brand_project", project);
	ret = fu_firmware_build(firmware, json_obj, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fw = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fw);
	return g_steal_pointer(&fw);
}

static void
fu_genesys_usbhub_firmware_json_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "genesys-usbhub.builder.json", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  NULL,
						  FU_FIRMWARE_BUILDER_FLAG_NONE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_genesys_usbhub_firmware_brand_project_func(void)
{
	gboolean ret;
	const gchar *tmp;
	g_autoptr(FuFirmware) firmware = g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;

	fw = fu_genesys_usbhub_firmware_test_build(FU_GENESYS_TS_VERSION_BRAND_PROJECT,
						   "352310",
						   "MYPROJECT");
	ret = fu_firmware_parse_bytes(firmware, fw, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_genesys_usbhub_firmware_get_brand_project(FU_GENESYS_USBHUB_FIRMWARE(firmware));
	g_assert_cmpstr(tmp, ==, "MYPROJECT");
	tmp = fu_genesys_usbhub_firmware_get_mask_project_ic_type(
	    FU_GENESYS_USBHUB_FIRMWARE(firmware));
	g_assert_cmpstr(tmp, ==, "352310");
}

static void
fu_genesys_usbhub_firmware_brand_project_old_func(void)
{
	gboolean ret;
	const gchar *tmp;
	g_autoptr(FuFirmware) firmware = g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;

	/* tool string too old to have a project, so it should be ignored */
	fw = fu_genesys_usbhub_firmware_test_build(FU_GENESYS_TS_VERSION_DYNAMIC_13_BYTE,
						   "352310",
						   "MYPROJECT");
	ret = fu_firmware_parse_bytes(firmware, fw, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_genesys_usbhub_firmware_get_brand_project(FU_GENESYS_USBHUB_FIRMWARE(firmware));
	g_assert_null(tmp);
	tmp = fu_genesys_usbhub_firmware_get_mask_project_ic_type(
	    FU_GENESYS_USBHUB_FIRMWARE(firmware));
	g_assert_cmpstr(tmp, ==, "352310");
}

static void
fu_genesys_usbhub_device_strict_project_check_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error2 = NULL;

	fw = fu_genesys_usbhub_firmware_test_build(FU_GENESYS_TS_VERSION_BRAND_PROJECT,
						   "352310",
						   "MYPROJECT");
	ret = fu_firmware_parse_bytes(firmware, fw, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* device has no project, so the strict check must refuse the firmware */
	device = g_object_new(FU_TYPE_GENESYS_USBHUB_DEVICE, "context", ctx, NULL);
	fu_device_add_private_flag(device, FU_GENESYS_USBHUB_FLAG_VERIFY_PROJECT);
	ret = fu_device_check_firmware(device, firmware, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_nonnull(strstr(error->message, "PROJECT"));
	g_assert_false(ret);

	/* without the flag the project is not checked, and it fails later for another reason */
	fu_device_remove_private_flag(device, FU_GENESYS_USBHUB_FLAG_VERIFY_PROJECT);
	ret = fu_device_check_firmware(device, firmware, FU_FIRMWARE_PARSE_FLAG_NONE, &error2);
	g_assert_error(error2, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(strstr(error2->message, "PROJECT"));
	g_assert_false(ret);
}

/* nocheck:name */
static void
fu_genesys_usbhub_test_add_str_desc(FuDevice *device,
				    guint8 desc_idx,
				    const guint8 *payload,
				    gsize sz)
{
	g_autofree gchar *ev_id =
	    g_strdup_printf("GetStringDescriptorBytes:DescIndex=0x%02x,Langid=0x0409,Length=0x40",
			    desc_idx);
	g_autoptr(FuDeviceEvent) ev = fu_device_event_new(ev_id);
	guint8 desc[64] = {0};

	g_assert_cmpuint(2 + (sz * 2), <=, sizeof(desc));
	desc[0] = (guint8)(2 + (sz * 2));
	desc[1] = 0x03;
	for (gsize i = 0; i < sz; i++)
		desc[2 + (i * 2)] = payload[i];
	fu_device_event_set_data(ev, "Data", desc, desc[0]);
	fu_device_add_event(device, ev);
}

static FuDevice *
fu_genesys_usbhub_test_build_parent_id_hub(FuContext *ctx, FuBackend *backend, gboolean with_parent)
{
	g_autoptr(FuDevice) hub = g_object_new(FU_TYPE_GENESYS_USBHUB_DEVICE, "context", ctx, NULL);
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(FwupdJsonArray) json_ifaces = fwupd_json_array_new();
	g_autoptr(FwupdJsonObject) json_iface = fwupd_json_object_new();
	g_autoptr(FuStructGenesysTsStatic) st_static = fu_struct_genesys_ts_static_new();
	g_autoptr(GError) error = NULL;
	const guint8 child_usb_desc[] = {
	    0x12, 0x01, 0x10, 0x02, 0x09, 0x00, 0x02, 0x40, 0xe3, 0x05, 0x10, 0x06, 0x10,
	    0x87, 0x01, 0x02, 0x00, 0x01, /* 05e3:0610 device + hub interface */
	    0x09, 0x02, 0x19, 0x00, 0x01, 0x01, 0x00, 0xe0, 0x00, 0x09, 0x04, 0x00, 0x00,
	    0x01, 0x09, 0x00, 0x01, 0x00, 0x07, 0x05, 0x81, 0x03, 0x01, 0x00, 0x0c,
	};
	const guint8 hub_dt[9] = {0x09, 0x29, 0x04, 0xed, 0x00, 0x32, 0x64, 0x10, 0xff};
	const guint8 tok_hub[] = {'G', 'L', ' ', '3', '.', '0', ' ', 'H', 'u', 'b'};
	const guint8 zero1[1] = {0x00};
	const guint8 zero2[2] = {0x00, 0x00};
	const guint8 c84012[3] = {0xc8, 0x40, 0x12};
	guint8 dyn_buf[31] = {0};
	guint8 fwinfo_buf[31] = {0};
	guint8 vendor_buf[31] = {0};

	fu_device_set_backend(hub, backend);
	fu_device_add_flag(hub, FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_add_private_flag(hub, FU_GENESYS_USBHUB_FLAG_BUILD_PARENT_ID);
	fu_device_set_fwupd_version(hub, PACKAGE_VERSION);

	fwupd_json_object_add_string(json_obj, "PlatformId", "1-4.1");
	fwupd_json_object_add_integer(json_obj, "IdVendor", 0x05E3);
	fwupd_json_object_add_integer(json_obj, "IdProduct", 0x0610);
	fwupd_json_object_add_integer(json_obj, "USB", 0x0210);
	fwupd_json_object_add_integer(json_obj, "DeviceClass", 0x09);
	fwupd_json_object_add_integer(json_iface, "Length", 9);
	fwupd_json_object_add_integer(json_iface, "DescriptorType", 4);
	fwupd_json_object_add_integer(json_iface, "InterfaceClass", 9);
	fwupd_json_array_add_object(json_ifaces, json_iface);
	fwupd_json_object_add_array(json_obj, "UsbInterfaces", json_ifaces);
	g_assert_true(fu_device_build(hub, json_obj, &error));
	g_assert_no_error(error);

	/* real FuUdevBackend emulation captures include the child hub's own LoadDescriptor event */
	{
		g_autoptr(FuDeviceEvent) ev_desc =
		    fu_device_event_new("LoadDescriptor:basename=descriptors");
		fu_device_event_set_data(ev_desc, "Data", child_usb_desc, sizeof(child_usb_desc));
		fu_device_add_event(hub, ev_desc);
	}
	{
		g_autoptr(FuDeviceEvent) ev_hub_dt = fu_device_event_new(
		    "ControlTransfer:Direction=0x00,RequestType=0x01,Recipient=0x00,"
		    "Request=0x06,Value=0x2900,Idx=0x0000,Data=AAAAAAAAAAAAAAAA,Length=0xc");
		fu_device_event_set_data(ev_hub_dt, "Data", hub_dt, sizeof(hub_dt));
		fu_device_add_event(hub, ev_hub_dt);
	}
	fu_genesys_usbhub_test_add_str_desc(hub, 0x80, tok_hub, sizeof(tok_hub));

	fu_struct_genesys_ts_static_set_tool_string_version(st_static,
							    FU_GENESYS_TS_VERSION_VENDOR_SUPPORT);
	g_assert_true(fu_struct_genesys_ts_static_set_mask_project_code(st_static, "7466", &error));
	g_assert_true(
	    fu_struct_genesys_ts_static_set_mask_project_hardware(st_static, "1", &error));
	g_assert_true(
	    fu_struct_genesys_ts_static_set_mask_project_firmware(st_static, "03", &error));
	g_assert_true(
	    fu_struct_genesys_ts_static_set_mask_project_ic_type(st_static, "352350", &error));
	g_assert_true(
	    fu_struct_genesys_ts_static_set_running_project_code(st_static, "7466", &error));
	g_assert_true(
	    fu_struct_genesys_ts_static_set_running_project_hardware(st_static, "1", &error));
	g_assert_true(
	    fu_struct_genesys_ts_static_set_running_project_firmware(st_static, "03", &error));
	g_assert_true(
	    fu_struct_genesys_ts_static_set_running_project_ic_type(st_static, "352350", &error));
	g_assert_true(fu_struct_genesys_ts_static_set_firmware_version(st_static, "8716", &error));
	fu_genesys_usbhub_test_add_str_desc(hub, 0x81, st_static->buf->data, st_static->buf->len);

	memset(dyn_buf, '0', sizeof(dyn_buf));
	dyn_buf[0] = 'C';
	dyn_buf[22] = 'F';
	fu_genesys_usbhub_test_add_str_desc(hub, 0x82, dyn_buf, sizeof(dyn_buf));

	memset(fwinfo_buf, '-', sizeof(fwinfo_buf));
	g_assert_true(fu_memcpy_safe(fwinfo_buf,
				     sizeof(fwinfo_buf),
				     7,
				     (const guint8 *)"202504161838",
				     12,
				     0,
				     12,
				     &error));
	fu_genesys_usbhub_test_add_str_desc(hub, 0x83, fwinfo_buf, sizeof(fwinfo_buf));

	memset(vendor_buf, '0', sizeof(vendor_buf));
	vendor_buf[0] = '0';
	vendor_buf[1] = '3';
	fu_genesys_usbhub_test_add_str_desc(hub, 0x86, vendor_buf, sizeof(vendor_buf));

	/* ISP_ENTER + flash status 0x05 + CFI RDID 0x9F02 -> C84012 */
	{
		g_autoptr(FuDeviceEvent) ev = fu_device_event_new(
		    "ControlTransfer:Direction=0x01,RequestType=0x02,Recipient=0x00,"
		    "Request=0x81,Value=0x0001,Idx=0x0000,Data=,Length=0x0");
		fu_device_event_set_data(ev, "Data", NULL, 0);
		fu_device_add_event(hub, ev);
	}
	{
		g_autoptr(FuDeviceEvent) ev = fu_device_event_new(
		    "ControlTransfer:Direction=0x00,RequestType=0x02,Recipient=0x00,"
		    "Request=0x82,Value=0x0502,Idx=0x0000,Data=AA==,Length=0x1");
		fu_device_event_set_data(ev, "Data", zero1, sizeof(zero1));
		fu_device_add_event(hub, ev);
	}
	{
		const gchar *rdids[] = {"0x1d02", "0x4b01", "0x9001"};
		for (guint i = 0; i < G_N_ELEMENTS(rdids); i++) {
			g_autofree gchar *ev_id = g_strdup_printf(
			    "ControlTransfer:Direction=0x00,RequestType=0x02,Recipient=0x00,"
			    "Request=0x82,Value=%s,Idx=0x0000,Data=AAA=,Length=0x2",
			    rdids[i]);
			g_autoptr(FuDeviceEvent) ev = fu_device_event_new(ev_id);
			fu_device_event_set_data(ev, "Data", zero2, sizeof(zero2));
			fu_device_add_event(hub, ev);
		}
	}
	{
		g_autoptr(FuDeviceEvent) ev = fu_device_event_new(
		    "ControlTransfer:Direction=0x00,RequestType=0x02,Recipient=0x00,"
		    "Request=0x82,Value=0x9f02,Idx=0x0000,Data=AAAA,Length=0x3");
		fu_device_event_set_data(ev, "Data", c84012, sizeof(c84012));
		fu_device_add_event(hub, ev);
	}
	{
		g_autoptr(FuDeviceEvent) ev_p =
		    fu_device_event_new("GetBackendParent:Subsystem=usb:usb_device");
		if (with_parent) {
			fu_device_event_set_str(ev_p, "GType", "FuUsbDevice");
			fu_device_event_set_str(ev_p, "PhysicalId", "1-4");
			fu_device_event_set_i64(ev_p, "Vid", 0x05E3);
			fu_device_event_set_i64(ev_p, "Pid", 0x0608);
		} else {
			g_autoptr(GError) err_parent =
			    g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no parent");
			fu_device_event_set_error(ev_p, err_parent);
		}
		fu_device_add_event(hub, ev_p);
	}
	return g_steal_pointer(&hub);
}

static void
fu_genesys_usbhub_device_build_parent_id_emulation_func(void)
{
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_CACHE);
	g_autoptr(FuBackend) backend = g_object_new(FU_TYPE_BACKEND, "context", ctx, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuDevice) hub_ok = NULL;
	g_autoptr(FuDevice) hub_missing_parent = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *cfi_quirk = "[CFI\\FLASHID_C84012]\n"
				 "Name = GD25Q20C\n"
				 "FirmwareSizeMax = 0x40000\n"
				 "BlockSize = 0x10000\n"
				 "SectorSize = 0x1000\n";

	tmpdir = fu_temporary_directory_new("genesys-parent-id", &error);
	g_assert_no_error(error);
	{
		g_autofree gchar *cfi_fn = fu_temporary_directory_build(tmpdir, "cfi.quirk", NULL);
		g_assert_true(g_file_set_contents(cfi_fn, cfi_quirk, -1, &error));
	}
	fu_context_set_path(ctx,
			    FU_PATH_KIND_DATADIR_QUIRKS,
			    fu_temporary_directory_get_path(tmpdir));
	g_assert_true(fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error));
	g_assert_no_error(error);

	/* emulated parent FuUsbDevice must keep its recorded PPID=0608 instead of wrapping to
	 * parse the child hub's own LoadDescriptor (0610) */
	hub_ok = fu_genesys_usbhub_test_build_parent_id_hub(ctx, backend, TRUE);
	g_assert_true(fu_device_probe(hub_ok, &error));
	g_assert_no_error(error);
	{
		g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(hub_ok, &error);
		g_assert_no_error(error);
		g_assert_nonnull(locker);
	}
	g_assert_true(fu_device_has_guid(hub_ok, "USB\\VID_05E3&PID_0610&PVID_05E3&PPID_0608"));
	g_assert_false(fu_device_has_guid(hub_ok, "USB\\VID_05E3&PID_0610&PVID_05E3&PPID_0610"));

	/* missing parent still fails closed */
	hub_missing_parent = fu_genesys_usbhub_test_build_parent_id_hub(ctx, backend, FALSE);
	g_assert_true(fu_device_probe(hub_missing_parent, &error));
	g_assert_no_error(error);
	{
		g_autoptr(FuDeviceLocker) locker = fu_device_locker_new(hub_missing_parent, &error);
		g_assert_null(locker);
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
		g_clear_error(&error);
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_GENESYS_USBHUB_FIRMWARE);
	g_test_add_func("/genesys/usbhub-firmware/json", fu_genesys_usbhub_firmware_json_func);
	g_test_add_func("/genesys/usbhub-firmware/brand-project",
			fu_genesys_usbhub_firmware_brand_project_func);
	g_test_add_func("/genesys/usbhub-firmware/brand-project-old",
			fu_genesys_usbhub_firmware_brand_project_old_func);
	g_test_add_func("/genesys/usbhub-device/strict-project-check",
			fu_genesys_usbhub_device_strict_project_check_func);
	g_test_add_func("/genesys/usbhub-device/build-parent-id-emulation",
			fu_genesys_usbhub_device_build_parent_id_emulation_func);
	return g_test_run();
}
