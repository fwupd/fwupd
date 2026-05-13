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

#define EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED 0x0000000000000004ULL

gchar *
fu_uefi_capsule_build_app_basename(FuPathStore *pstore, const gchar *cmd, GError **error);
gchar *
fu_uefi_get_built_app_path(FuPathStore *pstore,
			   FuEfivars *efivars,
			   const gchar *basename,
			   GError **error);
gboolean
fu_uefi_get_framebuffer_size(FuPathStore *pstore, guint32 *width, guint32 *height, GError **error);
guint64
fu_uefi_read_file_as_uint64(const gchar *path, const gchar *attr_name);
gboolean
fu_uefi_esp_target_verify(const gchar *fn_src, const gchar *fn_dst);
gboolean
fu_uefi_esp_target_copy(const gchar *fn_src, const gchar *fn_dst, GError **error);
