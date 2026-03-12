/*
 * Copyright 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#ifdef HAVE_UTSNAME_H
#include <sys/utsname.h>
#endif

#include "fwupd-security-attr-private.h"

#include "fu-mem.h"
#include "fu-path.h"
#include "fu-processor-device.h"
#include "fu-string.h"

struct _FuProcessorDevice {
	FuDevice parent_instance;
	FuProcessorKind kind;
	FuProcessorFeatureFlags feature_flags;
	FuProcessorMitigationFlags mitigation_flags;
	guint32 sinkclose_microcode_ver;
};

G_DEFINE_TYPE(FuProcessorDevice, fu_processor_device, FU_TYPE_DEVICE)

static gboolean
fu_processor_device_has_feature(FuProcessorDevice *self, FuProcessorFeatureFlags flag)
{
	return (self->feature_flags & flag) > 0;
}

/**
 * fu_processor_device_get_kind:
 * @self: a #FuProcessorDevice
 *
 * Returns the CPU kind.
 *
 * Returns: kind, or %FU_PROCESSOR_KIND_UNKNOWN if invalid
 *
 * Since: 2.1.1
 **/
FuProcessorKind
fu_processor_device_get_kind(FuProcessorDevice *self)
{
	g_return_val_if_fail(FU_IS_PROCESSOR_DEVICE(self), FU_PROCESSOR_KIND_UNKNOWN);
	return self->kind;
}

/**
 * fu_processor_device_needs_mitigation:
 * @self: a #FuProcessorDevice
 * @mitigation_flag: a #FuProcessorMitigationFlags, e.g. %FU_PROCESSOR_MITIGATION_FLAG_GDS
 *
 * Returns if the CPU needs a specific mitigation.
 *
 * Returns: %TRUE if required
 *
 * Since: 2.1.1
 **/
gboolean
fu_processor_device_needs_mitigation(FuProcessorDevice *self,
				     FuProcessorMitigationFlags mitigation_flag)
{
	g_return_val_if_fail(FU_IS_PROCESSOR_DEVICE(self), FALSE);
	return (self->mitigation_flags & mitigation_flag) > 0;
}

/**
 * fu_processor_device_get_sinkclose_microcode_ver:
 * @self: a #FuProcessorDevice
 *
 * Returns the microcode version required to mitigate Sinkclose.
 *
 * Returns: version, or 0% if invalid
 *
 * Since: 2.1.1
 **/
guint32
fu_processor_device_get_sinkclose_microcode_ver(FuProcessorDevice *self)
{
	g_return_val_if_fail(FU_IS_PROCESSOR_DEVICE(self), 0);
	return self->sinkclose_microcode_ver;
}

static void
fu_processor_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuProcessorDevice *self = FU_PROCESSOR_DEVICE(device);
	if (self->kind != FU_PROCESSOR_KIND_UNKNOWN) {
		fwupd_codec_string_append(str,
					  idt,
					  "Kind",
					  fu_processor_kind_to_string(self->kind));
	}
	if (self->feature_flags != FU_PROCESSOR_FEATURE_FLAG_NONE) {
		g_autofree gchar *tmp = fu_processor_feature_flags_to_string(self->feature_flags);
		fwupd_codec_string_append(str, idt, "FeatureFlags", tmp);
	}
	if (self->mitigation_flags != FU_PROCESSOR_MITIGATION_FLAG_NONE) {
		g_autofree gchar *tmp =
		    fu_processor_mitigation_flags_to_string(self->mitigation_flags);
		fwupd_codec_string_append(str, idt, "MitigationFlags", tmp);
	}
	fwupd_codec_string_append_int(str,
				      idt,
				      "SinkcloseMicrocodeVer",
				      self->sinkclose_microcode_ver);
}

