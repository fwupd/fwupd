/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#ifdef HAVE_CPUID_H
#include <cpuid.h>
#endif

#include "fu-common-private.h"
#include "fu-firmware.h"
#include "fu-string.h"

/**
 * fu_cpuid:
 * @leaf: the CPUID level, now called the 'leaf' by Intel
 * @eax: (out) (nullable): EAX register
 * @ebx: (out) (nullable): EBX register
 * @ecx: (out) (nullable): ECX register
 * @edx: (out) (nullable): EDX register
 * @error: (nullable): optional return location for an error
 *
 * Calls CPUID and returns the registers for the given leaf.
 *
 * Returns: %TRUE if the registers are set.
 *
 * Since: 1.8.2
 **/
gboolean
fu_cpuid(guint32 leaf, guint32 *eax, guint32 *ebx, guint32 *ecx, guint32 *edx, GError **error)
{
#ifdef HAVE_CPUID_H
	guint eax_tmp = 0;
	guint ebx_tmp = 0;
	guint ecx_tmp = 0;
	guint edx_tmp = 0;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* get vendor */
	__get_cpuid_count(leaf, 0x0, &eax_tmp, &ebx_tmp, &ecx_tmp, &edx_tmp);
	if (eax != NULL)
		*eax = eax_tmp;
	if (ebx != NULL)
		*ebx = ebx_tmp;
	if (ecx != NULL)
		*ecx = ecx_tmp;
	if (edx != NULL)
		*edx = edx_tmp;
	return TRUE;
#else
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no <cpuid.h> support");
	return FALSE;
#endif
}

/**
 * fu_cpu_get_vendor:
 *
 * Uses CPUID to discover the CPU vendor.
 *
 * Returns: a CPU vendor, e.g. %FU_CPU_VENDOR_AMD if the vendor was AMD.
 *
 * Since: 1.8.2
 **/
FuCpuVendor
fu_cpu_get_vendor(void)
{
#ifdef HAVE_CPUID_H
	guint ebx = 0;
	guint ecx = 0;
	guint edx = 0;

	if (fu_cpuid(0x0, NULL, &ebx, &ecx, &edx, NULL)) {
		if (ebx == signature_INTEL_ebx && edx == signature_INTEL_edx &&
		    ecx == signature_INTEL_ecx) {
			return FU_CPU_VENDOR_INTEL;
		}
		if (ebx == signature_AMD_ebx && edx == signature_AMD_edx &&
		    ecx == signature_AMD_ecx) {
			return FU_CPU_VENDOR_AMD;
		}
	}
#endif

	/* failed */
	return FU_CPU_VENDOR_UNKNOWN;
}

/**
 * fu_common_is_live_media:
 *
 * Checks if the user is running from a live media using various heuristics.
 *
 * Returns: %TRUE if live
 *
 * Since: 1.4.6
 **/
gboolean
fu_common_is_live_media(void)
{
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) tokens = NULL;
	const gchar *args[] = {
	    "rd.live.image",
	    "boot=live",
	    NULL, /* last entry */
	};
	if (g_file_test("/cdrom/.disk/info", G_FILE_TEST_EXISTS))
		return TRUE;
	if (!g_file_get_contents("/proc/cmdline", &buf, &bufsz, NULL))
		return FALSE;
	if (bufsz <= 1)
		return FALSE;
	tokens = fu_strsplit(buf, bufsz - 1, " ", -1);
	for (guint i = 0; args[i] != NULL; i++) {
		if (g_strv_contains((const gchar *const *)tokens, args[i]))
			return TRUE;
	}
	return FALSE;
}

/**
 * fu_common_get_memory_size:
 *
 * Returns the size of physical memory.
 *
 * Returns: bytes
 *
 * Since: 1.5.6
 **/
guint64
fu_common_get_memory_size(void)
{
	return fu_common_get_memory_size_impl();
}

/**
 * fu_common_get_kernel_cmdline:
 * @error: (nullable): optional return location for an error
 *
 * Returns the current kernel command line options.
 *
 * Returns: options as a string, or %NULL on error
 *
 * Since: 1.5.6
 **/
gchar *
fu_common_get_kernel_cmdline(GError **error)
{
	return fu_common_get_kernel_cmdline_impl(error);
}

/**
 * fu_common_check_full_disk_encryption:
 * @error: (nullable): optional return location for an error
 *
 * Checks that all FDE volumes are not going to be affected by a firmware update. If unsure,
 * return with failure and let the user decide.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.7.1
 **/
