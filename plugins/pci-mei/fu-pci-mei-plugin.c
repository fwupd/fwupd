/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-mei-common.h"
#include "fu-pci-mei-plugin.h"

struct _FuPciMeiPlugin {
	FuPlugin parent_instance;
	FuDevice *pci_device;
	FuMeiHfsts1 hfsts1;
	FuMeiHfsts2 hfsts2;
	FuMeiHfsts3 hfsts3;
	FuMeiHfsts4 hfsts4;
	FuMeiHfsts5 hfsts5;
	FuMeiHfsts6 hfsts6;
	FuMeiFamily family;
	FuMeiVersion vers;
	FuMeiIssue issue;
};

G_DEFINE_TYPE(FuPciMeiPlugin, fu_pci_mei_plugin, FU_TYPE_PLUGIN)

#define PCI_CFG_HFS_1 0x40
#define PCI_CFG_HFS_2 0x48
#define PCI_CFG_HFS_3 0x60
#define PCI_CFG_HFS_4 0x64
#define PCI_CFG_HFS_5 0x68
#define PCI_CFG_HFS_6 0x6c

static void
fu_pci_mei_plugin_to_string(FuPlugin *plugin, guint idt, GString *str)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	fu_string_append(str, idt, "HFSTS1", NULL);
	fu_mei_hfsts1_to_string(self->hfsts1, idt + 1, str);
	fu_string_append(str, idt, "HFSTS2", NULL);
	fu_mei_hfsts2_to_string(self->hfsts2, idt + 1, str);
	fu_string_append(str, idt, "HFSTS3", NULL);
	fu_mei_hfsts3_to_string(self->hfsts3, idt + 1, str);
	fu_string_append(str, idt, "HFSTS4", NULL);
	fu_mei_hfsts4_to_string(self->hfsts4, idt + 1, str);
	fu_string_append(str, idt, "HFSTS5", NULL);
	fu_mei_hfsts5_to_string(self->hfsts5, idt + 1, str);
	fu_string_append(str, idt, "HFSTS6", NULL);
	fu_mei_hfsts6_to_string(self->hfsts6, idt + 1, str);
}

static FuMeiFamily
fu_mei_detect_family(FuPlugin *plugin)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	guint8 ver = self->vers.major;

	if (ver == 1 || ver == 2) {
		if (self->hfsts1.fields.operation_mode == 0xf)
			return FU_MEI_FAMILY_SPS;
		return FU_MEI_FAMILY_TXE;
	}
	if (ver == 3 || ver == 4 || ver == 5)
		return FU_MEI_FAMILY_TXE;
	if (ver == 6 || ver == 7 || ver == 8 || ver == 9 || ver == 10)
		return FU_MEI_FAMILY_ME;
	if (ver == 11 || ver == 12 || ver == 13 || ver == 14 || ver == 15 || ver == 16)
		return FU_MEI_FAMILY_CSME;
	return FU_MEI_FAMILY_UNKNOWN;
}

static gboolean
fu_mei_parse_fwvers(FuPlugin *plugin, const gchar *fwvers, GError **error)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	guint64 tmp64 = 0;
	g_auto(GStrv) lines = NULL;
	g_auto(GStrv) sections = NULL;
	g_auto(GStrv) split = NULL;

	/* we only care about the first version */
	lines = g_strsplit(fwvers, "\n", -1);
	if (g_strv_length(lines) < 1) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "expected data, got %s",
			    fwvers);
		return FALSE;
	}

	/* split platform : version */
	sections = g_strsplit(lines[0], ":", -1);
	if (g_strv_length(sections) != 2) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "expected platform:major.minor.micro.build, got %s",
			    lines[0]);
		return FALSE;
	}

	/* parse platform and versions */
	if (!fu_strtoull(sections[0], &tmp64, 0, G_MAXUINT8, error)) {
		g_prefix_error(error, "failed to process platform version %s: ", sections[0]);
		return FALSE;
	}
	self->vers.platform = tmp64;
	split = g_strsplit(sections[1], ".", -1);
	if (g_strv_length(split) != 4) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "expected major.minor.micro.build, got %s",
			    sections[1]);
		return FALSE;
	}

	if (!fu_strtoull(split[0], &tmp64, 0, G_MAXUINT8, error)) {
		g_prefix_error(error, "failed to process major version %s: ", split[0]);
		return FALSE;
	}
	self->vers.major = tmp64;
	if (!fu_strtoull(split[1], &tmp64, 0, G_MAXUINT8, error)) {
		g_prefix_error(error, "failed to process minor version %s: ", split[1]);
		return FALSE;
	}
	self->vers.minor = tmp64;
	if (!fu_strtoull(split[2], &tmp64, 0, G_MAXUINT8, error)) {
		g_prefix_error(error, "failed to process hotfix version %s: ", split[2]);
		return FALSE;
	}
	self->vers.hotfix = tmp64;
	if (!fu_strtoull(split[3], &tmp64, 0, G_MAXUINT16, error)) {
		g_prefix_error(error, "failed to process buildno version %s: ", split[3]);
		return FALSE;
	}
	self->vers.buildno = tmp64;

	/* check the AMT version for issues using the data from:
	 * https://downloadcenter.intel.com/download/28632 */
	self->family = fu_mei_detect_family(plugin);
	if (self->family == FU_MEI_FAMILY_CSME)
		self->issue = fu_mei_common_is_csme_vulnerable(&self->vers);
	else if (self->family == FU_MEI_FAMILY_TXE)
		self->issue = fu_mei_common_is_txe_vulnerable(&self->vers);
	else if (self->family == FU_MEI_FAMILY_SPS)
		self->issue = fu_mei_common_is_sps_vulnerable(&self->vers);
	g_debug("%s version parsed as %u.%u.%u",
		fu_mei_common_family_to_string(self->family),
		self->vers.major,
		self->vers.minor,
		self->vers.hotfix);
	return TRUE;
}

