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
	guint32			 mei_cfg;
	FuMeiVersion		 vers;
	FuMeiIssue		 issue;
};

#define PCI_CFG_HFS_1		0x40

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
	guint8 operating_mode = (priv->mei_cfg >> 16) & 0xF;
	guint8 ver = priv->vers.major;

	if (ver == 1 || ver == 2) {
		if (operating_mode == 0xF)
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
	FuMeiFamily family;
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
	family = fu_mei_detect_family (plugin);
	if (family == FU_MEI_FAMILY_CSME)
		priv->issue = fu_mei_common_is_csme_vulnerable (&priv->vers);
	else if (family == FU_MEI_FAMILY_TXE)
		priv->issue = fu_mei_common_is_txe_vulnerable (&priv->vers);
	else if (family == FU_MEI_FAMILY_SPS)
		priv->issue = fu_mei_common_is_sps_vulnerable (&priv->vers);
	if (g_getenv ("FWUPD_MEI_VERBOSE") != NULL) {
		g_debug ("%s version parsed as %u.%u.%u",
			 fu_mei_common_family_to_string (family),
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

	/* grab MEI config Register */
	if (!fu_udev_device_pread_full (device, PCI_CFG_HFS_1, buf, sizeof(buf), error)) {
		g_prefix_error (error, "could not read MEI: ");
		return FALSE;
	}
	priv->mei_cfg = fu_common_read_uint32 (buf, G_LITTLE_ENDIAN);
	priv->has_device = TRUE;

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
	fu_security_attrs_append (attrs, attr);

	/* Manufacturing Mode */
	if (((priv->mei_cfg >> 4) & 0x1) != 0) {
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
	fu_security_attrs_append (attrs, attr);

	/* Flash Descriptor Security Override Strap */
	if (((priv->mei_cfg >> 16) & 0x7) != 0) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

static void
fu_plugin_add_security_attrs_csme_version (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	if (priv->issue == FU_MEI_ISSUE_UNKNOWN) {
		g_warning ("ME family not supported for %u:%u.%u.%u.%u",
			   priv->vers.platform,
			   priv->vers.major,
			   priv->vers.minor,
			   priv->vers.hotfix,
			   priv->vers.buildno);
		return;
	}

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_MEI_VERSION);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fu_security_attrs_append (attrs, attr);

	/* Flash Descriptor Security Override Strap */
	if (priv->issue == FU_MEI_ISSUE_VULNERABLE) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
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
	fu_plugin_add_security_attrs_csme_version (plugin, attrs);
}
