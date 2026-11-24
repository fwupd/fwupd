/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-config-private.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-test.h"

static void
fu_backend_emulate_count_cb(FuBackend *backend, FuDevice *device, gpointer user_data)
{
	guint *cnt = (guint *)user_data;
	(*cnt)++;
}

static void
fu_backend_emulate_func(void)
{
	gboolean ret;
	guint8 buf[] = {0x00, 0x00};
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;
	FuDevice *device;
	g_autofree gchar *json3 = NULL;
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDeviceEvent) ev_parent = NULL;
	g_autoptr(FuDevice) device2 = NULL;
	g_autoptr(FuIoctl) ioctl = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *json1 = "{\n"
			     "  \"UsbDevices\": [\n"
			     "    {\n"
			     "      \"Created\": \"2023-02-01T16:35:03.302027Z\",\n"
			     "      \"GType\": \"FuUdevDevice\",\n"
			     "      \"BackendId\": \"foo:bar:baz\",\n"
			     "      \"Events\": [\n"
			     "        {\n"
			     "          \"Id\": \"Ioctl:Request=0x007b,Data=AAA=,Length=0x2\",\n"
			     "          \"Data\": \"Aw==\",\n"
			     "          \"DataOut\": \"Aw==\"\n"
			     "        },\n"
			     "        {\n"
			     "          \"Id\": \"Ioctl:Request=0x007b,Data=AAA=,Length=0x2\",\n"
			     "          \"Data\": \"Aw==\",\n"
			     "          \"DataOut\": \"Aw==\"\n"
			     "        }\n"
			     "      ]\n"
			     "    }\n"
			     "  ]\n"
			     "}";
	const gchar *json2 = "{\n"
			     "  \"FwupdVersion\": \"" PACKAGE_VERSION "\",\n"
			     "  \"UsbDevices\": [\n"
			     "    {\n"
			     "      \"Created\": \"2099-02-01T16:35:03Z\",\n"
			     "      \"GType\": \"FuUdevDevice\",\n"
			     "      \"BackendId\": \"usb:FF:FF:06\"\n"
			     "    }\n"
			     "  ]\n"
			     "}";

	/* watch events */
	backend = g_object_new(FU_TYPE_BACKEND,
			       "context",
			       ctx,
			       "name",
			       "udev",
			       "device-gtype",
			       FU_TYPE_UDEV_DEVICE,
			       NULL);
	g_signal_connect(FU_BACKEND(backend),
			 "device-added",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &added_cnt);
	g_signal_connect(FU_BACKEND(backend),
			 "device-removed",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_BACKEND(backend),
			 "device-changed",
			 G_CALLBACK(fu_backend_emulate_count_cb),
			 &changed_cnt);

	/* parse */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* get device */
	device = fu_backend_lookup_by_id(backend, "foo:bar:baz");
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));

#ifndef HAVE_IOCTL_H
	g_test_skip("no <ioctl.h> support");
	return;
