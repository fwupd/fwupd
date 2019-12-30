/*
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <umockdev.h>

#include <locale.h>

#include "fu-plugin-private.h"
#include "fu-thunderbolt-image.h"

static gchar *
udev_mock_add_domain (UMockdevTestbed *bed, int id)
{
	gchar *path;
	g_autofree gchar *name = NULL;

	name = g_strdup_printf ("domain%d", id);
	path = umockdev_testbed_add_device (bed, "thunderbolt", name,
					    NULL,
					    "security", "secure",
					    NULL,
					    "DEVTYPE",
					    "thunderbolt_domain",
					    NULL);

	g_assert_nonnull (path);
	return path;
}


static gchar *
udev_mock_add_nvmem (UMockdevTestbed *bed,
		     gboolean         active,
		     const char      *parent,
		     int              id)
{
	g_autofree gchar *name = NULL;
	gchar *path;

	name = g_strdup_printf ("%s%d", active ? "nvm_active" : "nvm_non_active", id);
	path = umockdev_testbed_add_device (bed, "nvmem", name,
					    parent,
					    "nvmem", "",
					    NULL,
					    NULL);

	g_assert_nonnull (path);
	return path;
}

typedef struct MockDevice MockDevice;

struct MockDevice {

	const char *name; /* sysfs: device_name */
	const char *id;   /* sysfs: device */
	const char *nvm_version;
	const char *nvm_parsed_version;

	int delay_ms;

	int domain_id;

	struct MockDevice *children;

	/* optionally filled out */
	const char *uuid;
};

typedef struct MockTree MockTree;

struct MockTree {
	const MockDevice *device;

	MockTree  *parent;
	GPtrArray *children;

	gchar *sysfs_parent;
	int    sysfs_id;
	int    sysfs_nvm_id;

	gchar *uuid;

	UMockdevTestbed *bed;
	gchar  *path;
	gchar  *nvm_non_active;
	gchar  *nvm_active;
	guint   nvm_authenticate;
	gchar  *nvm_version;

	FuDevice *fu_device;
};

static MockTree *
mock_tree_new (MockTree *parent, MockDevice *device, int *id)
{
	MockTree *node = g_slice_new0 (MockTree);
	int current_id = (*id)++;

	node->device = device;
	node->sysfs_id = current_id;
	node->sysfs_nvm_id = current_id;
	node->parent = parent;

	if (device->uuid)
		node->uuid = g_strdup (device->uuid);
	else
		node->uuid = g_uuid_string_random ();

	node->nvm_version = g_strdup (device->nvm_version);
	return node;
}

static void
mock_tree_free (MockTree *tree)
{
	for (guint i = 0; i < tree->children->len; i++) {
		MockTree *child = g_ptr_array_index (tree->children, i);
		mock_tree_free (child);
	}

	g_ptr_array_free (tree->children, TRUE);

	if (tree->fu_device)
		g_object_unref (tree->fu_device);

	g_free (tree->uuid);
	if (tree->bed != NULL) {
		if (tree->nvm_active) {
			umockdev_testbed_uevent (tree->bed, tree->nvm_active, "remove");
			umockdev_testbed_remove_device (tree->bed, tree->nvm_active);
		}

		if (tree->nvm_non_active) {
			umockdev_testbed_uevent (tree->bed, tree->nvm_non_active, "remove");
			umockdev_testbed_remove_device (tree->bed, tree->nvm_non_active);
		}

		if (tree->path) {
			umockdev_testbed_uevent (tree->bed, tree->path, "remove");
			umockdev_testbed_remove_device (tree->bed, tree->path);
		}


		g_object_unref (tree->bed);
	}

	g_free (tree->nvm_version);
	g_free (tree->nvm_active);
	g_free (tree->nvm_non_active);
	g_free (tree->path);
	g_free (tree->sysfs_parent);
	g_slice_free (MockTree, tree);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MockTree, mock_tree_free);
#pragma clang diagnostic pop

static GPtrArray *
mock_tree_init_children (MockTree *node, int *id)
{
	GPtrArray *children = g_ptr_array_new ();
	MockDevice *iter;

	for (iter = node->device->children; iter && iter->name; iter++) {
		MockTree *child = mock_tree_new (node, iter, id);
		g_ptr_array_add (children, child);
		child->children = mock_tree_init_children (child, id);
	}

	return children;
}

static MockTree *
mock_tree_init (MockDevice *device)
{
	MockTree *tree;
	int devices = 0;

	tree = mock_tree_new (NULL, device, &devices);
	tree->children = mock_tree_init_children (tree, &devices);

	return tree;
}

