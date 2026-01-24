/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-device-list.h"
#include "fu-device-private.h"
#include "fu-engine.h"
#include "fu-plugin-private.h"
#include "fu-test.h"

static void
fu_test_device_list_count_cb(FuDeviceList *device_list, FuDevice *device, gpointer user_data)
{
	guint *cnt = (guint *)user_data;
	(*cnt)++;
}

static void
fu_device_list_no_auto_remove_children_func(gconstpointer user_data)
{
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuDevice) parent = fu_device_new(NULL);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(GPtrArray) active1 = NULL;
	g_autoptr(GPtrArray) active2 = NULL;
	g_autoptr(GPtrArray) active3 = NULL;

	/* normal behavior, remove child with parent */
	fu_device_set_id(parent, "parent");
	fu_device_set_id(child, "child");
	fu_device_add_child(parent, child);
	fu_device_list_add(device_list, parent);
	fu_device_list_add(device_list, child);
	fu_device_list_remove(device_list, parent);
	active1 = fu_device_list_get_active(device_list);
	g_assert_cmpint(active1->len, ==, 0);

	/* new-style behavior, do not remove child */
	fu_device_add_private_flag(parent, FU_DEVICE_PRIVATE_FLAG_NO_AUTO_REMOVE_CHILDREN);
	fu_device_list_add(device_list, parent);
	fu_device_list_add(device_list, child);
	fu_device_list_remove(device_list, parent);
	active2 = fu_device_list_get_active(device_list);
	g_assert_cmpint(active2->len, ==, 1);
	fu_device_list_remove(device_list, child);
	active3 = fu_device_list_get_active(device_list);
	g_assert_cmpint(active3->len, ==, 0);
}

static void
fu_device_list_delay_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "added",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &added_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "removed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "changed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &changed_cnt);

	/* add one device */
	fu_device_set_id(device1, "device1");
	fu_device_add_instance_id(device1, "foobar");
	fu_device_add_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_DELAYED_REMOVAL);
	fu_device_set_remove_delay(device1, 100);
	fu_device_list_add(device_list, device1);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* add the same device again */
	fu_device_list_add(device_list, device1);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 1);

	/* add a device with the same ID */
	fu_device_set_id(device2, "device1");
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_DELAYED_REMOVAL);
	fu_device_list_add(device_list, device2);
	fu_device_set_remove_delay(device2, 100);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 2);

	/* spin a bit */
	fu_test_loop_run_with_timeout(10);
	fu_test_loop_quit();

	/* verify only a changed event was generated */
	added_cnt = removed_cnt = changed_cnt = 0;
	fu_device_list_remove(device_list, device1);
	fu_device_list_add(device_list, device1);
	g_assert_cmpint(added_cnt, ==, 0);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 1);
}

typedef struct {
	FuDevice *device_new;
	FuDevice *device_old;
	FuDeviceList *device_list;
} FuDeviceListReplugHelper;

static gboolean
fu_device_list_remove_cb(gpointer user_data)
{
	FuDeviceListReplugHelper *helper = (FuDeviceListReplugHelper *)user_data;
	fu_device_list_remove(helper->device_list, helper->device_old);
	return FALSE;
}

static gboolean
fu_device_list_add_cb(gpointer user_data)
{
	FuDeviceListReplugHelper *helper = (FuDeviceListReplugHelper *)user_data;
	fu_device_list_add(helper->device_list, helper->device_new);
	return FALSE;
}

