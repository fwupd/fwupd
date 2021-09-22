/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "fu-security-attr.h"

#include <config.h>
#include <glib/gi18n.h>
#include <json-glib/json-glib.h>

#include "fwupd-enums-private.h"
#include "fwupd-security-attr-private.h"

#include "fu-security-attrs-private.h"

gchar *
fu_security_attr_get_name(FwupdSecurityAttr *attr)
{
	const gchar *appstream_id = fwupd_security_attr_get_appstream_id(attr);
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return g_strdup(_("SPI write"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_BLE) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return g_strdup(_("SPI lock"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return g_strdup(_("SPI BIOS region"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR) == 0) {
		/* TRANSLATORS: Title: SPI refers to the flash chip in the computer */
		return g_strdup(_("SPI BIOS Descriptor"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_ACPI_DMAR) == 0) {
		/* TRANSLATORS: Title: DMA as in https://en.wikipedia.org/wiki/DMA_attack  */
		return g_strdup(_("Pre-boot DMA protection"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel */
		return g_strdup(_("Intel BootGuard"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * verified boot refers to the way the boot process is verified */
		return g_strdup(_("Intel BootGuard verified boot"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * ACM means to verify the integrity of Initial Boot Block */
		return g_strdup(_("Intel BootGuard ACM protected"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * error policy is what to do on failure */
		return g_strdup(_("Intel BootGuard error policy"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP) == 0) {
		/* TRANSLATORS: Title: BootGuard is a trademark from Intel,
		 * OTP = one time programmable */
		return g_strdup(_("Intel BootGuard OTP fuse"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED) == 0) {
		/* TRANSLATORS: Title: CET = Control-flow Enforcement Technology,
		 * enabled means supported by the processor */
		return g_strdup(_("Intel CET Enabled"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE) == 0) {
		/* TRANSLATORS: Title: CET = Control-flow Enforcement Technology,
		 * active means being used by the OS */
		return g_strdup(_("Intel CET Active"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_SMAP) == 0) {
		/* TRANSLATORS: Title: SMAP = Supervisor Mode Access Prevention */
		return g_strdup(_("Intel SMAP"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM) == 0) {
		/* TRANSLATORS: Title: Memory contents are encrypted, e.g. Intel TME */
		return g_strdup(_("Encrypted RAM"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_IOMMU) == 0) {
		/* TRANSLATORS: Title:
		 * https://en.wikipedia.org/wiki/Input%E2%80%93output_memory_management_unit */
		return g_strdup(_("IOMMU"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN) == 0) {
		/* TRANSLATORS: Title: lockdown is a security mode of the kernel */
		return g_strdup(_("Linux kernel lockdown"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED) == 0) {
		/* TRANSLATORS: Title: if it's tainted or not */
		return g_strdup(_("Linux kernel"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP) == 0) {
		/* TRANSLATORS: Title: swap space or swap partition */
		return g_strdup(_("Linux swap"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM) == 0) {
		/* TRANSLATORS: Title: sleep state */
		return g_strdup(_("Suspend-to-ram"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE) == 0) {
		/* TRANSLATORS: Title: a better sleep state */
		return g_strdup(_("Suspend-to-idle"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_PK) == 0) {
		/* TRANSLATORS: Title: PK is the 'platform key' for the machine */
		return g_strdup(_("UEFI platform key"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT) == 0) {
		/* TRANSLATORS: Title: SB is a way of locking down UEFI */
		return g_strdup(_("UEFI secure boot"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0) == 0) {
		/* TRANSLATORS: Title: the PCR is rebuilt from the TPM event log */
		return g_strdup(_("TPM PCR0 reconstruction"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20) == 0) {
		/* TRANSLATORS: Title: TPM = Trusted Platform Module */
		return g_strdup(_("TPM v2.0"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE) == 0) {
		const gchar *kind = fwupd_security_attr_get_metadata(attr, "kind");
		if (kind != NULL) {
			/* TRANSLATORS: Title: %s is ME kind, e.g. CSME/TXT */
			return g_strdup_printf(_("%s manufacturing mode"), kind);
		}
		/* TRANSLATORS: Title: MEI = Intel Management Engine */
		return g_strdup(_("MEI manufacturing mode"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP) == 0) {
		const gchar *kind = fwupd_security_attr_get_metadata(attr, "kind");
		if (kind != NULL) {
			/* TRANSLATORS: Title: %s is ME kind, e.g. CSME/TXT */
			return g_strdup_printf(_("%s override"), kind);
		}
		/* TRANSLATORS: Title: MEI = Intel Management Engine, and the
		 * "override" is the physical PIN that can be driven to
		 * logic high -- luckily it is probably not accessible to
		 * end users on consumer boards */
		return g_strdup(_("MEI override"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_MEI_VERSION) == 0) {
		/* TRANSLATORS: Title: MEI = Intel Management Engine */
		const gchar *kind = fwupd_security_attr_get_metadata(attr, "kind");
		const gchar *version = fwupd_security_attr_get_metadata(attr, "version");
		if (kind != NULL && version != NULL) {
			/* TRANSLATORS: Title: %1 is ME kind, e.g. CSME/TXT, %2 is a version number
			 */
			return g_strdup_printf(_("%s v%s"), kind, version);
		}
		if (kind != NULL) {
			/* TRANSLATORS: Title: %s is ME kind, e.g. CSME/TXT */
			return g_strdup_printf(_("%s version"), kind);
		}
		return g_strdup(_("MEI version"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES) == 0) {
		/* TRANSLATORS: Title: if firmware updates are available */
		return g_strdup(_("Firmware updates"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_ATTESTATION) == 0) {
		/* TRANSLATORS: Title: if we can verify the firmware checksums */
		return g_strdup(_("Firmware attestation"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS) == 0) {
		/* TRANSLATORS: Title: if the fwupd plugins are all present and correct */
		return g_strdup(_("fwupd plugins"));
	}
	if (g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_DCI_ENABLED) == 0 ||
	    g_strcmp0(appstream_id, FWUPD_SECURITY_ATTR_ID_INTEL_DCI_LOCKED) == 0) {
		/* TRANSLATORS: Title: Direct Connect Interface (DCI) allows
		 * debugging of Intel processors using the USB3 port */
		return g_strdup(_("Intel DCI debugger"));
	}

	/* we should not get here */
	return g_strdup(fwupd_security_attr_get_name(attr));
}

const gchar *
fu_security_attr_get_result(FwupdSecurityAttr *attr)
{
	FwupdSecurityAttrResult result = fwupd_security_attr_get_result(attr);
	if (result == FWUPD_SECURITY_ATTR_RESULT_VALID) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Valid");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_VALID) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Invalid");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_ENABLED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Enabled");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Disabled");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_LOCKED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Locked");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Unlocked");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_ENCRYPTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Encrypted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_ENCRYPTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Unencrypted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_TAINTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Tainted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_TAINTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Untainted");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_FOUND) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Found");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Not found");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_SUPPORTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Supported");
	}
	if (result == FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("Not supported");
	}

	/* fallback */
	if (fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS)) {
		/* TRANSLATORS: Suffix: the HSI result */
		return _("OK");
	}

	/* TRANSLATORS: Suffix: the fallback HSI result */
	return _("Failed");
}

/**
 * fu_security_attrs_to_json_string：
 * Convert security attribute to JSON string.
 * @attrs: a pointer for a FuSecurityAttrs data structure.
 * @error: return location for an error
 *
 * fu_security_attrs_to_json_string() converts FuSecurityAttrs and return the
 * string pointer. The converted JSON format is shown as follows:
 * {
 *     "SecurityAttributes": {
 *         "$AppStreamID1": {
 *              "name": "aaa",
 *              "value": "bbb"
 *         },
 * 	   "$AppStreamID2": {
 *              "name": "aaa",
 *              "value": "bbb"
 *         },
 *     }
 *  }
 *
 * Returns: A string and NULL on fail.
 *
 * Since: 1.7.0
 *
 */
gchar *
fu_security_attrs_to_json_string(FuSecurityAttrs *attrs, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) json_root = NULL;
	fu_security_attrs_to_json(attrs, builder);
	json_root = json_builder_get_root(builder);
	json_generator = json_generator_new();
	json_generator_set_root(json_generator, json_root);
	data = json_generator_to_data(json_generator, NULL);
	if (data == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to convert security attribute to json.");
		return NULL;
	}
	return g_steal_pointer(&data);
}

void
fu_security_attrs_to_json(FuSecurityAttrs *attrs, JsonBuilder *builder)
{
	g_autoptr(GPtrArray) items = NULL;
	g_autoptr(GError) error = NULL;
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "SecurityAttributes");
	json_builder_begin_object(builder);
	items = fu_security_attrs_get_all(attrs);
	for (guint i = 0; i < items->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(items, i);
		json_builder_set_member_name(builder, fwupd_security_attr_get_appstream_id(attr));
		json_builder_begin_object(builder);
		fwupd_security_attr_to_json(attr, builder);
		json_builder_end_object(builder);
	}
	json_builder_end_object(builder);
	json_builder_end_object(builder);
}

guint
fu_security_attrs_compare_hsi_score(const guint previous_hsi, const guint current_hsi)
{
	if (current_hsi > previous_hsi)
		return 1;
	else if (current_hsi < previous_hsi)
		return -1;
	else
		return 0;
}

static void
fu_security_attr_dup_json_array_to_builder(JsonBuilder *builder,
					   JsonArray *src,
					   const gchar *item_name)
{
	JsonNode *json_node;
	if (src != NULL) {
		json_builder_set_member_name(builder, item_name);
		json_builder_begin_array(builder);
		for (guint i = 0; i < json_array_get_length(src); i++) {
			json_node = json_array_dup_element(src, i);
			json_builder_add_value(builder, json_node);
		}
		json_builder_end_array(builder);
	}
}

static void
fu_security_attr_dup_json(JsonObject *src, JsonBuilder *builder)
{
	JsonArray *array_items = NULL;
	json_builder_set_member_name(builder, FWUPD_RESULT_KEY_HSI_LEVEL);
	json_builder_add_int_value(builder,
				   json_object_get_int_member(src, FWUPD_RESULT_KEY_HSI_LEVEL));
	json_builder_set_member_name(builder, FWUPD_RESULT_KEY_HSI_RESULT);
	json_builder_add_string_value(
	    builder,
	    json_object_get_string_member(src, FWUPD_RESULT_KEY_HSI_RESULT));
	json_builder_set_member_name(builder, FWUPD_RESULT_KEY_NAME);
	json_builder_add_string_value(builder,
				      json_object_get_string_member(src, FWUPD_RESULT_KEY_NAME));
	if (json_object_has_member(src, FWUPD_RESULT_KEY_FLAGS) == TRUE) {
		array_items = json_object_get_array_member(src, FWUPD_RESULT_KEY_FLAGS);
		fu_security_attr_dup_json_array_to_builder(builder,
							   array_items,
							   FWUPD_RESULT_KEY_FLAGS);
	}
}

/**
 * fu_security_attr_flag_to_string_array:
 *
 * @attr: a single FwupdSecurityAttr
 *
 * Convert flags to string and store them in GPtrArray.
 *
 * Since: 1.7.0
 *
 */
static GPtrArray *
fu_security_attr_flag_to_string_array(FwupdSecurityAttr *attr)
{
	GPtrArray *flag_array = NULL;
	FwupdSecurityAttrFlags flags = fwupd_security_attr_get_flags(attr);
	if (flags != FWUPD_SECURITY_ATTR_FLAG_NONE) {
		flag_array = g_ptr_array_new_with_free_func(g_free);
		for (guint i = 0; i < 64; i++) {
			if ((flags & ((guint64)1 << i)) == 0)
				continue;
			g_ptr_array_add(
			    flag_array,
			    g_strdup(fwupd_security_attr_flag_to_string((guint64)1 << i)));
		}
	} else {
		return NULL;
	}
	return flag_array;
}

/**
 * fu_security_attr_deep_object_compare:
 *
 * Detect HSI changes and put the results into a JSON builder.
 *
 * @current_attr: a pointer for a current FuSecurityAttrs data structure.
 * @previous_json_obj: a JSON object of previous security detail.
 * @result_builder: A JSON builder.
 *
 * The format of the results are shown as follows.
 * {
 *	"$appstreamID_difference": {
 *		"previous": {
 *			"AppstreamID": ...
 *			...
 *		},
 *		"current": {
 *			"AppstreamID": ...
 *			...
 *		}
 *	},
 *	"$appstreamID2_new" {
 *		"new": {
 *		...
 *		},
 *	}
 *	"$appstreamID3_removed" {
 *		"removed": {
 *		...
 *		}
 *	}
 * }
 *
 * Returns: TRUE on success and FALSE on error.
 *
 * Since: 1.7.0
 *
 */
static gboolean
fu_security_attr_deep_object_compare(FwupdSecurityAttr *current_attr,
				     JsonObject *previous_json_obj,
				     JsonBuilder *result_builder)
{
	g_autoptr(GPtrArray) flag_array = NULL;
	if (previous_json_obj != NULL) {
		if (fwupd_security_attr_get_level(current_attr) ==
		    json_object_get_int_member(previous_json_obj, FWUPD_RESULT_KEY_HSI_LEVEL))
			return TRUE;

		json_builder_set_member_name(
		    result_builder,
		    json_object_get_string_member(previous_json_obj,
						  FWUPD_RESULT_KEY_APPSTREAM_ID));
		json_builder_begin_object(result_builder);
		json_builder_set_member_name(result_builder, "previous");
		json_builder_begin_object(result_builder);
		fu_security_attr_dup_json(previous_json_obj, result_builder);
		json_builder_end_object(result_builder);
		json_builder_set_member_name(result_builder, "current");
	} else {
		json_builder_set_member_name(result_builder,
					     fwupd_security_attr_get_appstream_id(current_attr));
		json_builder_begin_object(result_builder);
		json_builder_set_member_name(result_builder, "new");
	}
	json_builder_begin_object(result_builder);
	json_builder_set_member_name(result_builder, FWUPD_RESULT_KEY_HSI_LEVEL);
	json_builder_add_int_value(result_builder, fwupd_security_attr_get_level(current_attr));
	json_builder_set_member_name(result_builder, FWUPD_RESULT_KEY_HSI_RESULT);
	json_builder_add_string_value(
	    result_builder,
	    fwupd_security_attr_result_to_string(fwupd_security_attr_get_result(current_attr)));
	json_builder_set_member_name(result_builder, FWUPD_RESULT_KEY_NAME);
	json_builder_add_string_value(result_builder, fwupd_security_attr_get_name(current_attr));
	flag_array = fu_security_attr_flag_to_string_array(current_attr);
	if (flag_array != NULL) {
		json_builder_set_member_name(result_builder, FWUPD_RESULT_KEY_FLAGS);
		json_builder_begin_array(result_builder);
		for (guint i = 0; i < flag_array->len; i++) {
			json_builder_add_string_value(result_builder,
						      g_strdup(g_ptr_array_index(flag_array, i)));
		}
		json_builder_end_array(result_builder);
	}
	json_builder_end_object(result_builder);
	json_builder_end_object(result_builder);

	return FALSE;
}

static void
fu_security_attr_append_remove_to_result(JsonObject *previous_json_obj, JsonBuilder *result_builder)
{
	json_builder_set_member_name(
	    result_builder,
	    json_object_get_string_member(previous_json_obj, FWUPD_RESULT_KEY_APPSTREAM_ID));
	json_builder_begin_object(result_builder);
	json_builder_set_member_name(result_builder, "removed");
	json_builder_begin_object(result_builder);
	fu_security_attr_dup_json(previous_json_obj, result_builder);
	json_builder_end_object(result_builder);
	json_builder_end_object(result_builder);
}

gchar *
fu_security_attrs_hsi_change(FuSecurityAttrs *attrs, const gchar *last_hsi_detail)
{
	g_autofree gchar *data = NULL;
	g_autoptr(JsonParser) parser = json_parser_new();
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonBuilder) result_builder = json_builder_new();
	g_autoptr(JsonNode) result_json_root = NULL;
	g_autoptr(GPtrArray) items = NULL;
	g_autoptr(GHashTable) found_list = g_hash_table_new(g_str_hash, g_str_equal);
	g_autoptr(GList) member_list = NULL;
	g_autoptr(GList) removed_keys = NULL;
	JsonNode *json_root = NULL;
	JsonObject *json_obj = NULL;
	JsonObject *previous_security_attrs = NULL;

	json_parser_load_from_data(parser, last_hsi_detail, -1, NULL);
	json_root = json_parser_get_root(parser);
	json_obj = json_node_get_object(json_root);
	previous_security_attrs = json_object_get_object_member(json_obj, "SecurityAttributes");

	member_list = json_object_get_members(previous_security_attrs);
	for (GList *tmp = member_list; tmp != NULL; tmp = tmp->next) {
		g_hash_table_insert(found_list, g_strdup(tmp->data), NULL);
	}

	items = fu_security_attrs_get_all(attrs);
	json_builder_begin_object(result_builder);
	for (guint i = 0; i < items->len; i++) {
		FwupdSecurityAttr *attr = g_ptr_array_index(items, i);
		if (json_object_has_member(previous_security_attrs,
					   fwupd_security_attr_get_appstream_id(attr)) == TRUE) {
			/* hit */
			g_hash_table_remove(found_list, fwupd_security_attr_get_appstream_id(attr));
			fu_security_attr_deep_object_compare(
			    attr,
			    json_object_get_object_member(
				previous_security_attrs,
				fwupd_security_attr_get_appstream_id(attr)),
			    result_builder);
		} else {
			/* miss- a new AppStreamId */
			fu_security_attr_deep_object_compare(attr, NULL, result_builder);
		}
	}
	removed_keys = g_hash_table_get_keys(found_list);
	if (removed_keys != NULL) {
		/* removed from current */
		for (GList *tmp_remove = removed_keys; tmp_remove != NULL;
		     tmp_remove = tmp_remove->next) {
			fu_security_attr_append_remove_to_result(
			    json_object_get_object_member(previous_security_attrs,
							  (gchar *)tmp_remove->data),
			    result_builder);
		}
	}

	json_builder_end_object(result_builder);
	json_generator = json_generator_new();
	result_json_root = json_builder_get_root(result_builder);
	json_generator_set_root(json_generator, result_json_root);
	data = json_generator_to_data(json_generator, NULL);
	return g_steal_pointer(&data);
}