static gboolean
fu_processor_device_add_instance_ids(FuProcessorDevice *self, GError **error)
{
	guint32 eax = 0;
	guint32 family_id;
	guint32 family_id_ext;
	guint32 model_id;
	guint32 model_id_ext;
	guint32 processor_id;
	guint32 stepping_id;

	/* decode according to https://en.wikipedia.org/wiki/CPUID */
	if (!fu_cpuid(0x1, &eax, NULL, NULL, NULL, error))
		return FALSE;
	processor_id = (eax >> 12) & 0x3;
	model_id = (eax >> 4) & 0xf;
	family_id = (eax >> 8) & 0xf;
	model_id_ext = (eax >> 16) & 0xf;
	family_id_ext = (eax >> 20) & 0xff;
	stepping_id = eax & 0xf;

	/* use extended IDs where required */
	if (family_id == 6 || family_id == 15)
		model_id |= model_id_ext << 4;
	if (family_id == 15)
		family_id += family_id_ext;

	/* add GUIDs */
	fu_device_add_instance_u4(FU_DEVICE(self), "PRO", processor_id);
	fu_device_add_instance_u8(FU_DEVICE(self), "FAM", family_id);
	fu_device_add_instance_u8(FU_DEVICE(self), "MOD", model_id);
	fu_device_add_instance_u4(FU_DEVICE(self), "STP", stepping_id);
	fu_device_build_instance_id_full(FU_DEVICE(self),
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "CPUID",
					 "PRO",
					 "FAM",
					 NULL);
	fu_device_build_instance_id(FU_DEVICE(self), NULL, "CPUID", "PRO", "FAM", "MOD", NULL);
	fu_device_build_instance_id(FU_DEVICE(self),
				    NULL,
				    "CPUID",
				    "PRO",
				    "FAM",
				    "MOD",
				    "STP",
				    NULL);

	/* success */
	return TRUE;
}

static gboolean
fu_processor_device_probe_manufacturer_id(FuProcessorDevice *self, GError **error)
{
	guint32 ebx = 0;
	guint32 ecx = 0;
	guint32 edx = 0;
	gchar str[13] = {'\0'};
	if (!fu_cpuid(0x0, NULL, &ebx, &ecx, &edx, error))
		return FALSE;
	if (!fu_memcpy_safe((guint8 *)str,
			    sizeof(str),
			    0x0, /* dst */
			    (const guint8 *)&ebx,
			    sizeof(ebx),
			    0x0, /* src */
			    sizeof(guint32),
			    error))
		return FALSE;
	if (!fu_memcpy_safe((guint8 *)str,
			    sizeof(str),
			    0x4, /* dst */
			    (const guint8 *)&edx,
			    sizeof(edx),
			    0x0, /* src */
			    sizeof(guint32),
			    error))
		return FALSE;
	if (!fu_memcpy_safe((guint8 *)str,
			    sizeof(str),
			    0x8, /* dst */
			    (const guint8 *)&ecx,
			    sizeof(ecx),
			    0x0, /* src */
			    sizeof(guint32),
			    error))
		return FALSE;

	/* convert to something sane and get the quirked vendor name */
	fu_device_add_instance_strsafe(FU_DEVICE(self), "VEN", str);
	return fu_device_build_instance_id_full(FU_DEVICE(self),
						FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						error,
						"CPUID",
						"VEN",
						NULL);
}

static gboolean
fu_processor_device_probe_model(FuProcessorDevice *self, GError **error)
{
	guint32 eax = 0;
	guint32 ebx = 0;
	guint32 ecx = 0;
	guint32 edx = 0;
	gchar str[49] = {'\0'};

	for (guint32 i = 0; i < 3; i++) {
		if (!fu_cpuid(0x80000002 + i, &eax, &ebx, &ecx, &edx, error))
			return FALSE;
		if (!fu_memcpy_safe((guint8 *)str,
				    sizeof(str),
				    (16 * i) + 0x0, /* dst */
				    (const guint8 *)&eax,
				    sizeof(eax),
				    0x0, /* src */
				    sizeof(guint32),
				    error))
			return FALSE;
		if (!fu_memcpy_safe((guint8 *)str,
				    sizeof(str),
				    (16 * i) + 0x4, /* dst */
				    (const guint8 *)&ebx,
				    sizeof(ebx),
				    0x0, /* src */
				    sizeof(guint32),
				    error))
			return FALSE;
		if (!fu_memcpy_safe((guint8 *)str,
				    sizeof(str),
				    (16 * i) + 0x8, /* dst */
				    (const guint8 *)&ecx,
				    sizeof(ecx),
				    0x0, /* src */
				    sizeof(guint32),
				    error))
			return FALSE;
		if (!fu_memcpy_safe((guint8 *)str,
				    sizeof(str),
				    (16 * i) + 0xc, /* dst */
				    (const guint8 *)&edx,
				    sizeof(edx),
				    0x0, /* src */
				    sizeof(guint32),
				    error))
			return FALSE;
	}
	fu_device_set_name(FU_DEVICE(self), str);
	return TRUE;
}