static void
fu_device_list_replug_auto_func(gconstpointer user_data)
{
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new(NULL);
	g_autoptr(FuDevice) device2 = fu_device_new(NULL);
	g_autoptr(FuDevice) parent = fu_device_new(NULL);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(GError) error = NULL;
	FuDeviceListReplugHelper helper;

	/* parent */
	fu_device_set_id(parent, "parent");

	/* fake child devices */
	fu_device_set_id(device1, "device1");
	fu_device_add_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_physical_id(device1, "ID");
	fu_device_set_plugin(device1, "self-test");
	fu_device_set_remove_delay(device1, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_child(parent, device1);
	fu_device_set_id(device2, "device2");
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_physical_id(device2, "ID"); /* matches */
	fu_device_set_plugin(device2, "self-test");
	fu_device_set_remove_delay(device2, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);

	/* not yet added */
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add device */
	fu_device_list_add(device_list, device1);

	/* not waiting */
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* waiting */
	helper.device_old = device1;
	helper.device_new = device2;
	helper.device_list = device_list;
	g_timeout_add(100, fu_device_list_remove_cb, &helper);
	g_timeout_add(200, fu_device_list_add_cb, &helper);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* check device2 now has parent too */
	g_assert_true(fu_device_get_parent_internal(device2) == parent);

	/* waiting, failed */
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));
}

static void
fu_device_list_replug_user_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(GError) error = NULL;
	FuDeviceListReplugHelper helper;

	/* fake devices */
	fu_device_set_id(device1, "device1");
	fu_device_set_name(device1, "device1");
	fu_device_add_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_instance_id(device1, "foo");
	fu_device_add_instance_id(device1, "bar");
	fu_device_set_plugin(device1, "self-test");
	fu_device_set_remove_delay(device1, FU_DEVICE_REMOVE_DELAY_USER_REPLUG);
	fu_device_set_id(device2, "device2");
	fu_device_set_name(device2, "device2");
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_instance_id(device2, "baz");
	fu_device_add_instance_id_full(device2,
				       "bar",
				       FU_DEVICE_INSTANCE_FLAG_COUNTERPART); /* matches */
	fu_device_set_plugin(device2, "self-test");
	fu_device_set_remove_delay(device2, FU_DEVICE_REMOVE_DELAY_USER_REPLUG);

	/* not yet added */
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add device */
	fu_device_list_add(device_list, device1);

	/* add duplicate */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_list_add(device_list, device1);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* not waiting */
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* waiting */
	helper.device_old = device1;
	helper.device_new = device2;
	helper.device_list = device_list;
	g_timeout_add(100, fu_device_list_remove_cb, &helper);
	g_timeout_add(200, fu_device_list_add_cb, &helper);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	ret = fu_device_list_wait_for_replug(device_list, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* should not be possible, but here we are */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_list_add(device_list, device1);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));
	g_assert_false(fu_device_has_flag(device2, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* add back the old device */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_list_remove(device_list, device2);
	fu_device_list_add(device_list, device1);
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));
	g_assert_false(fu_device_has_flag(device2, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));
}

