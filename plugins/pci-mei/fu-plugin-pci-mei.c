/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-mei-common.h"

struct FuPluginData {
	gboolean		 has_device;
	FuMeiHfsts1		 hfsts1;
	FuMeiHfsts2		 hfsts2;
	FuMeiHfsts3		 hfsts3;
	FuMeiHfsts4		 hfsts4;
	FuMeiHfsts5		 hfsts5;
	FuMeiHfsts6		 hfsts6;
	FuMeiFamily		 family;
	FuMeiVersion		 vers;
	FuMeiIssue		 issue;
};

#define PCI_CFG_HFS_1		0x40
#define PCI_CFG_HFS_2		0x48
#define PCI_CFG_HFS_3		0x60
#define PCI_CFG_HFS_4		0x64
#define PCI_CFG_HFS_5		0x68
#define PCI_CFG_HFS_6		0x6c

static void
fu_mei_hfsts_to_string (FuPlugin *plugin, guint idt, GString *str)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	fu_common_string_append_kv (str, idt, "HFSTS1", NULL);
	fu_mei_hfsts1_to_string (priv->hfsts1, idt + 1, str);
	fu_common_string_append_kv (str, idt, "HFSTS2", NULL);
	fu_mei_hfsts2_to_string (priv->hfsts2, idt + 1, str);
	fu_common_string_append_kv (str, idt, "HFSTS3", NULL);
	fu_mei_hfsts3_to_string (priv->hfsts3, idt + 1, str);
	fu_common_string_append_kv (str, idt, "HFSTS4", NULL);
	fu_mei_hfsts4_to_string (priv->hfsts4, idt + 1, str);
	fu_common_string_append_kv (str, idt, "HFSTS5", NULL);
	fu_mei_hfsts5_to_string (priv->hfsts5, idt + 1, str);
	fu_common_string_append_kv (str, idt, "HFSTS6", NULL);
	fu_mei_hfsts6_to_string (priv->hfsts6, idt + 1, str);
}

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "pci");
}

static FuMeiFamily
fu_mei_detect_family (FuPlugin *plugin)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	guint8 ver = priv->vers.major;

	if (ver == 1 || ver == 2) {
		if (priv->hfsts1.fields.operation_mode == 0xf)
			return FU_MEI_FAMILY_SPS;
		return FU_MEI_FAMILY_TXE;
	}
	if (ver == 3 || ver == 4 || ver == 5)
		return FU_MEI_FAMILY_TXE;
	if (ver == 6 || ver == 7 || ver == 8 || ver == 9 || ver == 10)
		return FU_MEI_FAMILY_ME;
	if (ver == 11 || ver == 12 || ver == 13 || ver == 14 || ver == 15)
		return FU_MEI_FAMILY_CSME;
	return FU_MEI_FAMILY_UNKNOWN;
}