gboolean
fu_common_check_full_disk_encryption(GError **error)
{
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	devices = fu_common_get_block_devices(error);
	if (devices == NULL)
		return FALSE;
	for (guint i = 0; i < devices->len; i++) {
		GDBusProxy *proxy = g_ptr_array_index(devices, i);
		g_autoptr(GVariant) id_type = g_dbus_proxy_get_cached_property(proxy, "IdType");
		g_autoptr(GVariant) device = g_dbus_proxy_get_cached_property(proxy, "Device");
		if (id_type == NULL || device == NULL)
			continue;
		if (g_strcmp0(g_variant_get_string(id_type, NULL), "BitLocker") == 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_WOULD_BLOCK,
				    "%s device %s is encrypted",
				    g_variant_get_string(id_type, NULL),
				    g_variant_get_bytestring(device));
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * fu_common_get_olson_timezone_id:
 * @error: (nullable): optional return location for an error
 *
 * Gets the system Olson timezone ID, as used in the CLDR and ICU specifications.
 *
 * Returns: timezone string, e.g. `Europe/London` or %NULL on error
 *
 * Since: 1.9.7
 **/
gchar *
fu_common_get_olson_timezone_id(GError **error)
{
	return fu_common_get_olson_timezone_id_impl(error);
}

/**
 * fu_common_align_up:
 * @value: value to align
 * @alignment: align to this power of 2, where 0x1F is the maximum value of 2GB
 *
 * Align a value to a power of 2 boundary, where @alignment is the bit position
 * to align to. If @alignment is zero then @value is always returned unchanged.
 *
 * Returns: aligned value, which will be the same as @value if already aligned,
 * 		or %G_MAXSIZE if the value would overflow
 *
 * Since: 1.6.0
 **/
gsize
fu_common_align_up(gsize value, guint8 alignment)
{
	gsize value_new;
	gsize mask = (gsize)1 << alignment;

	g_return_val_if_fail(alignment <= FU_FIRMWARE_ALIGNMENT_2G, G_MAXSIZE);

	/* no alignment required */
	if ((value & (mask - 1)) == 0)
		return value;

	/* increment up to the next alignment value */
	value_new = value + mask;
	value_new &= ~(mask - 1);

	/* overflow */
	if (value_new < value)
		return G_MAXSIZE;

	/* success */
	return value_new;
}

/**
 * fu_power_state_to_string:
 * @power_state: a power state, e.g. %FU_POWER_STATE_AC_FULLY_CHARGED
 *
 * Converts an enumerated type to a string.
 *
 * Returns: a string, or %NULL for invalid
 *
 * Since: 1.8.11
 **/
const gchar *
fu_power_state_to_string(FuPowerState power_state)
{
	if (power_state == FU_POWER_STATE_UNKNOWN)
		return "unknown";
	if (power_state == FU_POWER_STATE_BATTERY)
		return "battery";
	if (power_state == FU_POWER_STATE_BATTERY_DISCHARGING)
		return "battery-discharging";
	if (power_state == FU_POWER_STATE_BATTERY_EMPTY)
		return "battery-empty";
	if (power_state == FU_POWER_STATE_AC)
		return "ac";
	if (power_state == FU_POWER_STATE_AC_CHARGING)
		return "ac-charging";
	if (power_state == FU_POWER_STATE_AC_FULLY_CHARGED)
		return "ac-fully-charged";
	return NULL;
}

/**
 * fu_power_state_is_ac:
 * @power_state: a power state, e.g. %FU_POWER_STATE_AC_FULLY_CHARGED
 *
 * Determines if the power state can be considered "on AC power".
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.11
 **/
gboolean
fu_power_state_is_ac(FuPowerState power_state)
{
	if (power_state == FU_POWER_STATE_UNKNOWN || power_state == FU_POWER_STATE_AC ||
	    power_state == FU_POWER_STATE_AC_CHARGING ||
	    power_state == FU_POWER_STATE_AC_FULLY_CHARGED)
		return TRUE;
	return FALSE;
}

/**
 * fu_lid_state_to_string:
 * @lid_state: a lid state, e.g. %FU_LID_STATE_CLOSED
 *
 * Converts an enumerated type to a string.
 *
 * Returns: a string, or %NULL for invalid
 *
 * Since: 1.7.4
 **/
const gchar *
fu_lid_state_to_string(FuLidState lid_state)
{
	if (lid_state == FU_LID_STATE_UNKNOWN)
		return "unknown";
	if (lid_state == FU_LID_STATE_OPEN)
		return "open";
	if (lid_state == FU_LID_STATE_CLOSED)
		return "closed";
	return NULL;
}

/**
 * fu_display_state_to_string:
 * @display_state: a lid state, e.g. %FU_DISPLAY_STATE_CONNECTED
 *
 * Converts an enumerated type to a string.
 *
 * Returns: a string, or %NULL for invalid
 *
 * Since: 1.9.6
 **/
const gchar *
fu_display_state_to_string(FuDisplayState display_state)
{
	if (display_state == FU_DISPLAY_STATE_UNKNOWN)
		return "unknown";
	if (display_state == FU_DISPLAY_STATE_CONNECTED)
		return "connected";
	if (display_state == FU_DISPLAY_STATE_DISCONNECTED)
		return "disconnected";
	return NULL;
}

/**
 * fu_xmlb_builder_insert_kv:
 * @bn: #XbBuilderNode
 * @key: string key
 * @value: string value
 *
 * Convenience function to add an XML node with a string value. If @value is %NULL
 * then no member is added.
 *
 * Since: 1.6.0
 **/
void
fu_xmlb_builder_insert_kv(XbBuilderNode *bn, const gchar *key, const gchar *value)
{
	if (value == NULL)
		return;
	xb_builder_node_insert_text(bn, key, value, NULL);
}

/**
 * fu_xmlb_builder_insert_kx:
 * @bn: #XbBuilderNode
 * @key: string key
 * @value: integer value
 *
 * Convenience function to add an XML node with an integer value. If @value is 0
 * then no member is added.
 *
 * Since: 1.6.0
 **/
void
fu_xmlb_builder_insert_kx(XbBuilderNode *bn, const gchar *key, guint64 value)
{
	g_autofree gchar *value_hex = NULL;
	if (value == 0)
		return;
	value_hex = g_strdup_printf("0x%x", (guint)value);
	xb_builder_node_insert_text(bn, key, value_hex, NULL);
}

/**
 * fu_xmlb_builder_insert_kb:
 * @bn: #XbBuilderNode
 * @key: string key
 * @value: boolean value
 *
 * Convenience function to add an XML node with a boolean value.
 *
 * Since: 1.6.0
 **/
void
fu_xmlb_builder_insert_kb(XbBuilderNode *bn, const gchar *key, gboolean value)
{
	xb_builder_node_insert_text(bn, key, value ? "true" : "false", NULL);
}