static void
fu_device_list_compatible_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuDevice) device_old = NULL;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(GPtrArray) devices_all = NULL;
	g_autoptr(GPtrArray) devices_active = NULL;
	FuDevice *device;
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "added",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &added_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "removed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "changed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &changed_cnt);

	/* add one device in runtime mode */
	fu_device_set_id(device1, "device1");
	fu_device_set_plugin(device1, "plugin-for-runtime");
	fu_device_build_vendor_id(device1, "USB", "0x20A0");
	fu_device_set_version_format(device1, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device1, "1.2.3");
	fu_device_add_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_DELAYED_REMOVAL);
	fu_device_add_instance_id(device1, "foobar");
	fu_device_add_instance_id_full(device1, "bootloader", FU_DEVICE_INSTANCE_FLAG_COUNTERPART);
	fu_device_set_remove_delay(device1, 100);
	fu_device_list_add(device_list, device1);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* add another device in bootloader mode */
	fu_device_set_id(device2, "device2");
	fu_device_set_plugin(device2, "plugin-for-bootloader");
	fu_device_add_instance_id(device2, "bootloader");
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);

	/* verify only a changed event was generated */
	added_cnt = removed_cnt = changed_cnt = 0;
	fu_device_list_remove(device_list, device1);
	fu_device_list_add(device_list, device2);
	g_assert_cmpint(added_cnt, ==, 0);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 1);

	/* device2 should inherit the vendor ID and version from device1 */
	g_assert_true(fu_device_has_vendor_id(device2, "USB:0x20A0"));
	g_assert_cmpstr(fu_device_get_version(device2), ==, "1.2.3");

	/* one device is active */
	devices_active = fu_device_list_get_active(device_list);
	g_assert_cmpint(devices_active->len, ==, 1);
	device = g_ptr_array_index(devices_active, 0);
	g_assert_cmpstr(fu_device_get_id(device), ==, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");

	/* the list knows about both devices, list in order of active->old */
	devices_all = fu_device_list_get_all(device_list);
	g_assert_cmpint(devices_all->len, ==, 2);
	device = g_ptr_array_index(devices_all, 0);
	g_assert_cmpstr(fu_device_get_id(device), ==, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	device = g_ptr_array_index(devices_all, 1);
	g_assert_cmpstr(fu_device_get_id(device), ==, "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");

	/* verify we can get the old device from the new device */
	device_old = fu_device_list_get_old(device_list, device2);
	g_assert_true(device_old == device1);
}

static void
fu_device_list_remove_chain_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(FuDevice) device_child = fu_device_new(ctx);
	g_autoptr(FuDevice) device_parent = fu_device_new(ctx);

	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "added",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &added_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "removed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "changed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &changed_cnt);

	/* add child */
	fu_device_set_id(device_child, "child");
	fu_device_add_instance_id(device_child, "child-GUID-1");
	fu_device_list_add(device_list, device_child);
	g_assert_cmpint(added_cnt, ==, 1);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* add parent */
	fu_device_set_id(device_parent, "parent");
	fu_device_add_instance_id(device_parent, "parent-GUID-1");
	fu_device_add_child(device_parent, device_child);
	fu_device_list_add(device_list, device_parent);
	g_assert_cmpint(added_cnt, ==, 2);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* make sure that removing the parent causes both to go; but the child to go first */
	fu_device_list_remove(device_list, device_parent);
	g_assert_cmpint(added_cnt, ==, 2);
	g_assert_cmpint(removed_cnt, ==, 2);
	g_assert_cmpint(changed_cnt, ==, 0);
}

static void
fu_device_list_explicit_order_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	g_autoptr(FuDevice) device_child = fu_device_new(ctx);
	g_autoptr(FuDevice) device_root = fu_device_new(ctx);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();

	/* add both */
	fu_device_set_id(device_root, "device");
	fu_device_add_instance_id(device_root, "foobar");
	fu_device_set_id(device_child, "device-child");
	fu_device_add_instance_id(device_child, "baz");
	fu_device_add_child(device_root, device_child);
	fu_device_list_add(device_list, device_root);

	fu_device_add_private_flag(device_root, FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
	fu_device_list_depsolve_order(device_list, device_root);
	g_assert_cmpint(fu_device_get_order(device_root), ==, G_MAXINT);
	g_assert_cmpint(fu_device_get_order(device_child), ==, G_MAXINT);
}

static void
fu_device_list_explicit_order_post_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	g_autoptr(FuDevice) device_child = fu_device_new(ctx);
	g_autoptr(FuDevice) device_root = fu_device_new(ctx);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();

	/* add both */
	fu_device_set_id(device_root, "device");
	fu_device_add_instance_id(device_root, "foobar");
	fu_device_set_id(device_child, "device-child");
	fu_device_add_instance_id(device_child, "baz");
	fu_device_add_child(device_root, device_child);
	fu_device_list_add(device_list, device_root);
	fu_device_list_add(device_list, device_child);

	fu_device_list_depsolve_order(device_list, device_root);
	g_assert_cmpint(fu_device_get_order(device_root), ==, 0);
	g_assert_cmpint(fu_device_get_order(device_child), ==, -1);

	fu_device_add_private_flag(device_root, FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
	g_assert_cmpint(fu_device_get_order(device_root), ==, G_MAXINT);
	g_assert_cmpint(fu_device_get_order(device_child), ==, G_MAXINT);
}