static void
mock_tree_dump (const MockTree *node, int level)
{
	if (node->path) {
		g_debug ("%*s * %s [%s] at %s", level, " ",
			 node->device->name, node->uuid, node->path);
		g_debug ("%*s   non-active nvmem at %s", level, " ",
			 node->nvm_non_active);
		g_debug ("%*s   active nvmem at %s", level, " ",
			 node->nvm_active);
	} else {
		g_debug ("%*s * %s [%s] %d", level, " ",
			 node->device->name, node->uuid, node->sysfs_id);
	}

	for (guint i = 0; i < node->children->len; i++) {
		const MockTree *child = g_ptr_array_index (node->children, i);
		mock_tree_dump (child, level + 2);
	}
}

static void
mock_tree_firmware_verify (const MockTree *node, GBytes *data)
{
	g_autoptr(GFile) nvm_device = NULL;
	g_autoptr(GFile) nvm = NULL;
	g_autoptr(GInputStream) is = NULL;
	g_autoptr(GChecksum) chk = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *sum_data = NULL;
	const gchar *sum_disk = NULL;
	gsize s;

	sum_data = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, data);
	chk = g_checksum_new (G_CHECKSUM_SHA1);

	g_assert_nonnull (node);
	g_assert_nonnull (node->nvm_non_active);

	nvm_device = g_file_new_for_path (node->nvm_non_active);
	nvm = g_file_get_child (nvm_device, "nvmem");

	is = (GInputStream *) g_file_read (nvm, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (is);

	do {
		g_autoptr(GBytes) b = NULL;
		const guchar *d;

		b = g_input_stream_read_bytes (is, 4096, NULL, &error);
		g_assert_no_error (error);
		g_assert_nonnull (is);

		d = g_bytes_get_data (b, &s);
		if (s > 0)
			g_checksum_update (chk, d, (gssize) s);

	} while (s > 0);

	sum_disk = g_checksum_get_string (chk);

	g_assert_cmpstr (sum_data, ==, sum_disk);
}

typedef gboolean (* MockTreePredicate) (const MockTree *node, gpointer data);

static const MockTree *
mock_tree_contains (const MockTree    *node,
		    MockTreePredicate  predicate,
		    gpointer           data)
{
	if (predicate (node, data))
		return node;

	for (guint i = 0; i < node->children->len; i++) {
		const MockTree *child;
		const MockTree *match;

		child = g_ptr_array_index (node->children, i);
		match = mock_tree_contains (child, predicate, data);
		if (match != NULL)
			return match;
	}

	return NULL;
}

static gboolean
mock_tree_all (const MockTree    *node,
	       MockTreePredicate  predicate,
	       gpointer           data)
{
	if (!predicate (node, data))
		return FALSE;

	for (guint i = 0; i < node->children->len; i++) {
		const MockTree *child;

		child = g_ptr_array_index (node->children, i);
		if (!mock_tree_all (child, predicate, data))
			return FALSE;
	}

	return TRUE;
}

static gboolean
mock_tree_compare_uuid (const MockTree *node, gpointer data)
{
	const gchar *uuid = (const gchar *) data;
	return g_str_equal (node->uuid, uuid);
}

static const MockTree *
mock_tree_find_uuid (const MockTree *root, const char *uuid)
{
	return mock_tree_contains (root,
				   mock_tree_compare_uuid,
				   (gpointer) uuid);
}

static gboolean
mock_tree_node_have_fu_device (const MockTree *node, gpointer data)
{
	return node->fu_device != NULL;
}