static gboolean
fu_processor_device_probe_extended_features(FuProcessorDevice *self, GError **error)
{
	guint32 ebx = 0;
	guint32 ecx = 0;
	guint32 edx = 0;

	if (!fu_cpuid(0x7, NULL, &ebx, &ecx, &edx, error))
		return FALSE;
	if ((ebx >> 20) & 0x1)
		self->feature_flags |= FU_PROCESSOR_FEATURE_FLAG_SMAP;
	if ((ecx >> 7) & 0x1)
		self->feature_flags |= FU_PROCESSOR_FEATURE_FLAG_SHSTK;

	if (fu_cpu_get_vendor() == FU_CPU_VENDOR_INTEL) {
		if ((ecx >> 13) & 0x1)
			self->feature_flags |= FU_PROCESSOR_FEATURE_FLAG_TME;
		if ((edx >> 20) & 0x1)
			self->feature_flags |= FU_PROCESSOR_FEATURE_FLAG_IBT;
	}

	return TRUE;
}

static gboolean
fu_processor_device_probe(FuDevice *device, GError **error)
{
	FuProcessorDevice *self = FU_PROCESSOR_DEVICE(device);
	if (!fu_processor_device_probe_manufacturer_id(self, error))
		return FALSE;
	if (!fu_processor_device_probe_model(self, error))
		return FALSE;
	if (!fu_processor_device_probe_extended_features(self, error))
		return FALSE;
	if (!fu_processor_device_add_instance_ids(self, error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_processor_device_set_quirk_kv(FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	FuProcessorDevice *self = FU_PROCESSOR_DEVICE(device);
	if (g_strcmp0(key, "PciBcrAddr") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		fu_device_set_metadata_integer(device, "PciBcrAddr", tmp);
		return TRUE;
	}
	if (g_strcmp0(key, "ProcessorMitigationsRequired") == 0) {
		self->mitigation_flags = fu_processor_mitigation_flags_from_string(value);
		return TRUE;
	}
	if (g_strcmp0(key, "ProcessorKind") == 0) {
		self->kind = fu_processor_kind_from_string(value);
		return TRUE;
	}
	if (g_strcmp0(key, "ProcessorSinkcloseMicrocodeVersion") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_16, error))
			return FALSE;
		self->sinkclose_microcode_ver = (guint32)tmp;
		return TRUE;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no supported");
	return FALSE;
}

static void
fu_processor_device_add_security_attrs_cet_enabled(FuProcessorDevice *self, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_CET_ENABLED);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_SUPPORTED);
	fu_security_attrs_append(attrs, attr);

	switch (fu_cpu_get_vendor()) {
	case FU_CPU_VENDOR_INTEL:
		if (fu_processor_device_has_feature(self, FU_PROCESSOR_FEATURE_FLAG_SHSTK) &&
		    fu_processor_device_has_feature(self, FU_PROCESSOR_FEATURE_FLAG_IBT)) {
			fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
			return;
		}
		break;
	case FU_CPU_VENDOR_AMD:
		if (fu_processor_device_has_feature(self, FU_PROCESSOR_FEATURE_FLAG_SHSTK)) {
			fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
			return;
		}
		break;
	default:
		break;
	}

	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
}

