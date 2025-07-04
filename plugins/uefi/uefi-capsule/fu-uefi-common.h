/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#ifdef HAVE_EFI_TIME_T
#include <efivar/efivar.h>
#endif

#define EFI_CAPSULE_HEADER_FLAGS_PERSIST_ACROSS_RESET  0x00010000
#define EFI_CAPSULE_HEADER_FLAGS_POPULATE_SYSTEM_TABLE 0x00020000
#define EFI_CAPSULE_HEADER_FLAGS_INITIATE_RESET	       0x00040000

#define EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED 0x0000000000000004ULL

gchar *
fu_uefi_get_esp_app_path(const gchar *base, const gchar *cmd, GError **error);
gchar *
fu_uefi_get_built_app_path(FuEfivars *efivars, const gchar *binary, GError **error);
gboolean
fu_uefi_get_framebuffer_size(guint32 *width, guint32 *height, GError **error);
gchar *
fu_uefi_get_esp_path_for_os(const gchar *base);
guint64
fu_uefi_read_file_as_uint64(const gchar *path, const gchar *attr_name);
gboolean
fu_uefi_esp_target_exists(FuVolume *esp, const gchar *target_no_mountpoint);
gboolean
fu_uefi_esp_target_verify(const gchar *source_fn, FuVolume *esp, const gchar *target_no_mountpoint);
gboolean
fu_uefi_esp_target_copy(const gchar *source_fn,
			FuVolume *esp,
			const gchar *target_no_mountpoint,
			GError **error);
