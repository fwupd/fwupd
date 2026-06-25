/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-mok-common.h"
#include "fu-uefi-mok-struct.h"

static FuUefiMokHsiKey
fu_uefi_mok_attr_parse(GBytes *blob)
{
	FuUefiMokHsiKey key_all = FU_UEFI_MOK_HSI_KEY_NONE;
	g_auto(GStrv) lines = fu_strsplit_bytes(blob, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		g_auto(GStrv) kv = NULL;
		if (lines[i][0] == '\0')
			continue;
		kv = g_strsplit(lines[i], ": ", -1);
		if (g_strv_length(kv) != 2)
			continue;
		if (g_strcmp0(kv[1], "1") == 0)
			key_all |= fu_uefi_mok_hsi_key_from_string(kv[0]);
	}
	return key_all;
}

FwupdSecurityAttr *
fu_uefi_mok_attr_nx_new(FuPlugin *plugin, GBytes *blob)
{
	FuUefiMokHsiKey key_all;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_UEFI_NX_COMPAT);
	fwupd_security_attr_set_plugin(attr, "uefi_mok");
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_OS);

	/* is this valid? */
	key_all = fu_uefi_mok_attr_parse(blob);
	if ((key_all & FU_UEFI_MOK_HSI_KEY_SHIM_HAS_NX_COMPAT_SET) == 0) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	} else {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	}

	/* success */
	return g_steal_pointer(&attr);
}

FwupdSecurityAttr *
fu_uefi_mok_attr_fw_new(FuPlugin *plugin, GBytes *blob)
{
	FuUefiMokHsiKey key_all;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_UEFI_MEMORY_PROTECTION);
	fwupd_security_attr_set_plugin(attr, "uefi_mok");
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);

	/* is this valid? */
	key_all = fu_uefi_mok_attr_parse(blob);
	if ((key_all & FU_UEFI_MOK_HSI_KEY_HEAP_IS_EXECUTABLE) > 0 ||
	    (key_all & FU_UEFI_MOK_HSI_KEY_STACK_IS_EXECUTABLE) > 0 ||
	    (key_all & FU_UEFI_MOK_HSI_KEY_RO_SECTIONS_ARE_WRITABLE) > 0) {
		/* the firmware is being dumb */
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
	} else {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	}

	/* success */
	return g_steal_pointer(&attr);
}