static void
fu_processor_device_add_security_attrs_cet_active(FuProcessorDevice *self, FuSecurityAttrs *attrs)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	gint exit_status = 0xff;
	g_autofree gchar *toolfn = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(FwupdSecurityAttr) cet_plat_attr = NULL;
	g_autoptr(GError) error_local = NULL;

	/* check for CET */
	cet_plat_attr =
	    fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_CET_ENABLED, NULL);
	if (cet_plat_attr == NULL)
		return;
	if (!fwupd_security_attr_has_flag(cet_plat_attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS))
		return;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_CET_ACTIVE);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_SUPPORTED);
	fu_security_attrs_append(attrs, attr);

	/* check that userspace has been compiled for CET support */
	toolfn = fu_context_build_filename(ctx,
					   &error_local,
					   FU_PATH_KIND_LIBEXECDIR_PKG,
					   "fwupd-detect-cet",
					   NULL);
	if (toolfn == NULL) {
		g_warning("failed to test CET: %s", error_local->message);
		return;
	}
	if (!g_spawn_command_line_sync(toolfn, NULL, NULL, &exit_status, &error_local)) {
		g_warning("failed to test CET: %s", error_local->message);
		return;
	}
	if (!g_spawn_check_wait_status(exit_status, &error_local)) {
		g_debug("CET does not function, not supported: %s", error_local->message);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_processor_device_add_security_attrs_intel_tme(FuProcessorDevice *self, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	/* check for TME */
	if (!fu_processor_device_has_feature(self, FU_PROCESSOR_FEATURE_FLAG_TME)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_processor_device_add_security_attrs_smap(FuProcessorDevice *self, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_SMAP);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	fu_security_attrs_append(attrs, attr);

	/* check for SMEP and SMAP */
	if (!fu_processor_device_has_feature(self, FU_PROCESSOR_FEATURE_FLAG_SMAP)) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_SUPPORTED);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

#ifdef HAVE_UTSNAME_H
static void
fu_processor_device_add_x86_64_security_attrs(FuProcessorDevice *self, FuSecurityAttrs *attrs)
{
	/* only Intel */
	if (fu_cpu_get_vendor() == FU_CPU_VENDOR_INTEL)
		fu_processor_device_add_security_attrs_intel_tme(self, attrs);
	fu_processor_device_add_security_attrs_cet_enabled(self, attrs);
	fu_processor_device_add_security_attrs_cet_active(self, attrs);
	fu_processor_device_add_security_attrs_smap(self, attrs);
}
#endif

static void
fu_processor_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
#ifdef HAVE_UTSNAME_H
	FuProcessorDevice *self = FU_PROCESSOR_DEVICE(device);
	struct utsname name_tmp = {0};

	if (uname(&name_tmp) < 0) {
		g_warning("failed to read CPU architecture");
		return;
	}

	if (g_strcmp0(name_tmp.machine, "x86_64") == 0)
		fu_processor_device_add_x86_64_security_attrs(self, attrs);
#endif
}

static gchar *
fu_processor_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_processor_device_init(FuProcessorDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_CPU);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_COMPUTER);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_physical_id(FU_DEVICE(self), "cpu:0");
}

static void
fu_processor_device_class_init(FuProcessorDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_processor_device_to_string;
	device_class->probe = fu_processor_device_probe;
	device_class->set_quirk_kv = fu_processor_device_set_quirk_kv;
	device_class->add_security_attrs = fu_processor_device_add_security_attrs;
	device_class->convert_version = fu_processor_device_convert_version;
}

/**
 * fu_processor_device_new:
 * @ctx: a #FuContext
 *
 * Creates a new #FuProcessorDevice.
 *
 * Returns: (transfer full): a #FuProcessorDevice
 *
 * Since: 2.1.1
 **/
FuProcessorDevice *
fu_processor_device_new(FuContext *ctx)
{
	return g_object_new(FU_TYPE_PROCESSOR_DEVICE, "context", ctx, NULL);
}