static gboolean
fu_pci_mei_plugin_backend_device_added(FuPlugin *plugin,
				       FuDevice *device,
				       FuProgress *progress,
				       GError **error)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	const gchar *fwvers;
	guint8 buf[4] = {0x0};
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* interesting device? */
	if (!FU_IS_UDEV_DEVICE(device))
		return TRUE;
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "pci") != 0)
		return TRUE;

	/* open the config */
	fu_udev_device_set_flags(FU_UDEV_DEVICE(device), FU_UDEV_DEVICE_FLAG_USE_CONFIG);
	if (!fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "pci", error))
		return FALSE;
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;

	/* grab MEI config registers */
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(device), PCI_CFG_HFS_1, buf, sizeof(buf), error)) {
		g_prefix_error(error, "could not read HFS1: ");
		return FALSE;
	}
	self->hfsts1.data = fu_memread_uint32(buf, G_LITTLE_ENDIAN);
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(device), PCI_CFG_HFS_2, buf, sizeof(buf), error)) {
		g_prefix_error(error, "could not read HFS2: ");
		return FALSE;
	}
	self->hfsts2.data = fu_memread_uint32(buf, G_LITTLE_ENDIAN);
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(device), PCI_CFG_HFS_3, buf, sizeof(buf), error)) {
		g_prefix_error(error, "could not read HFS3: ");
		return FALSE;
	}
	self->hfsts3.data = fu_memread_uint32(buf, G_LITTLE_ENDIAN);
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(device), PCI_CFG_HFS_4, buf, sizeof(buf), error)) {
		g_prefix_error(error, "could not read HFS4: ");
		return FALSE;
	}
	self->hfsts4.data = fu_memread_uint32(buf, G_LITTLE_ENDIAN);
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(device), PCI_CFG_HFS_5, buf, sizeof(buf), error)) {
		g_prefix_error(error, "could not read HFS5: ");
		return FALSE;
	}
	self->hfsts5.data = fu_memread_uint32(buf, G_LITTLE_ENDIAN);
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(device), PCI_CFG_HFS_6, buf, sizeof(buf), error)) {
		g_prefix_error(error, "could not read HFS6: ");
		return FALSE;
	}
	self->hfsts6.data = fu_memread_uint32(buf, G_LITTLE_ENDIAN);
	g_set_object(&self->pci_device, device);

	/* check firmware version */
	fwvers = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device), "mei/mei0/fw_ver", NULL);
	if (fwvers != NULL) {
		if (!fu_mei_parse_fwvers(plugin, fwvers, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_plugin_add_security_attrs_manufacturing_mode(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* Manufacturing Mode */
	fwupd_security_attr_add_metadata(attr,
					 "kind",
					 fu_mei_common_family_to_string(self->family));
	if (self->hfsts1.fields.mfg_mode) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

static void
fu_plugin_add_security_attrs_override_strap(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* Flash Descriptor Security Override Strap */
	fwupd_security_attr_add_metadata(attr,
					 "kind",
					 fu_mei_common_family_to_string(self->family));
	if (self->hfsts1.fields.operation_mode == ME_HFS_MODE_OVER_JMPR) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_LOCKED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
}

static void
fu_plugin_add_security_attrs_bootguard_enabled(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* not supported */
	if (self->family == FU_MEI_FAMILY_TXE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* disabled at runtime? */
	if (self->hfsts6.fields.boot_guard_disable) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
}

static void
fu_plugin_add_security_attrs_bootguard_verified(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* not supported */
	if (self->family == FU_MEI_FAMILY_TXE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* actively disabled */
	if (self->hfsts6.fields.boot_guard_disable) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* measured boot is not sufficient, verified is required */
	if (!self->hfsts6.fields.verified_boot) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_plugin_add_security_attrs_bootguard_acm(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* not supported */
	if (self->family == FU_MEI_FAMILY_TXE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* actively disabled */
	if (self->hfsts6.fields.boot_guard_disable) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* ACM protection required */
	if (!self->hfsts6.fields.force_boot_guard_acm) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_plugin_add_security_attrs_bootguard_policy(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* not supported */
	if (self->family == FU_MEI_FAMILY_TXE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* actively disabled */
	if (self->hfsts6.fields.boot_guard_disable) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* policy must be to immediately shutdown or after 30 mins -- the latter isn't ideal but
	 * we've been testing for this accidentally for a long time now */
	if (self->hfsts6.fields.error_enforce_policy != ME_HFS_ENFORCEMENT_POLICY_SHUTDOWN_NOW &&
	    self->hfsts6.fields.error_enforce_policy != ME_HFS_ENFORCEMENT_POLICY_SHUTDOWN_30MINS) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_plugin_add_security_attrs_bootguard_otp(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP);
	fu_security_attrs_append(attrs, attr);

	/* no device */
	if (self->pci_device == NULL) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
		return;
	}

	/* not supported */
	if (self->family == FU_MEI_FAMILY_TXE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* actively disabled */
	if (self->hfsts6.fields.boot_guard_disable) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
		return;
	}

	/* ensure vendor set the FPF OTP fuse */
	if (!self->hfsts6.fields.fpf_soc_lock) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_plugin_add_security_attrs_bootguard(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	fu_plugin_add_security_attrs_bootguard_enabled(plugin, attrs);
	fu_plugin_add_security_attrs_bootguard_verified(plugin, attrs);
	fu_plugin_add_security_attrs_bootguard_acm(plugin, attrs);
	fu_plugin_add_security_attrs_bootguard_policy(plugin, attrs);
	fu_plugin_add_security_attrs_bootguard_otp(plugin, attrs);
}

static void
fu_plugin_add_security_attrs_mei_version(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(plugin);
	g_autofree gchar *version = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_plugin_security_attr_new(plugin, FWUPD_SECURITY_ATTR_ID_MEI_VERSION);
	fu_security_attrs_append(attrs, attr);

	/* not enabled */
	if (self->pci_device == NULL) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_MISSING_DATA);
		return;
	}

	/* format version as string */
	version = g_strdup_printf("%u:%u.%u.%u.%u",
				  self->vers.platform,
				  self->vers.major,
				  self->vers.minor,
				  self->vers.hotfix,
				  self->vers.buildno);
	if (self->issue == FU_MEI_ISSUE_UNKNOWN) {
		g_warning("ME family not supported for %s", version);
		return;
	}
	fwupd_security_attr_add_metadata(attr, "version", version);
	fwupd_security_attr_add_metadata(attr,
					 "kind",
					 fu_mei_common_family_to_string(self->family));

	/* Flash Descriptor Security Override Strap */
	if (self->issue == FU_MEI_ISSUE_VULNERABLE) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_pci_mei_plugin_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	/* only Intel */
	if (fu_cpu_get_vendor() != FU_CPU_VENDOR_INTEL)
		return;

	fu_plugin_add_security_attrs_manufacturing_mode(plugin, attrs);
	fu_plugin_add_security_attrs_override_strap(plugin, attrs);
	fu_plugin_add_security_attrs_bootguard(plugin, attrs);
	fu_plugin_add_security_attrs_mei_version(plugin, attrs);
}

static void
fu_pci_mei_plugin_init(FuPciMeiPlugin *self)
{
}

static void
fu_pci_mei_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "pci");
}

static void
fu_pci_mei_finalize(GObject *obj)
{
	FuPciMeiPlugin *self = FU_PCI_MEI_PLUGIN(obj);
	if (self->pci_device != NULL)
		g_object_unref(self->pci_device);
	G_OBJECT_CLASS(fu_pci_mei_plugin_parent_class)->finalize(obj);
}

static void
fu_pci_mei_plugin_class_init(FuPciMeiPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_pci_mei_finalize;
	plugin_class->constructed = fu_pci_mei_plugin_constructed;
	plugin_class->to_string = fu_pci_mei_plugin_to_string;
	plugin_class->add_security_attrs = fu_pci_mei_plugin_add_security_attrs;
	plugin_class->backend_device_added = fu_pci_mei_plugin_backend_device_added;
}