static void
fu_device_list_install_parent_first_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	g_autoptr(FuDevice) device_child = fu_device_new(ctx);
	g_autoptr(FuDevice) device_root = fu_device_new(ctx);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();

	/* add both */
	fu_device_set_id(device_root, "device");
	fu_device_add_instance_id(device_root, "foobar");
	fu_device_add_private_flag(device_root, FU_DEVICE_PRIVATE_FLAG_INSTALL_PARENT_FIRST);
	fu_device_set_id(device_child, "device-child");
	fu_device_add_instance_id(device_child, "baz");
	fu_device_add_child(device_root, device_child);
	fu_device_list_add(device_list, device_root);
	fu_device_list_add(device_list, device_child);

	fu_device_list_depsolve_order(device_list, device_root);
	g_assert_cmpint(fu_device_get_order(device_root), <, fu_device_get_order(device_child));
}

static void
fu_device_list_install_parent_first_child_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	g_autoptr(FuDevice) device_child = fu_device_new(ctx);
	g_autoptr(FuDevice) device_root = fu_device_new(ctx);
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();

	/* add both */
	fu_device_set_id(device_root, "device");
	fu_device_add_instance_id(device_root, "foobar");
	fu_device_set_id(device_child, "device-child");
	fu_device_add_instance_id(device_child, "baz");
	fu_device_add_private_flag(device_child, FU_DEVICE_PRIVATE_FLAG_INSTALL_PARENT_FIRST);
	fu_device_add_child(device_root, device_child);
	fu_device_list_add(device_list, device_root);
	fu_device_list_add(device_list, device_child);

	fu_device_list_depsolve_order(device_list, device_root);
	g_assert_cmpint(fu_device_get_order(device_root), <, fu_device_get_order(device_child));
}

static void
fu_device_list_better_than_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	gboolean ret;
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuDevice) device_best = NULL;
	g_autoptr(FuDevice) device_replug = NULL;
	g_autoptr(FuEngine) engine = fu_engine_new(ctx);
	g_autoptr(FuPlugin) plugin1 = fu_plugin_new(ctx);
	g_autoptr(FuPlugin) plugin2 = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* add a bad plugin */
	fu_plugin_set_name(plugin2, "plugin2");
	fu_engine_add_plugin(engine, plugin2);

	/* add a good plugin */
	fu_plugin_set_name(plugin1, "plugin1");
	fu_plugin_add_rule(plugin1, FU_PLUGIN_RULE_BETTER_THAN, "plugin2");
	fu_engine_add_plugin(engine, plugin1);

	/* load the daemon */
	ret = fu_engine_load(engine, FU_ENGINE_LOAD_FLAG_NO_CACHE, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a higher priority device */
	fu_device_set_id(device1, "87ea5dfc8b8e384d848979496e706390b497e547");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device1, "12345678-1234-1234-1234-123456789012");
	fu_device_add_protocol(device1, "com.acme");
	fu_device_set_remove_delay(device1, FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_plugin_device_add(plugin1, device1);

	/* should be ignored */
	fu_device_set_id(device2, "87ea5dfc8b8e384d848979496e706390b497e547");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_instance_id(device2, "12345678-1234-1234-1234-123456789012");
	fu_device_add_protocol(device2, "com.acme");
	fu_plugin_device_add(plugin2, device2);

	/* ensure we still have device1 */
	device_best =
	    fu_engine_get_device(engine, "87ea5dfc8b8e384d848979496e706390b497e547", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_best);
	g_assert_true(device_best == device1);

	/* should be replaced */
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_plugin_device_add(plugin2, device2);

	/* ensure we now have device2 */
	device_replug =
	    fu_engine_get_device(engine, "87ea5dfc8b8e384d848979496e706390b497e547", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device_replug);
	g_assert_true(device_replug == device2);
}