static void
write_controller_fw (const gchar *nvm)
{
	g_autoptr(GFile) nvm_device = NULL;
	g_autoptr(GFile) nvmem = NULL;
	g_autofree gchar *fw_path = NULL;
	g_autoptr(GFile) fw_file = NULL;
	g_autoptr(GInputStream) is = NULL;
	g_autoptr(GOutputStream) os = NULL;
	g_autoptr(GError) error = NULL;
	gssize n;

	fw_path = g_build_filename (TESTDATADIR, "thunderbolt/minimal-fw-controller.bin", NULL);
	fw_file = g_file_new_for_path (fw_path);
	g_assert_nonnull (fw_file);

	nvm_device = g_file_new_for_path (nvm);
	g_assert_nonnull (nvm_device);

	nvmem = g_file_get_child (nvm_device, "nvmem");
	g_assert_nonnull (nvmem);

	os = (GOutputStream *) g_file_append_to (nvmem,
						 G_FILE_CREATE_NONE,
						 NULL,
						 &error);
	g_assert_no_error (error);
	g_assert_nonnull (os);

	is = (GInputStream *) g_file_read (fw_file, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (is);

	n = g_output_stream_splice (os,
				    is,
				    G_OUTPUT_STREAM_SPLICE_NONE,
				    NULL,
				    &error);
	g_assert_no_error (error);
	g_assert_cmpuint (n, >, 0);
}

static gboolean
mock_tree_attach_device (gpointer user_data)
{
	MockTree *tree = (MockTree *) user_data;
	const MockDevice *dev = tree->device;
	g_autofree gchar *idstr = NULL;
	g_autofree gchar *authenticate = NULL;

	g_assert_nonnull (tree);
	g_assert_nonnull (tree->sysfs_parent);
	g_assert_nonnull (dev);

	idstr = g_strdup_printf ("%d-%d", dev->domain_id, tree->sysfs_id);
	authenticate = g_strdup_printf ("0x%x", tree->nvm_authenticate);

	tree->path = umockdev_testbed_add_device (tree->bed, "thunderbolt", idstr,
						  tree->sysfs_parent,
						  "device_name", dev->name,
						  "device", dev->id,
						  "vendor", "042",
						  "vendor_name", "GNOME.org",
						  "authorized", "0",
						  "nvm_authenticate", authenticate,
						  "nvm_version", tree->nvm_version,
						  "unique_id", tree->uuid,
						  NULL,
						  "DEVTYPE",
						  "thunderbolt_device",
						  NULL);

	tree->nvm_non_active = udev_mock_add_nvmem (tree->bed,
						    FALSE,
						    tree->path,
						    tree->sysfs_id);

	tree->nvm_active = udev_mock_add_nvmem (tree->bed,
						TRUE,
						tree->path,
						tree->sysfs_id);

	g_assert_nonnull (tree->path);
	g_assert_nonnull (tree->nvm_non_active);
	g_assert_nonnull (tree->nvm_active);

	write_controller_fw (tree->nvm_active);

	for (guint i = 0; i < tree->children->len; i++) {
		MockTree *child;

		child = g_ptr_array_index (tree->children, i);

		child->bed = g_object_ref (tree->bed);
		child->sysfs_parent = g_strdup (tree->path);

		g_timeout_add (child->device->delay_ms,
			       mock_tree_attach_device,
			       child);
	}

	return FALSE;
}

typedef struct SyncContext {
	MockTree  *tree;
	GMainLoop *loop;
} SyncContext;

static gboolean
on_sync_timeout (gpointer user_data)
{
	SyncContext *ctx = (SyncContext *) user_data;
	g_main_loop_quit (ctx->loop);
	return FALSE;
}

static void
sync_device_added (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	SyncContext *ctx = (SyncContext *) user_data;
	MockTree *tree = ctx->tree;
	const gchar *uuid = fu_device_get_physical_id (device);
	MockTree *target;

	target = (MockTree *) mock_tree_find_uuid (tree, uuid);

	if (target == NULL) {
		g_critical ("Got device that could not be matched: %s", uuid);
		return;
	}

	if (target->fu_device != NULL)
		g_object_unref (target->fu_device);

	target->fu_device = g_object_ref (device);
}

static void
sync_device_removed (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	SyncContext *ctx = (SyncContext *) user_data;
	MockTree *tree = ctx->tree;
	const gchar *uuid = fu_device_get_physical_id (device);
	MockTree *target;

	target = (MockTree *) mock_tree_find_uuid (tree, uuid);

	if (target == NULL) {
		g_warning ("Got device that could not be matched: %s", uuid);
		return;
	} else if (target->fu_device == NULL) {
		g_warning ("Got remove event for out-of-tree device %s", uuid);
		return;
	}

	g_object_unref (target->fu_device);
	target->fu_device = NULL;
}

static void
mock_tree_sync (MockTree *root, FuPlugin *plugin, int timeout_ms)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new (NULL, FALSE);
	gulong id_add;
	gulong id_del;
	SyncContext ctx = {
		.tree = root,
		.loop = mainloop,
	};

	id_add = g_signal_connect (plugin, "device-added",
				   G_CALLBACK (sync_device_added),
				   &ctx);

	id_del = g_signal_connect (plugin, "device-removed",
				   G_CALLBACK (sync_device_removed),
				   &ctx);

	if (timeout_ms > 0)
		g_timeout_add (timeout_ms, on_sync_timeout, &ctx);

	g_main_loop_run (mainloop);

	g_signal_handler_disconnect (plugin, id_add);
	g_signal_handler_disconnect (plugin, id_del);
}


typedef struct AttachContext {
	/* in */
	MockTree  *tree;
	GMainLoop *loop;
	/* out */
	gboolean   complete;

} AttachContext;

