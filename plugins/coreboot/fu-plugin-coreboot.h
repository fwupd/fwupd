/*
 * Copyright (C) 2019 9elements Agency GmbH <patrick.rudolph@9elements.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#include "fu-plugin.h"
#include "fu-device.h"

struct fmap;

struct fmap *
fu_plugin_coreboot_find_fmap (FuPlugin *plugin, GError **error);

gboolean
fu_plugin_coreboot_add_fmap_devices (FuPlugin *plugin, GError **error, FuDevice *parent, const struct fmap *fmap);

const gchar*
fu_plugin_coreboot_get_name_for_type (FuPlugin *plugin, const gchar *vboot_partition);

const struct cbfs_header *
fu_plugin_coreboot_find_cbfs_master (const void *mem, const guint region_size);

const struct cbfs_file *
fu_plugin_coreboot_find_cbfs (const void *mem, const guint region_size);

struct cbfs_file *
fu_plugin_coreboot_find_cbfs_file (const void *mem, const guint region_size, const gchar *name);

const guint8 *
fu_plugin_coreboot_get_raw_cbfs_file (const void *mem, const guint region_size, const gchar *name);

const gchar *
fu_plugin_coreboot_version_string_to_triplet (const gchar *coreboot_version, GError **error);

const gchar *
fu_plugin_coreboot_parse_revision_file (const gchar *file, GError **error);
