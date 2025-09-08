/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#define FU_TYPE_VOLUME (fu_volume_get_type())

G_DECLARE_FINAL_TYPE(FuVolume, fu_volume, FU, VOLUME, GObject)

/**
 * FU_VOLUME_KIND_ESP:
 *
 * The GUID for the ESP, see: https://en.wikipedia.org/wiki/EFI_system_partition
 *
 * Since: 1.5.0
 **/
#define FU_VOLUME_KIND_ESP "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"
/**
 * FU_VOLUME_KIND_BDP:
 *
 * The GUID for the BDP, see: https://en.wikipedia.org/wiki/Microsoft_basic_data_partition
 *
 * Since: 1.5.3
 **/
#define FU_VOLUME_KIND_BDP "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"

const gchar *
fu_volume_get_id(FuVolume *self) G_GNUC_NON_NULL(1);
gboolean
fu_volume_check_free_space(FuVolume *self,
			   guint64 required,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_volume_is_mounted(FuVolume *self) G_GNUC_NON_NULL(1);
gboolean
fu_volume_is_encrypted(FuVolume *self) G_GNUC_NON_NULL(1);
guint64
fu_volume_get_size(FuVolume *self) G_GNUC_NON_NULL(1);
gchar *
fu_volume_get_block_name(FuVolume *self) G_GNUC_NON_NULL(1);
gsize
fu_volume_get_block_size(FuVolume *self, GError **error) G_GNUC_NON_NULL(1);
gchar *
fu_volume_get_partition_kind(FuVolume *self) G_GNUC_NON_NULL(1);
gchar *
fu_volume_get_partition_name(FuVolume *self) G_GNUC_NON_NULL(1);
guint64
fu_volume_get_partition_size(FuVolume *self) G_GNUC_NON_NULL(1);
guint64
fu_volume_get_partition_offset(FuVolume *self) G_GNUC_NON_NULL(1);
guint32
fu_volume_get_partition_number(FuVolume *self) G_GNUC_NON_NULL(1);
gchar *
fu_volume_get_partition_uuid(FuVolume *self) G_GNUC_NON_NULL(1);
gchar *
fu_volume_get_mount_point(FuVolume *self) G_GNUC_NON_NULL(1);
gboolean
fu_volume_mount(FuVolume *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_volume_unmount(FuVolume *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_volume_is_internal(FuVolume *self) G_GNUC_NON_NULL(1);
gchar *
fu_volume_get_id_type(FuVolume *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_volume_new_by_kind(const gchar *kind, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
FuVolume *
fu_volume_new_by_device(const gchar *device, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
FuVolume *
fu_volume_new_by_devnum(guint32 devnum, GError **error) G_GNUC_WARN_UNUSED_RESULT;
const gchar *
fu_volume_kind_convert_to_gpt(const gchar *kind) G_GNUC_NON_NULL(1);
gboolean
fu_volume_is_mdraid(FuVolume *self) G_GNUC_NON_NULL(1);