static void
mock_tree_plugin_device_added (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	AttachContext *ctx = (AttachContext *) user_data;
	MockTree *tree = ctx->tree;
	const gchar *uuid = fu_device_get_physical_id (device);
	MockTree *target;

	target = (MockTree *) mock_tree_find_uuid (tree, uuid);

	if (target == NULL) {
		g_warning ("Got device that could not be matched: %s", uuid);
		return;
	}

	g_set_object (&target->fu_device, device);

	if (mock_tree_all (tree, mock_tree_node_have_fu_device, NULL)) {
		ctx->complete = TRUE;
		g_main_loop_quit (ctx->loop);
	}
}

static gboolean
mock_tree_settle (MockTree *root, FuPlugin *plugin)
{
	g_autoptr(GMainLoop) mainloop = g_main_loop_new (NULL, FALSE);
	gulong id;
	AttachContext ctx = {
		.tree = root,
		.loop = mainloop,
	};

	id = g_signal_connect (plugin, "device-added",
			       G_CALLBACK (mock_tree_plugin_device_added),
			       &ctx);

	g_main_loop_run (mainloop);
	g_signal_handler_disconnect (plugin, id);

	return ctx.complete;
}

static gboolean
mock_tree_attach (MockTree *root, UMockdevTestbed *bed, FuPlugin *plugin)
{
	root->bed = g_object_ref (bed);
	root->sysfs_parent = udev_mock_add_domain (bed, root->device->domain_id);
	g_assert_nonnull (root->sysfs_parent);

	g_timeout_add (root->device->delay_ms, mock_tree_attach_device, root);

	return mock_tree_settle (root, plugin);
}

/* the unused parameter makes the function signature compatible
 * with 'MockTreePredicate' */
static gboolean
mock_tree_node_is_detached (const MockTree *node, gpointer unused)
{
	gboolean ret = node->path == NULL;

	/* consistency checks: if ret, make sure we are
	 * fully detached */
	if (ret) {
		g_assert_null (node->nvm_active);
		g_assert_null (node->nvm_non_active);
		g_assert_null (node->bed);
	} else {
		g_assert_nonnull (node->nvm_active);
		g_assert_nonnull (node->nvm_non_active);
		g_assert_nonnull (node->bed);
	}

	return ret;
}

static void
mock_tree_detach (MockTree *node)
{
	UMockdevTestbed *bed;

	if (mock_tree_node_is_detached (node, NULL))
		return;

	for (guint i = 0; i < node->children->len; i++) {
		MockTree *child = g_ptr_array_index (node->children, i);
		mock_tree_detach (child);
		g_free (child->sysfs_parent);
		child->sysfs_parent = NULL;
	}

	bed  = node->bed;
	umockdev_testbed_uevent (bed, node->nvm_active, "remove");
	umockdev_testbed_remove_device (bed, node->nvm_active);

	umockdev_testbed_uevent (bed, node->nvm_non_active, "remove");
	umockdev_testbed_remove_device (bed, node->nvm_non_active);

	umockdev_testbed_uevent (bed, node->path, "remove");
	umockdev_testbed_remove_device (bed, node->path);

	g_free (node->path);
	g_free (node->nvm_non_active);
	g_free (node->nvm_active);

	node->path = NULL;
	node->nvm_non_active = NULL;
	node->nvm_active = NULL;

	g_object_unref (bed);
	node->bed = NULL;
}

typedef enum UpdateResult {
	UPDATE_SUCCESS     = 0,
	/* nvm_authenticate will report error condition */
	UPDATE_FAIL_DEVICE_INTERNAL = 1,
	/* device to be updated will NOT re-appear */
	UPDATE_FAIL_DEVICE_NOSHOW   = 2
} UpdateResult;

typedef struct UpdateContext {
	GFileMonitor *monitor;

	UpdateResult result;
	guint timeout;
	GBytes *data;
	UMockdevTestbed *bed;
	FuPlugin *plugin;

	MockTree *node;
	gchar *version;
} UpdateContext;