static gboolean
fu_mei_parse_fwvers (FuPlugin *plugin, const gchar *fwvers, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_auto(GStrv) lines = NULL;
	g_auto(GStrv) sections = NULL;
	g_auto(GStrv) split = NULL;

	/* we only care about the first version */
	lines = g_strsplit (fwvers, "\n", -1);
	if (g_strv_length (lines) < 1) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "expected data, got %s", fwvers);
		return FALSE;
	}

	/* split platform : version */
	sections = g_strsplit (lines[0], ":", -1);
	if (g_strv_length (sections) != 2) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "expected platform:major.minor.micro.build, got %s",
			     lines[0]);
		return FALSE;
	}

	/* parse platform and versions */
	priv->vers.platform = fu_common_strtoull (sections[0]);
	split = g_strsplit (sections[1], ".", -1);
	if (g_strv_length (split) != 4) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "expected major.minor.micro.build, got %s",
			     sections[1]);
		return FALSE;
	}
	priv->vers.major = fu_common_strtoull (split[0]);
	priv->vers.minor = fu_common_strtoull (split[1]);
	priv->vers.hotfix = fu_common_strtoull (split[2]);
	priv->vers.buildno = fu_common_strtoull (split[3]);

	/* check the AMT version for issues using the data from:
	 * https://downloadcenter.intel.com/download/28632 */
	priv->family = fu_mei_detect_family (plugin);
	if (priv->family == FU_MEI_FAMILY_CSME)
		priv->issue = fu_mei_common_is_csme_vulnerable (&priv->vers);
	else if (priv->family == FU_MEI_FAMILY_TXE)
		priv->issue = fu_mei_common_is_txe_vulnerable (&priv->vers);
	else if (priv->family == FU_MEI_FAMILY_SPS)
		priv->issue = fu_mei_common_is_sps_vulnerable (&priv->vers);
	if (g_getenv ("FWUPD_MEI_VERBOSE") != NULL) {
		g_debug ("%s version parsed as %u.%u.%u",
			 fu_mei_common_family_to_string (priv->family),
			 priv->vers.major, priv->vers.minor, priv->vers.hotfix);
	}
	return TRUE;
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	const gchar *fwvers;
	guint8 buf[4] = { 0x0 };
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* interesting device? */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "pci") != 0)
		return TRUE;

	/* open the config */
	fu_udev_device_set_flags (device, FU_UDEV_DEVICE_FLAG_USE_CONFIG);
	if (!fu_udev_device_set_physical_id (device, "pci", error))
		return FALSE;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* grab MEI config registers */
	if (!fu_udev_device_pread_full (device, PCI_CFG_HFS_1, buf, sizeof(buf), error)) {
		g_prefix_error (error, "could not read HFS1: ");
		return FALSE;
	}
	priv->hfsts1.data = fu_common_read_uint32 (buf, G_LITTLE_ENDIAN);
	if (!fu_udev_device_pread_full (device, PCI_CFG_HFS_2, buf, sizeof(buf), error)) {
		g_prefix_error (error, "could not read HFS2: ");
		return FALSE;
	}
	priv->hfsts2.data = fu_common_read_uint32 (buf, G_LITTLE_ENDIAN);
	if (!fu_udev_device_pread_full (device, PCI_CFG_HFS_3, buf, sizeof(buf), error)) {
		g_prefix_error (error, "could not read HFS3: ");
		return FALSE;
	}
	priv->hfsts3.data = fu_common_read_uint32 (buf, G_LITTLE_ENDIAN);
	if (!fu_udev_device_pread_full (device, PCI_CFG_HFS_4, buf, sizeof(buf), error)) {
		g_prefix_error (error, "could not read HFS4: ");
		return FALSE;
	}
	priv->hfsts4.data = fu_common_read_uint32 (buf, G_LITTLE_ENDIAN);
	if (!fu_udev_device_pread_full (device, PCI_CFG_HFS_5, buf, sizeof(buf), error)) {
		g_prefix_error (error, "could not read HFS5: ");
		return FALSE;
	}
	priv->hfsts5.data = fu_common_read_uint32 (buf, G_LITTLE_ENDIAN);
	if (!fu_udev_device_pread_full (device, PCI_CFG_HFS_6, buf, sizeof(buf), error)) {
		g_prefix_error (error, "could not read HFS6: ");
		return FALSE;
	}
	priv->hfsts6.data = fu_common_read_uint32 (buf, G_LITTLE_ENDIAN);
	priv->has_device = TRUE;

	/* dump to console */
	if (g_getenv ("FWUPD_PCI_MEI_VERBOSE") != NULL) {
		g_autoptr(GString) str = g_string_new (NULL);
		fu_mei_hfsts_to_string (plugin, 0, str);
		g_debug ("\n%s", str->str);
	}

	/* check firmware version */
	fwvers = fu_udev_device_get_sysfs_attr (device, "mei/mei0/fw_ver", NULL);
	if (fwvers != NULL) {
		if (!fu_mei_parse_fwvers (plugin, fwvers, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_plugin_add_security_attrs_manufacturing_mode (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_add_metadata (attr, "kind", fu_mei_common_family_to_string (priv->family));
	fu_security_attrs_append (attrs, attr);

	/* Manufacturing Mode */
	if (priv->hfsts1.fields.mfg_mode) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

static void
fu_plugin_add_security_attrs_override_strap (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_add_metadata (attr, "kind", fu_mei_common_family_to_string (priv->family));
	fu_security_attrs_append (attrs, attr);

	/* Flash Descriptor Security Override Strap */
	if (priv->hfsts1.fields.operation_mode == ME_HFS_MODE_OVER_JMPR) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

static void
fu_plugin_add_security_attrs_bootguard_enabled (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fu_security_attrs_append (attrs, attr);

	/* disabled at runtime? */
	if (priv->hfsts6.fields.boot_guard_disable) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

static void
fu_plugin_add_security_attrs_bootguard_verified (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* disabled */
	if (priv->hfsts6.fields.boot_guard_disable)
		return;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fu_security_attrs_append (attrs, attr);

	/* measured boot is not sufficient, verified is required */
	if (!priv->hfsts6.fields.verified_boot) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_plugin_add_security_attrs_bootguard_acm (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* disabled */
	if (priv->hfsts6.fields.boot_guard_disable)
		return;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fu_security_attrs_append (attrs, attr);

	/* ACM protection required */
	if (!priv->hfsts6.fields.force_boot_guard_acm) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_plugin_add_security_attrs_bootguard_policy (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* disabled */
	if (priv->hfsts6.fields.boot_guard_disable)
		return;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fu_security_attrs_append (attrs, attr);

	/* policy must be to immediatly shutdown */
	if (priv->hfsts6.fields.error_enforce_policy != ME_HFS_ENFORCEMENT_POLICY_SHUTDOWN_NOW) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_plugin_add_security_attrs_bootguard_otp (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* disabled */
	if (priv->hfsts6.fields.boot_guard_disable)
		return;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fu_security_attrs_append (attrs, attr);

	/* ensure vendor set the FPF OTP fuse */
	if (!priv->hfsts6.fields.fpf_soc_lock) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_plugin_add_security_attrs_bootguard (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	fu_plugin_add_security_attrs_bootguard_enabled (plugin, attrs);
	fu_plugin_add_security_attrs_bootguard_verified (plugin, attrs);
	fu_plugin_add_security_attrs_bootguard_acm (plugin, attrs);
	fu_plugin_add_security_attrs_bootguard_policy (plugin, attrs);
	fu_plugin_add_security_attrs_bootguard_otp (plugin, attrs);
}

static void
fu_plugin_add_security_attrs_mei_version (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autofree gchar *version = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* format version as string */
	version = g_strdup_printf ("%u:%u.%u.%u.%u",
				   priv->vers.platform,
				   priv->vers.major,
				   priv->vers.minor,
				   priv->vers.hotfix,
				   priv->vers.buildno);
	if (priv->issue == FU_MEI_ISSUE_UNKNOWN) {
		g_warning ("ME family not supported for %s", version);
		return;
	}

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_MEI_VERSION);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_add_metadata (attr, "kind", fu_mei_common_family_to_string (priv->family));
	fwupd_security_attr_add_metadata (attr, "version", version);
	fu_security_attrs_append (attrs, attr);

	/* Flash Descriptor Security Override Strap */
	if (priv->issue == FU_MEI_ISSUE_VULNERABLE) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);

	/* only Intel */
	if (!fu_common_is_cpu_intel ())
		return;
	if (!priv->has_device)
		return;

	fu_plugin_add_security_attrs_manufacturing_mode (plugin, attrs);
	fu_plugin_add_security_attrs_override_strap (plugin, attrs);
	fu_plugin_add_security_attrs_bootguard (plugin, attrs);
	fu_plugin_add_security_attrs_mei_version (plugin, attrs);
}