#endif

	/* in-order */
	ioctl = fu_udev_device_ioctl_new(FU_UDEV_DEVICE(device));
	g_assert_nonnull(ioctl);
	ret = fu_ioctl_execute(ioctl, 123, buf, sizeof(buf), NULL, 0, FU_IOCTL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* in-order, repeat */
	buf[0] = 0x00;
	buf[1] = 0x00;
	ret = fu_ioctl_execute(ioctl, 123, buf, sizeof(buf), NULL, 0, FU_IOCTL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* out-of-order */
	buf[0] = 0x00;
	buf[1] = 0x00;
	ret = fu_ioctl_execute(ioctl, 123, buf, sizeof(buf), NULL, 0, FU_IOCTL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load the same data */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 1);
	device = fu_backend_lookup_by_id(backend, "foo:bar:baz");
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATED));

	/* load a different device */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(backend), json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(added_cnt, ==, 2);
	g_assert_cmpint(changed_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 1);
	device = fu_backend_lookup_by_id(backend, "usb:FF:FF:06");
	g_assert_no_error(error);
	g_assert_nonnull(device);

	/* save to string */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG);
	json3 = fwupd_codec_to_json_string(FWUPD_CODEC(backend), FWUPD_CODEC_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(json3);
	g_debug("%s", json3);
	ret = fu_test_compare_lines(json3, json2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* missing event, new path */
	fu_device_set_fwupd_version(device, PACKAGE_VERSION);
	device2 = fu_device_get_backend_parent_with_subsystem(device, "usb", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(device2);
	g_clear_error(&error);

	/* GetBackendParent:Subsystem=pci, validation keeps the recorded ID */
	ev_parent = fu_device_event_new("GetBackendParent:Subsystem=pci");
	fu_device_event_set_str(ev_parent, "GType", "FuUdevDevice");
	fu_device_event_set_str(ev_parent, "DeviceId", "0123456789abcdef0123456789abcdef01234567");
	fu_device_event_set_str(ev_parent, "BackendId", "/sys/devices/pci0000:00");
	fu_device_event_set_str(ev_parent, "PhysicalId", "PCI_SLOT_NAME=0000:00:00.0");
	fu_device_add_event(device, ev_parent);
	device2 = fu_device_get_backend_parent_with_subsystem(device, "pci", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device2);
	g_assert_cmpstr(fu_device_get_physical_id(device2), ==, "PCI_SLOT_NAME=0000:00:00.0");
	ret = fu_device_ensure_id(device2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_id(device2), ==, "0123456789abcdef0123456789abcdef01234567");

	/* check version */
	g_assert_false(fu_device_check_fwupd_version(device, "5.0.0"));
	g_assert_true(fu_device_check_fwupd_version(device, "1.9.19"));
}

static void
fu_backend_device_child_func(void)
{
	gboolean ret;
	gsize bytes_read = 0;
	guint8 buf[4] = {0x0};
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) child = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDeviceEvent) ev_child = NULL;
	g_autoptr(FuDeviceEvent) ev_read = NULL;
	g_autoptr(FuDeviceEvent) ev_error = NULL;
	g_autoptr(FuDeviceEvent) ev_missing = NULL;
	g_autoptr(FuDeviceEvent) ev_invalid = NULL;
	g_autoptr(FuDeviceEvent) ev_unrelated = NULL;
	g_autoptr(FuDeviceEvent) ev_filter = NULL;
	g_autoptr(GError) error = NULL;

	/* no backend set */
	device = g_object_new(FU_TYPE_UDEV_DEVICE, "context", ctx, NULL);
	child =
	    fu_device_get_backend_child(device, "usb:usb_interface", FU_DEVICE_FILTER_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(child);
	g_clear_error(&error);

	/* vfunc not implemented */
	backend = g_object_new(FU_TYPE_BACKEND,
			       "context",
			       ctx,
			       "name",
			       "udev",
			       "device-gtype",
			       FU_TYPE_UDEV_DEVICE,
			       NULL);
	child = fu_backend_get_device_child(backend,
					    device,
					    "usb:usb_interface",
					    FU_DEVICE_FILTER_NONE,
					    &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(child);
	g_clear_error(&error);
	fu_device_set_backend(device, backend);
	child =
	    fu_device_get_backend_child(device, "usb:usb_interface", FU_DEVICE_FILTER_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(child);
	g_clear_error(&error);

	/* emulated */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_fwupd_version(device, PACKAGE_VERSION);

	/* GetBackendChild:Subsystem=usb:usb_interface */
	ev_child = fu_device_event_new("GetBackendChild:Subsystem=usb:usb_interface");
	fu_device_event_set_str(ev_child, "GType", "FuUdevDevice");
	fu_device_event_set_str(ev_child, "DeviceId", "0123456789abcdef0123456789abcdef01234567");
	fu_device_event_set_str(ev_child, "BackendId", "/sys/devices/usb1/1-1/1-1:1.0");
	fu_device_event_set_str(ev_child, "PhysicalId", "1-1:1.0");
	fu_device_event_set_i64(ev_child, "Vid", 0x093a);
	fu_device_event_set_i64(ev_child, "Pid", 0x2862);
	fu_device_add_event(device, ev_child);

	/* Read:Length=0x4,Offset=0x0 -> on the child, redirected to the parent */
	ev_read = fu_device_event_new("Read:Length=0x4,Offset=0x0");
	fu_device_event_set_data(ev_read, "Data", (const guint8 *)"abcd", 4);
	fu_device_add_event(device, ev_read);

	/* GetBackendChild:Subsystem=hid -> backend error */
	ev_error = fu_device_event_new("GetBackendChild:Subsystem=hid");
	fu_device_event_set_i64(ev_error, "Error", FWUPD_ERROR_NOT_FOUND);
	fu_device_event_set_str(ev_error, "ErrorMsg", "no child with subsystem hid");
	fu_device_add_event(device, ev_error);

	/* GetBackendChild:Subsystem=video4linux -> no GType */
	ev_missing = fu_device_event_new("GetBackendChild:Subsystem=video4linux");
	fu_device_add_event(device, ev_missing);

	/* GetBackendChild:Subsystem=nvme -> unknown GType */
	ev_invalid = fu_device_event_new("GetBackendChild:Subsystem=nvme");
	fu_device_event_set_str(ev_invalid, "GType", "FuDoesNotExistDevice");
	fu_device_add_event(device, ev_invalid);

	/* GetBackendChild:Subsystem=drm -> registered GType that is not a FuDevice */
	ev_unrelated = fu_device_event_new("GetBackendChild:Subsystem=drm");
	fu_device_event_set_str(ev_unrelated, "GType", "FuBackend");
	fu_device_add_event(device, ev_unrelated);

	/* filter success */
	ev_filter = fu_device_event_new(
	    "GetBackendChild:Subsystem=usb:usb_interface,Filter=USB\\VID_273F&PID_1001");
	fu_device_event_set_str(ev_filter, "GType", "FuUdevDevice");
	fu_device_event_set_str(ev_filter, "DeviceId", "0123456789abcdef0123456789abcdef01234567");
	fu_device_event_set_str(ev_filter, "BackendId", "/sys/devices/usb1/1-1/1-1:1.0");
	fu_device_event_set_str(ev_filter, "PhysicalId", "1-1:1.0");
	fu_device_event_set_i64(ev_filter, "Vid", 0x273F);
	fu_device_event_set_i64(ev_filter, "Pid", 0x1001);
	fu_device_add_event(device, ev_filter);

	/* success */
	child =
	    fu_device_get_backend_child(device, "usb:usb_interface", FU_DEVICE_FILTER_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(child);
	g_assert_true(FU_IS_UDEV_DEVICE(child));
	g_assert_true(fu_device_has_flag(child, FWUPD_DEVICE_FLAG_EMULATED));
	g_assert_true(fu_device_get_backend(child) == backend);
	g_assert_cmpstr(fu_device_get_backend_id(child), ==, "/sys/devices/usb1/1-1/1-1:1.0");
	g_assert_cmpstr(fu_device_get_physical_id(child), ==, "1-1:1.0");
	g_assert_cmpint(fu_device_get_vid(child), ==, 0x093a);
	g_assert_cmpint(fu_device_get_pid(child), ==, 0x2862);
	g_assert_true(fu_device_check_fwupd_version(child, PACKAGE_VERSION));

	/* validation keeps the recorded ID rather than recomputing it */
	ret = fu_device_ensure_id(child, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_id(child), ==, "0123456789abcdef0123456789abcdef01234567");

	/* replayed read uses the recording version of the parent */
	ret = fu_udev_device_read(FU_UDEV_DEVICE(child),
				  buf,
				  sizeof(buf),
				  &bytes_read,
				  0,
				  FU_IO_CHANNEL_FLAG_NONE,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(bytes_read, ==, sizeof(buf));
	g_assert_cmpint(memcmp(buf, "abcd", sizeof(buf)), ==, 0);
	g_clear_object(&child);

	/* saved error */
	child = fu_device_get_backend_child(device, "hid", FU_DEVICE_FILTER_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(child);
	g_clear_error(&error);

	/* no GType */
	child = fu_device_get_backend_child(device, "video4linux", FU_DEVICE_FILTER_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(child);
	g_clear_error(&error);

	/* invalid GType */
	child = fu_device_get_backend_child(device, "nvme", FU_DEVICE_FILTER_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(child);
	g_clear_error(&error);

	/* GType not derived from FuDevice */
	child = fu_device_get_backend_child(device, "drm", FU_DEVICE_FILTER_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(child);
	g_clear_error(&error);

	/* missing event */
	child =
	    fu_device_get_backend_child(device, "usb:usb_device", FU_DEVICE_FILTER_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(child);
	g_clear_error(&error);

	/* success with filter */
	child = fu_device_get_backend_child(device,
					    "usb:usb_interface",
					    "USB\\VID_273F&PID_1001",
					    &error);
	g_assert_no_error(error);
	g_assert_nonnull(child);
}

static void
fu_backend_func(void)
{
	FuDevice *dev;
	gboolean ret;
	g_autoptr(FuBackend) backend = g_object_new(FU_TYPE_BACKEND, NULL);
	g_autoptr(FuDevice) dev1 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev2 = fu_device_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* defaults */
	g_assert_null(fu_backend_get_name(backend));
	g_assert_true(fu_backend_has_flag(backend, FU_BACKEND_FLAG_ENABLED));

	/* load */
	ret = fu_backend_setup(backend, FU_BACKEND_SETUP_FLAG_NONE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_backend_coldplug(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add two devices, then remove one of them */
	fu_device_set_physical_id(dev1, "dev1");
	fu_backend_device_added(backend, dev1);
	fu_device_set_physical_id(dev2, "dev2");
	fu_backend_device_added(backend, dev2);
	fu_backend_device_changed(backend, dev2);
	fu_backend_device_removed(backend, dev2);

	dev = fu_backend_lookup_by_id(backend, "dev1");
	g_assert_nonnull(dev);
	g_assert_true(dev == dev1);

	/* should have been removed */
	dev = fu_backend_lookup_by_id(backend, "dev2");
	g_assert_null(dev);

	/* get linear array */
	devices = fu_backend_get_devices(backend);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	dev = g_ptr_array_index(devices, 0);
	g_assert_nonnull(dev);
	g_assert_true(dev == dev1);
}

static void
fu_backend_config_func(void)
{
	g_autofree gchar *value_none = NULL;
	g_autofree gchar *value_backend = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuBackend) backend = NULL;

	backend = g_object_new(FU_TYPE_BACKEND, "context", ctx, "name", "foo", NULL);
	value_none = fu_backend_get_config_str(backend, "Key");
	g_assert_null(value_none);

	/* only the section named after the backend is used */
	fu_config_set_value_internal(fu_context_get_config(ctx), "bar", "Key", "wrong");
	fu_config_set_value_internal(fu_context_get_config(ctx), "foo", "Key", "right");
	value_backend = fu_backend_get_config_str(backend, "Key");
	g_assert_cmpstr(value_backend, ==, "right");
}

static void
fu_backend_device_parent_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuBackend) backend = g_object_new(FU_TYPE_BACKEND, "context", ctx, NULL);
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) parent = NULL;
	g_autoptr(FuDevice) grandparent = NULL;
	g_autoptr(FuDevice) root = NULL;
	g_autoptr(FuDeviceEvent) ev_parent = NULL;
	g_autoptr(FuDeviceEvent) ev_grandparent = NULL;
	g_autoptr(FuDeviceEvent) ev_root = NULL;
	g_autoptr(GError) error = NULL;

	/* emulated */
	fu_device_set_backend(device, backend);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_fwupd_version(device, PACKAGE_VERSION);
	fu_device_set_physical_id(device, "1-4.1.2");

	/* hop 1: device -> parent */
	ev_parent = fu_device_event_new("GetBackendParent");
	fu_device_event_set_str(ev_parent, "GType", "FuDevice");
	fu_device_event_set_str(ev_parent, "DeviceId", "1111111111111111111111111111111111111122");
	fu_device_event_set_str(ev_parent, "BackendId", "/sys/devices/usb1/1-4/1-4.1");
	fu_device_event_set_str(ev_parent, "PhysicalId", "1-4.1");
	fu_device_event_set_i64(ev_parent, "Vid", 0x17EF);
	fu_device_event_set_i64(ev_parent, "Pid", 0x1122);
	fu_device_add_event(device, ev_parent);

	/* hop 2: parent -> grandparent (redirected to device via priv->target) */
	ev_grandparent = fu_device_event_new("GetBackendParent");
	fu_device_event_set_str(ev_grandparent, "GType", "FuDevice");
	fu_device_event_set_str(ev_grandparent,
				"DeviceId",
				"1111111111111111111111111111111111111121");
	fu_device_event_set_str(ev_grandparent, "BackendId", "/sys/devices/usb1/1-4");
	fu_device_event_set_str(ev_grandparent, "PhysicalId", "1-4");
	fu_device_event_set_i64(ev_grandparent, "Vid", 0x17EF);
	fu_device_event_set_i64(ev_grandparent, "Pid", 0x1121);
	fu_device_add_event(device, ev_grandparent);

	/* hop 3: grandparent -> root after intermediate parent is unreffed */
	ev_root = fu_device_event_new("GetBackendParent");
	fu_device_event_set_str(ev_root, "GType", "FuDevice");
	fu_device_event_set_str(ev_root, "DeviceId", "1111111111111111111111111111111111111100");
	fu_device_event_set_str(ev_root, "BackendId", "/sys/devices/usb1");
	fu_device_event_set_str(ev_root, "PhysicalId", "usb1");
	fu_device_event_set_i64(ev_root, "Vid", 0x1D6B);
	fu_device_event_set_i64(ev_root, "Pid", 0x0002);
	fu_device_add_event(device, ev_root);

	/* hop 1 */
	parent = fu_device_get_backend_parent(device, &error);
	g_assert_no_error(error);
	g_assert_nonnull(parent);
	g_assert_true(fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_EMULATED));
	g_assert_true(fu_device_get_backend(parent) == backend);
	g_assert_true(fu_device_check_fwupd_version(parent, PACKAGE_VERSION));
	g_assert_cmpstr(fu_device_get_physical_id(parent), ==, "1-4.1");
	g_assert_cmpint(fu_device_get_vid(parent), ==, 0x17EF);
	g_assert_cmpint(fu_device_get_pid(parent), ==, 0x1122);

	/* hop 2 */
	grandparent = fu_device_get_backend_parent(parent, &error);
	g_assert_no_error(error);
	g_assert_nonnull(grandparent);
	g_assert_true(fu_device_has_flag(grandparent, FWUPD_DEVICE_FLAG_EMULATED));
	g_assert_true(fu_device_get_backend(grandparent) == backend);
	g_assert_true(fu_device_check_fwupd_version(grandparent, PACKAGE_VERSION));
	g_assert_cmpstr(fu_device_get_physical_id(grandparent), ==, "1-4");
	g_assert_cmpint(fu_device_get_vid(grandparent), ==, 0x17EF);
	g_assert_cmpint(fu_device_get_pid(grandparent), ==, 0x1121);

	/* hop 3 after dropping intermediate parent: root target weak pointer must stay intact */
	g_clear_object(&parent);
	root = fu_device_get_backend_parent(grandparent, &error);
	g_assert_no_error(error);
	g_assert_nonnull(root);
	g_assert_cmpstr(fu_device_get_physical_id(root), ==, "usb1");
	g_assert_cmpint(fu_device_get_vid(root), ==, 0x1D6B);
	g_assert_cmpint(fu_device_get_pid(root), ==, 0x0002);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/backend", fu_backend_func);
	g_test_add_func("/fwupd/backend/config", fu_backend_config_func);
	g_test_add_func("/fwupd/backend/emulate", fu_backend_emulate_func);
	g_test_add_func("/fwupd/backend/device-child", fu_backend_device_child_func);
	g_test_add_func("/fwupd/backend/device-parent", fu_backend_device_parent_func);
	return g_test_run();
}