static void
update_context_free (UpdateContext *ctx)
{
	if (ctx == NULL)
		return;

	g_object_unref (ctx->bed);
	g_object_unref (ctx->plugin);
	g_object_unref (ctx->monitor);
	g_bytes_unref (ctx->data);
	g_free (ctx->version);
	g_free (ctx);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC (UpdateContext, update_context_free);
#pragma clang diagnostic pop

static gboolean
reattach_tree (gpointer user_data)
{
	UpdateContext *ctx = (UpdateContext *) user_data;
	MockTree *node = ctx->node;

	g_debug ("Mock update done, reattaching tree...");

	node->bed = g_object_ref (ctx->bed);
	g_timeout_add (node->device->delay_ms, mock_tree_attach_device, node);

	return FALSE;
}

static void
udev_file_changed_cb (GFileMonitor     *monitor,
		      GFile            *file,
		      GFile            *other_file,
		      GFileMonitorEvent event_type,
		      gpointer          user_data)
{
	UpdateContext *ctx = (UpdateContext *) user_data;
	gboolean ok;
	gsize len;
	g_autofree gchar *data = NULL;
	g_autoptr(GError) error = NULL;

	g_debug ("Got update trigger");
	ok = g_file_monitor_cancel (monitor);
	g_assert_true (ok);

	ok = g_file_load_contents (file, NULL, &data, &len, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (ok);

	if (!g_str_has_prefix (data, "1"))
		return;

	/* verify the firmware is correct */
	mock_tree_firmware_verify (ctx->node, ctx->data);

	g_debug ("Removing tree below and including: %s", ctx->node->path);
	mock_tree_detach (ctx->node);

	ctx->node->nvm_authenticate = (guint) ctx->result;

	/* update the version only on "success" simulations */
	if (ctx->result == UPDATE_SUCCESS) {
		g_free (ctx->node->nvm_version);
		ctx->node->nvm_version = g_strdup (ctx->version);
	}

	g_debug ("Simulating update to '%s' with result: 0x%x",
		 ctx->version, ctx->node->nvm_authenticate);

	if (ctx->result == UPDATE_FAIL_DEVICE_NOSHOW) {
		g_debug ("Simulating no-show fail:"
			 " device tree will not reappear");
		return;
	}

	g_debug ("Device tree reattachment in %3.2f seconds",
		 ctx->timeout / 1000.0);
	g_timeout_add (ctx->timeout, reattach_tree, ctx);
}

static UpdateContext *
mock_tree_prepare_for_update (MockTree    *node,
			      FuPlugin    *plugin,
			      const char  *version,
			      GBytes      *fw_data,
			      guint        timeout_ms)
{
	UpdateContext *ctx;
	g_autoptr(GFile) dir = NULL;
	g_autoptr(GFile) f = NULL;
	g_autoptr(GError) error = NULL;
	GFileMonitor *monitor;

	ctx = g_new0 (UpdateContext, 1);
	dir = g_file_new_for_path (node->path);
	f = g_file_get_child (dir, "nvm_authenticate");

	monitor = g_file_monitor_file (f, G_FILE_MONITOR_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (monitor);

	ctx->node = node;
	ctx->plugin = g_object_ref (plugin);
	ctx->bed = g_object_ref (node->bed);
	ctx->timeout = timeout_ms;
	ctx->monitor = monitor;
	ctx->version = g_strdup (version);
	ctx->data = g_bytes_ref (fw_data);

	g_signal_connect (monitor, "changed",
			  G_CALLBACK (udev_file_changed_cb), ctx);

	return ctx;
}

static MockDevice root_one = {

	.name = "Laptop",
	.id = "0x23",
	.nvm_version = "20.2",
	.nvm_parsed_version = "20.02",

	.children = (MockDevice[]) {
		{
			.name = "Thunderbolt Cable",
			.id = "0x24",
			.nvm_version = "20.0",
			.nvm_parsed_version = "20.00",

			.children = (MockDevice[]) {
				{
					.name = "Thunderbolt Dock",
					.id = "0x25",
					.nvm_version = "10.0",
					.nvm_parsed_version = "10.00",
				},
				{ NULL, }

			},
		}, {
			.name = "Thunderbolt Cable",
			.id = "0x24",
			.nvm_version = "23.0",
			.nvm_parsed_version = "23.00",

			.children = (MockDevice[]) {
				{
					.name = "Thunderbolt SSD",
					.id = "0x26",

					.nvm_version = "5.0",
					.nvm_parsed_version = "05.00",
				},
				{ NULL, }
			},
		},
		{ NULL, },
	},

};


typedef struct TestParam {
	gboolean initialize_tree;
	gboolean attach_and_coldplug;

	const char *firmware_file;
} TestParam;

typedef enum TestFlags {
	TEST_INITIALIZE_TREE     = 1 << 0,
	TEST_ATTACH_AND_COLDPLUG = 1 << 1,
	TEST_PREPARE_FIRMWARE    = 1 << 2,

	TEST_PREPARE_ALL = TEST_INITIALIZE_TREE |
	                   TEST_ATTACH_AND_COLDPLUG |
	                   TEST_PREPARE_FIRMWARE
} TestFlags;

#define TEST_INIT_FULL (GUINT_TO_POINTER (TEST_PREPARE_ALL))
#define TEST_INIT_NONE (GUINT_TO_POINTER (0))

typedef struct ThunderboltTest {
	UMockdevTestbed *bed;
	FuPlugin *plugin;

	/* if TestParam::initialize_tree */
	MockTree *tree;

	/* if TestParam::firmware_file is nonnull */
	GMappedFile *fw_file;
	GBytes      *fw_data;

} ThunderboltTest;

static void
test_set_up (ThunderboltTest *tt, gconstpointer params)
{
	TestFlags flags = GPOINTER_TO_UINT(params);
	gboolean ret;
	g_autofree gchar *pluginfn = NULL;
	g_autofree gchar *sysfs = NULL;
	g_autoptr(GError) error = NULL;

	tt->bed = umockdev_testbed_new ();
	g_assert_nonnull (tt->bed);

	sysfs = umockdev_testbed_get_sys_dir (tt->bed);
	g_debug ("mock sysfs at %s", sysfs);

	tt->plugin = fu_plugin_new ();
	g_assert_nonnull (tt->plugin);

	pluginfn = g_build_filename (PLUGINBUILDDIR,
				     "libfu_plugin_thunderbolt." G_MODULE_SUFFIX,
				     NULL);
	ret = fu_plugin_open (tt->plugin, pluginfn, &error);

	g_assert_no_error (error);
	g_assert_true (ret);

	ret = fu_plugin_runner_startup (tt->plugin, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	if (flags & TEST_INITIALIZE_TREE) {
		tt->tree = mock_tree_init (&root_one);
		g_assert_nonnull (tt->tree);
	}

	if (!umockdev_in_mock_environment ()) {
		g_warning ("Need to run with umockdev-wrapper");
		return;
	}

	if (flags & TEST_ATTACH_AND_COLDPLUG) {
		g_assert_true (flags & TEST_INITIALIZE_TREE);

		ret = fu_plugin_runner_coldplug (tt->plugin, &error);
		g_assert_no_error (error);
		g_assert_true (ret);

		ret = mock_tree_attach (tt->tree, tt->bed, tt->plugin);
		g_assert_true (ret);
	}

	if (flags & TEST_PREPARE_FIRMWARE) {
		g_autofree gchar *fw_path = NULL;

		fw_path = g_build_filename (TESTDATADIR, "thunderbolt/minimal-fw.bin", NULL);
		tt->fw_file = g_mapped_file_new (fw_path, FALSE, &error);
		g_assert_no_error (error);
		g_assert_nonnull (tt->fw_file);

		tt->fw_data = g_mapped_file_get_bytes (tt->fw_file);
		g_assert_nonnull (tt->fw_data);
	}
}

static void
test_tear_down (ThunderboltTest *tt, gconstpointer user_data)
{
	g_object_unref (tt->plugin);
	g_object_unref (tt->bed);

	if (tt->tree)
		mock_tree_free (tt->tree);

	if (tt->fw_data)
		g_bytes_unref (tt->fw_data);

	if (tt->fw_file)
		g_mapped_file_unref (tt->fw_file);
}



static gboolean
test_tree_uuids (const MockTree *node, gpointer data)
{
	const MockTree *root = (MockTree *) data;
	const gchar *uuid = node->uuid;
	const MockTree *found;

	g_assert_nonnull (uuid);

	g_debug ("Looking for %s", uuid);

	found = mock_tree_find_uuid (root, uuid);
	g_assert_nonnull (found);
	g_assert_cmpstr (node->uuid, ==, found->uuid);

	/* return false so we traverse the whole tree */
	return FALSE;
}

static void
test_tree (ThunderboltTest *tt, gconstpointer user_data)
{
	const MockTree *found;
	gboolean ret;
	g_autoptr(MockTree) tree = NULL;
	g_autoptr(GError) error = NULL;

	tree = mock_tree_init (&root_one);
	g_assert_nonnull (tree);

	mock_tree_dump (tree, 0);

	(void) mock_tree_contains (tree, test_tree_uuids, tree);

	found = mock_tree_find_uuid (tree, "nonexistentuuid");
	g_assert_null (found);

	ret = fu_plugin_runner_coldplug (tt->plugin, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	ret = mock_tree_attach (tree, tt->bed, tt->plugin);
	g_assert_true (ret);

	mock_tree_detach (tree);
	ret = mock_tree_all (tree, mock_tree_node_is_detached, NULL);
	g_assert_true (ret);
}

static void
test_image_validation (ThunderboltTest *tt, gconstpointer user_data)
{
	FuPluginValidation val;
	g_autofree gchar *ctl_path = NULL;
	g_autofree gchar *fwi_path = NULL;
	g_autofree gchar *bad_path = NULL;
	g_autoptr(GMappedFile) fwi_file = NULL;
	g_autoptr(GMappedFile) ctl_file = NULL;
	g_autoptr(GMappedFile) bad_file = NULL;
	g_autoptr(GBytes)      fwi_data = NULL;
	g_autoptr(GBytes)      ctl_data = NULL;
	g_autoptr(GBytes)      bad_data = NULL;
	g_autoptr(GError)      error = NULL;

	/* image as if read from the controller (i.e. no headers) */
	ctl_path = g_build_filename (TESTDATADIR,
					 "thunderbolt/minimal-fw-controller.bin", NULL);
	ctl_file = g_mapped_file_new (ctl_path, FALSE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (ctl_file);

	ctl_data = g_mapped_file_get_bytes (ctl_file);
	g_assert_nonnull (ctl_data);

	/* valid firmware update image */
	fwi_path = g_build_filename (TESTDATADIR, "thunderbolt/minimal-fw.bin", NULL);
	fwi_file = g_mapped_file_new (fwi_path, FALSE, &error);
 	g_assert_no_error (error);
	g_assert_nonnull (fwi_file);

	fwi_data = g_mapped_file_get_bytes (fwi_file);
	g_assert_nonnull (fwi_data);

	/* a wrong/bad firmware update image */
	bad_path = g_build_filename (TESTDATADIR, "colorhug/firmware.bin", NULL);
	bad_file = g_mapped_file_new (bad_path, FALSE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (bad_file);

	bad_data = g_mapped_file_get_bytes (bad_file);
	g_assert_nonnull (bad_data);

	/* now for some testing ... this should work */
	val = fu_thunderbolt_image_validate (ctl_data, fwi_data, &error);
	g_assert_no_error (error);
	g_assert_cmpint (val, ==, VALIDATION_PASSED);


	/* these all should fail */
	/*  valid controller, bad update data */
	val = fu_thunderbolt_image_validate (ctl_data, ctl_data, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_cmpint (val, ==, VALIDATION_FAILED);
	g_debug ("expected image validation error [ctl, ctl]: %s", error->message);
	g_clear_error (&error);

	val = fu_thunderbolt_image_validate (ctl_data, bad_data, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_cmpint (val, ==, VALIDATION_FAILED);
	g_debug ("expected image validation error [ctl, bad]: %s", error->message);
	g_clear_error (&error);

	/* bad controller data, valid update data */
	val = fu_thunderbolt_image_validate (fwi_data, fwi_data, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_cmpint (val, ==, VALIDATION_FAILED);
	g_debug ("expected image validation error [fwi, fwi]: %s", error->message);
	g_clear_error (&error);

	val = fu_thunderbolt_image_validate (bad_data, fwi_data, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_cmpint (val, ==, VALIDATION_FAILED);
	g_debug ("expected image validation error [bad, fwi]: %s", error->message);
	g_clear_error (&error);

	/* both bad */
	val = fu_thunderbolt_image_validate (bad_data, bad_data, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_cmpint (val, ==, VALIDATION_FAILED);
	g_debug ("expected image validation error [bad, bad]: %s", error->message);
	g_clear_error (&error);
}

static void
test_change_uevent (ThunderboltTest *tt, gconstpointer user_data)
{
	FuPlugin *plugin = tt->plugin;
	MockTree *tree = tt->tree;
	gboolean ret;
	const gchar *version_after;

	/* test sanity check */
	g_assert_nonnull (tree);

	/* simulate change of version via a change even, i.e.
	 * without add, remove. */
	umockdev_testbed_set_attribute (tt->bed, tree->path, "nvm_version", "42.23");
	umockdev_testbed_uevent (tt->bed, tree->path, "change");

	/* we just "wait" for 500ms, should be enough */
	mock_tree_sync (tree, plugin, 500);

	/* the tree should not have changed */
	ret = mock_tree_all (tree, mock_tree_node_have_fu_device, NULL);
	g_assert_true (ret);

	/* we should have the version change in the FuDevice */
	version_after = fu_device_get_version (tree->fu_device);
	g_assert_cmpstr (version_after, ==, "42.23");
}

static void
test_update_working (ThunderboltTest *tt, gconstpointer user_data)
{
	FuPlugin *plugin = tt->plugin;
	MockTree *tree = tt->tree;
	GBytes *fw_data = tt->fw_data;
	gboolean ret;
	const gchar *version_after;
	g_autoptr(GError) error = NULL;
	g_autoptr(UpdateContext) up_ctx = NULL;

	/* test sanity check */
	g_assert_nonnull (tree);
	g_assert_nonnull (fw_data);

	/* simulate an update, where the device goes away and comes back
	 * after the time in the last parameter (given in ms) */
	up_ctx = mock_tree_prepare_for_update (tree, plugin, "42.23", fw_data, 1000);
	ret = fu_plugin_runner_update (plugin, tree->fu_device, fw_data, 0, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* we wait until the plugin has picked up  all the
	 * subtree changes */
	ret = mock_tree_settle (tree, plugin);
	g_assert_true (ret);

	ret = fu_plugin_runner_update_attach (plugin, tree->fu_device, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	version_after = fu_device_get_version (tree->fu_device);
	g_debug ("version after update: %s", version_after);
	g_assert_cmpstr (version_after, ==, "42.23");

	/* make sure all pending events have happened */
	ret = mock_tree_settle (tree, plugin);
	g_assert_true (ret);

	/* now we check if the every tree node has a corresponding FuDevice,
	 * this implicitly checks that we are handling uevents correctly
	 * after the event, and that we are in sync with the udev tree */
	ret = mock_tree_all (tree, mock_tree_node_have_fu_device, NULL);
	g_assert_true (ret);
}

static void
test_update_fail (ThunderboltTest *tt, gconstpointer user_data)
{
	FuPlugin *plugin = tt->plugin;
	MockTree *tree = tt->tree;
	GBytes *fw_data = tt->fw_data;
	gboolean ret;
	const gchar *version_after;
	g_autoptr(GError) error = NULL;
	g_autoptr(UpdateContext) up_ctx = NULL;

	/* test sanity check */
	g_assert_nonnull (tree);
	g_assert_nonnull (fw_data);

	/* simulate an update, as in test_update_working,
	 * but simulate an error indicated by the device
	 */
	up_ctx = mock_tree_prepare_for_update (tree, plugin, "42.23", fw_data, 1000);
	up_ctx->result = UPDATE_FAIL_DEVICE_INTERNAL;

	ret = fu_plugin_runner_update (plugin, tree->fu_device, fw_data, 0, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* we wait until the plugin has picked up all the
	 * subtree changes, and make sure we still receive
	 * udev updates correctly and are in sync */
	ret = mock_tree_settle (tree, plugin);
	g_assert_true (ret);

	ret = fu_plugin_runner_update_attach (plugin, tree->fu_device, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_false (ret);

	/* make sure all pending events have happened */
	ret = mock_tree_settle (tree, plugin);
	g_assert_true (ret);

	/* version should *not* have changed (but we get parsed version) */
	version_after = fu_device_get_version (tree->fu_device);
	g_debug ("version after update: %s", version_after);
	g_assert_cmpstr (version_after, ==, tree->device->nvm_parsed_version);

	ret = mock_tree_all (tree, mock_tree_node_have_fu_device, NULL);
	g_assert_true (ret);
}


static void
test_update_fail_nowshow (ThunderboltTest *tt, gconstpointer user_data)
{
	FuPlugin *plugin = tt->plugin;
	MockTree *tree = tt->tree;
	GBytes *fw_data = tt->fw_data;
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(UpdateContext) up_ctx = NULL;

	/* test sanity check */
	g_assert_nonnull (tree);
	g_assert_nonnull (fw_data);

	/* simulate an update, as in test_update_working,
	 * but simulate an error indicated by the device
	 */
	up_ctx = mock_tree_prepare_for_update (tree, plugin, "42.23", fw_data, 1000);
	up_ctx->result = UPDATE_FAIL_DEVICE_NOSHOW;

	ret = fu_plugin_runner_update (plugin, tree->fu_device, fw_data, 0, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	mock_tree_sync (tree, plugin, 500);

	ret = mock_tree_all (tree, mock_tree_node_have_fu_device, NULL);
	g_assert_false (ret);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);


	g_test_add ("/thunderbolt/basic",
		    ThunderboltTest,
		    NULL,
		    test_set_up,
		    test_tree,
		    test_tear_down);

	g_test_add ("/thunderbolt/image-validation",
		    ThunderboltTest,
		    TEST_INIT_NONE,
		    test_set_up,
		    test_image_validation,
		    test_tear_down);

	g_test_add ("/thunderbolt/change-uevent",
		    ThunderboltTest,
		    GUINT_TO_POINTER (TEST_INITIALIZE_TREE |
				      TEST_ATTACH_AND_COLDPLUG),
		    test_set_up,
		    test_change_uevent,
		    test_tear_down);

	g_test_add ("/thunderbolt/update{working}",
		    ThunderboltTest,
		    TEST_INIT_FULL,
		    test_set_up,
		    test_update_working,
		    test_tear_down);

	g_test_add ("/thunderbolt/update{failing}",
		    ThunderboltTest,
		    TEST_INIT_FULL,
		    test_set_up,
		    test_update_fail,
		    test_tear_down);

	g_test_add ("/thunderbolt/update{failing-noshow}",
		    ThunderboltTest,
		    TEST_INIT_FULL,
		    test_set_up,
		    test_update_fail_nowshow,
		    test_tear_down);

	return g_test_run ();
}
