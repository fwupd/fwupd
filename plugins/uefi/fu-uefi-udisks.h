/*
 * Copyright (C) 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

GPtrArray	*fu_uefi_udisks_get_block_devices	(GError		**error);
gboolean	 fu_uefi_udisks_objpath			(const gchar	*path);
gboolean	 fu_uefi_udisks_objpath_is_esp		(const gchar	*obj);
gchar		*fu_uefi_udisks_objpath_mount		(const gchar	*path,
							 GError		**error);
gboolean	fu_uefi_udisks_objpath_umount		(const gchar	*path,
							 GError		**error);
gchar		*fu_uefi_udisks_objpath_is_mounted	(const gchar	*path);