static void
fu_device_list_counterpart_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);

	/* add and then remove runtime */
	fu_device_set_id(device1, "device-runtime");
	fu_device_add_instance_id(device1, "runtime"); /* 420dde7c-3102-5d8f-86bc-aaabd7920150 */
	fu_device_add_instance_id_full(device1, "bootloader", FU_DEVICE_INSTANCE_FLAG_COUNTERPART);
	fu_device_set_remove_delay(device1, 100);
	fu_device_list_add(device_list, device1);
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_list_remove(device_list, device1);
	g_assert_true(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* add bootloader */
	fu_device_set_id(device2, "device-bootloader");
	fu_device_add_instance_id(device2, "bootloader"); /* 015370aa-26f2-5daa-9661-a75bf4c1a913 */
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_list_add(device_list, device2);

	/* should have matched the runtime */
	g_assert_false(fu_device_has_flag(device1, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));

	/* should not have *visible* GUID of runtime */
	g_assert_false(fu_device_has_guid(device2, "runtime"));
	g_assert_false(
	    fu_device_has_instance_id(device2, "runtime", FU_DEVICE_INSTANCE_FLAG_VISIBLE));
}

static void
fu_device_list_equivalent_id_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(FuDevice) device3 = NULL;
	g_autoptr(FuDevice) device4 = NULL;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(GError) error = NULL;

	fu_device_set_id(device1, "8e9cb71aeca70d2faedb5b8aaa263f6175086b2e");
	fu_device_list_add(device_list, device1);

	fu_device_set_id(device2, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	fu_device_set_equivalent_id(device2, "8e9cb71aeca70d2faedb5b8aaa263f6175086b2e");
	fu_device_set_priority(device2, 999);
	fu_device_list_add(device_list, device2);

	device3 = fu_device_list_get_by_id(device_list, "8e9c", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device3);
	g_assert_cmpstr(fu_device_get_id(device3), ==, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");

	/* two devices with the 'same' priority */
	fu_device_set_priority(device2, 0);
	device4 = fu_device_list_get_by_id(device_list, "8e9c", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(device4);
}

static void
fu_device_list_unconnected_no_delay_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);

	fu_device_set_id(device1, "device1");
	fu_device_add_flag(device1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_instance_id(device1, "foobar");
	fu_device_list_add(device_list, device1);
	g_assert_false(fu_device_has_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));

	/* remove */
	fu_device_list_remove(device_list, device1);
	g_assert_true(fu_device_has_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));

	/* add back exact same device, then remove */
	fu_device_list_add(device_list, device1);
	g_assert_false(fu_device_has_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));
	fu_device_list_remove(device_list, device1);
	g_assert_true(fu_device_has_private_flag(device1, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));

	/* add back device with same ID, then remove */
	fu_device_set_id(device2, "device1");
	fu_device_add_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_instance_id(device2, "foobar");
	fu_device_list_add(device_list, device2);
	g_assert_false(fu_device_has_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));
	fu_device_list_remove(device_list, device2);
	g_assert_true(fu_device_has_private_flag(device2, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED));
}

static void
fu_device_list_func(gconstpointer user_data)
{
	FuContext *ctx = (FuContext *)user_data;
	g_autoptr(FuDeviceList) device_list = fu_device_list_new();
	g_autoptr(FuDevice) device1 = fu_device_new(ctx);
	g_autoptr(FuDevice) device2 = fu_device_new(ctx);
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices2 = NULL;
	g_autoptr(GError) error = NULL;
	FuDevice *device;
	guint added_cnt = 0;
	guint changed_cnt = 0;
	guint removed_cnt = 0;

	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "added",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &added_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "removed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &removed_cnt);
	g_signal_connect(FU_DEVICE_LIST(device_list),
			 "changed",
			 G_CALLBACK(fu_test_device_list_count_cb),
			 &changed_cnt);

	/* add both */
	fu_device_set_id(device1, "device1");
	fu_device_add_instance_id(device1, "foobar");
	fu_device_list_add(device_list, device1);
	fu_device_set_id(device2, "device2");
	fu_device_add_instance_id(device2, "baz");
	fu_device_list_add(device_list, device2);
	g_assert_cmpint(added_cnt, ==, 2);
	g_assert_cmpint(removed_cnt, ==, 0);
	g_assert_cmpint(changed_cnt, ==, 0);

	/* get all */
	devices = fu_device_list_get_all(device_list);
	g_assert_cmpint(devices->len, ==, 2);
	device = g_ptr_array_index(devices, 0);
	g_assert_cmpstr(fu_device_get_id(device), ==, "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");

	/* find by ID */
	device = fu_device_list_get_by_id(device_list,
					  "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a",
					  &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "99249eb1bd9ef0b6e192b271a8cb6a3090cfec7a");
	g_clear_object(&device);

	/* find by GUID */
	device =
	    fu_device_list_get_by_guid(device_list, "579a3b1c-d1db-5bdc-b6b9-e2c1b28d5b8a", &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_id(device), ==, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
	g_clear_object(&device);

	/* find by missing GUID */
	device = fu_device_list_get_by_guid(device_list, "notfound", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(device);

	/* remove device */
	added_cnt = removed_cnt = changed_cnt = 0;
	fu_device_list_remove(device_list, device1);
	g_assert_cmpint(added_cnt, ==, 0);
	g_assert_cmpint(removed_cnt, ==, 1);
	g_assert_cmpint(changed_cnt, ==, 0);
	devices2 = fu_device_list_get_all(device_list);
	g_assert_cmpint(devices2->len, ==, 1);
	device = g_ptr_array_index(devices2, 0);
	g_assert_cmpstr(fu_device_get_id(device), ==, "1a8d0d9a96ad3e67ba76cf3033623625dc6d6882");
}

int
main(int argc, char **argv)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_DATADIR", testdatadir, TRUE);

	/* do not save silo */
	ctx = fu_context_new();
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_IDLE_SOURCES);
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_test_add_data_func("/fwupd/device-list", ctx, fu_device_list_func);
	g_test_add_data_func("/fwupd/device-list/unconnected-no-delay",
			     ctx,
			     fu_device_list_unconnected_no_delay_func);
	g_test_add_data_func("/fwupd/device-list/equivalent-id",
			     ctx,
			     fu_device_list_equivalent_id_func);
	g_test_add_data_func("/fwupd/device-list/delay", ctx, fu_device_list_delay_func);
	g_test_add_data_func("/fwupd/device-list/explicit-order",
			     ctx,
			     fu_device_list_explicit_order_func);
	g_test_add_data_func("/fwupd/device-list/explicit-order-post",
			     ctx,
			     fu_device_list_explicit_order_post_func);
	g_test_add_data_func("/fwupd/device-list/install-parent-first",
			     ctx,
			     fu_device_list_install_parent_first_func);
	g_test_add_data_func("/fwupd/device-list/install-parent-first-child",
			     ctx,
			     fu_device_list_install_parent_first_child_func);
	g_test_add_data_func("/fwupd/device-list/no-auto-remove-children",
			     ctx,
			     fu_device_list_no_auto_remove_children_func);
	g_test_add_data_func("/fwupd/device-list/compatible", ctx, fu_device_list_compatible_func);
	g_test_add_data_func("/fwupd/device-list/remove-chain",
			     ctx,
			     fu_device_list_remove_chain_func);
	g_test_add_data_func("/fwupd/device-list/counterpart",
			     ctx,
			     fu_device_list_counterpart_func);
	g_test_add_data_func("/fwupd/device-list/better-than",
			     ctx,
			     fu_device_list_better_than_func);
	g_test_add_data_func("/fwupd/device-list/replug-user",
			     ctx,
			     fu_device_list_replug_user_func);
	if (g_test_slow()) {
		g_test_add_data_func("/fwupd/device-list/replug-auto",
				     ctx,
				     fu_device_list_replug_auto_func);
	}
	return g_test_run();
}
