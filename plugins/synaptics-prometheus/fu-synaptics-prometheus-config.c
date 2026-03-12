/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-synaptics-prometheus-common.h"
#include "fu-synaptics-prometheus-config.h"
#include "fu-synaptics-prometheus-firmware.h"
#include "fu-synaptics-prometheus-struct.h"

struct _FuSynapticsPrometheusConfig {
	FuDevice parent_instance;
	guint32 configid1; /* config ID1 */
	guint32 configid2; /* config ID2 */
};

#define FU_SYNAPTICS_PROMETHEUS_CMD_IOTA_FIND_FLAGS_ALLIOTAS 0x0001 /* itype ignored*/
#define FU_SYNAPTICS_PROMETHEUS_CMD_IOTA_FIND_FLAGS_READMAX  0x0002 /* nbytes ignored */
#define FU_SYNAPTICS_PROMETHEUS_MAX_IOTA_READ_SIZE                                                 \
	(64 * 1024) /* max size of iota data returned                                              \
		     */

#define FU_SYNAPTICS_PROMETHEUS_IOTA_ITYPE_CONFIG_VERSION                                          \
	0x0009 /* Configuration id and version                                                     \
		*/

G_DEFINE_TYPE(FuSynapticsPrometheusConfig, fu_synaptics_prometheus_config, FU_TYPE_DEVICE)

static gboolean
fu_synaptics_prometheus_config_setup(FuDevice *device, GError **error)
{
	FuDevice *parent;
	FuSynapticsPrometheusConfig *self = FU_SYNAPTICS_PROMETHEUS_CONFIG(device);
	g_autofree gchar *configid1_str = NULL;
	g_autofree gchar *configid2_str = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) reply = NULL;
	g_autoptr(FuStructSynapticsPrometheusRequest) st_request =
	    fu_struct_synaptics_prometheus_request_new();
	g_autoptr(FuStructSynapticsPrometheusIotaConfigVersion) st_cfg = NULL;
	g_autoptr(FuStructSynapticsPrometheusReplyIotaFindHdr) st_hdr = NULL;
	g_autoptr(FuStructSynapticsPrometheusCmdIotaFind) st_cmd =
	    fu_struct_synaptics_prometheus_cmd_iota_find_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* get IOTA */
	fu_struct_synaptics_prometheus_cmd_iota_find_set_itype(
	    st_cmd,
	    FU_SYNAPTICS_PROMETHEUS_IOTA_ITYPE_CONFIG_VERSION);
	fu_struct_synaptics_prometheus_cmd_iota_find_set_flags(
	    st_cmd,
	    FU_SYNAPTICS_PROMETHEUS_CMD_IOTA_FIND_FLAGS_READMAX);
	fu_struct_synaptics_prometheus_request_set_cmd(st_request,
						       FU_SYNAPTICS_PROMETHEUS_CMD_IOTA_FIND);
	fu_byte_array_append_array(st_request->buf, st_cmd->buf);

	reply = fu_synaptics_prometheus_reply_new(
	    FU_STRUCT_SYNAPTICS_PROMETHEUS_REPLY_IOTA_FIND_HDR_SIZE +
	    FU_SYNAPTICS_PROMETHEUS_MAX_IOTA_READ_SIZE);
	parent = fu_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;
	if (!fu_synaptics_prometheus_device_cmd_send(FU_SYNAPTICS_PROMETHEUS_DEVICE(parent),
						     st_request->buf,
						     reply,
						     progress,
						     5000,
						     error))
		return FALSE;
	if (reply->len < FU_STRUCT_SYNAPTICS_PROMETHEUS_REPLY_IOTA_FIND_HDR_SIZE +
			     FU_STRUCT_SYNAPTICS_PROMETHEUS_IOTA_CONFIG_VERSION_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "CFG return data invalid size: 0x%04x",
			    reply->len);
		return FALSE;
	}
	st_hdr = fu_struct_synaptics_prometheus_reply_iota_find_hdr_parse(reply->data,
									  reply->len,
									  0x0,
									  error);
	if (st_hdr == NULL)
		return FALSE;
	if (fu_struct_synaptics_prometheus_reply_iota_find_hdr_get_itype(st_hdr) !=
	    FU_SYNAPTICS_PROMETHEUS_IOTA_ITYPE_CONFIG_VERSION) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "CFG iota had invalid itype: 0x%04x",
			    fu_struct_synaptics_prometheus_reply_iota_find_hdr_get_itype(st_hdr));
		return FALSE;
	}
	st_cfg = fu_struct_synaptics_prometheus_iota_config_version_parse(reply->data,
									  reply->len,
									  st_hdr->buf->len,
									  error);
	if (st_cfg == NULL)
		return FALSE;
	self->configid1 = fu_struct_synaptics_prometheus_iota_config_version_get_config_id1(st_cfg);
	self->configid2 = fu_struct_synaptics_prometheus_iota_config_version_get_config_id2(st_cfg);

	/* we should have made these a %08% guint32... */
	configid1_str = g_strdup_printf("%u", self->configid1);
	configid2_str = g_strdup_printf("%u", self->configid2);

	/* append the configid to the generated GUID */
	fu_device_add_instance_str(device, "CFG1", configid1_str);
	fu_device_add_instance_str(device, "CFG2", configid2_str);
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "CFG1", "CFG2", NULL))
		return FALSE;

	/* no downgrades are allowed */
	version =
	    g_strdup_printf("%04u",
			    fu_struct_synaptics_prometheus_iota_config_version_get_version(st_cfg));
	fu_device_set_version(FU_DEVICE(self), version);
	fu_device_set_version_lowest(FU_DEVICE(self), version);
	return TRUE;
}

static FuFirmware *
fu_synaptics_prometheus_config_prepare_firmware(FuDevice *device,
						GInputStream *stream,
						FuProgress *progress,
						FuFirmwareParseFlags flags,
						GError **error)
{
	FuSynapticsPrometheusConfig *self = FU_SYNAPTICS_PROMETHEUS_CONFIG(device);
	FuDevice *parent = fu_device_get_parent(device, error);
	g_autoptr(FuStructSynapticsPrometheusCfgHdr) st_hdr = NULL;
	g_autoptr(GInputStream) stream_hdr = NULL;
	g_autoptr(FuFirmware) firmware = fu_synaptics_prometheus_firmware_new();
	g_autoptr(FuFirmware) img_hdr = NULL;

	/* sanity check */
	if (parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no parent");
		return NULL;
	}

	if (fu_synaptics_prometheus_device_get_product_type(FU_SYNAPTICS_PROMETHEUS_DEVICE(
		parent)) == FU_SYNAPTICS_PROMETHEUS_PRODUCT_TYPE_TRITON) {
		if (!fu_synaptics_prometheus_firmware_set_signature_size(
			FU_SYNAPTICS_PROMETHEUS_FIRMWARE(firmware),
			FU_SYNAPTICS_PROMETHEUS_FIRMWARE_TRITON_SIGSIZE))
			return NULL;
	}

	/* parse the firmware */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	/* check the update header product and version */
	img_hdr = fu_firmware_get_image_by_id(firmware, "cfg-update-header", error);
	if (img_hdr == NULL)
		return NULL;
	stream_hdr = fu_firmware_get_stream(img_hdr, error);
	if (stream_hdr == NULL)
		return NULL;
	st_hdr = fu_struct_synaptics_prometheus_cfg_hdr_parse_stream(stream_hdr, 0x0, error);
	if (st_hdr == NULL) {
		g_prefix_error_literal(error, "CFG metadata is invalid: ");
		return NULL;
	}
	if (fu_struct_synaptics_prometheus_cfg_hdr_get_product(st_hdr) !=
	    FU_SYNAPTICS_PROMETHEUS_PRODUCT_PROMETHEUS) {
		if (flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_VID_PID) {
			g_warning("CFG metadata not compatible, "
				  "got 0x%02x expected 0x%02x",
				  fu_struct_synaptics_prometheus_cfg_hdr_get_product(st_hdr),
				  (guint)FU_SYNAPTICS_PROMETHEUS_PRODUCT_PROMETHEUS);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "CFG metadata not compatible, "
				    "got 0x%02x expected 0x%02x",
				    fu_struct_synaptics_prometheus_cfg_hdr_get_product(st_hdr),
				    (guint)FU_SYNAPTICS_PROMETHEUS_PRODUCT_PROMETHEUS);
			return NULL;
		}
	}
	if (fu_struct_synaptics_prometheus_cfg_hdr_get_id1(st_hdr) != self->configid1 ||
	    fu_struct_synaptics_prometheus_cfg_hdr_get_id2(st_hdr) != self->configid2) {
		if (flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_VID_PID) {
			g_warning("CFG version not compatible, "
				  "got %u:%u expected %u:%u",
				  fu_struct_synaptics_prometheus_cfg_hdr_get_id1(st_hdr),
				  fu_struct_synaptics_prometheus_cfg_hdr_get_id2(st_hdr),
				  self->configid1,
				  self->configid2);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "CFG version not compatible, "
				    "got %u:%u expected %u:%u",
				    fu_struct_synaptics_prometheus_cfg_hdr_get_id1(st_hdr),
				    fu_struct_synaptics_prometheus_cfg_hdr_get_id2(st_hdr),
				    self->configid1,
				    self->configid2);
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_synaptics_prometheus_config_write_firmware(FuDevice *device,
					      FuFirmware *firmware,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      GError **error)
{
	FuDevice *parent;
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_image_by_id_bytes(firmware, "cfg-update-payload", error);
	if (fw == NULL)
		return FALSE;

	/* I assume the CFG/MFW difference is detected in the device...*/
	parent = fu_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;
	return fu_synaptics_prometheus_device_write_fw(FU_SYNAPTICS_PROMETHEUS_DEVICE(parent),
						       fw,
						       progress,
						       error);
}

static void
fu_synaptics_prometheus_config_init(FuSynapticsPrometheusConfig *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.prometheus.config");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_logical_id(FU_DEVICE(self), "cfg");
	fu_device_set_name(FU_DEVICE(self), "IOTA Config");
	fu_device_set_summary(FU_DEVICE(self), "Fingerprint reader config");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_AUTH_FINGERPRINT);
}

static void
fu_synaptics_prometheus_config_constructed(GObject *obj)
{
	FuSynapticsPrometheusConfig *self = FU_SYNAPTICS_PROMETHEUS_CONFIG(obj);
	FuDevice *parent = fu_device_get_parent(FU_DEVICE(self), NULL);

	/* append the firmware kind to the generated GUID */
	if (parent != NULL) {
		g_autofree gchar *devid = NULL;
		devid = g_strdup_printf("USB\\VID_%04X&PID_%04X-cfg",
					fu_device_get_vid(parent),
					fu_device_get_pid(parent));
		fu_device_add_instance_id(FU_DEVICE(self), devid);
	}

	G_OBJECT_CLASS(fu_synaptics_prometheus_config_parent_class)->constructed(obj);
}

static gboolean
fu_synaptics_prometheus_config_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;
	return fu_device_attach_full(parent, progress, error);
}

static gboolean
fu_synaptics_prometheus_config_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;
	return fu_device_detach_full(parent, progress, error);
}

static void
fu_synaptics_prometheus_config_class_init(FuSynapticsPrometheusConfigClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_synaptics_prometheus_config_constructed;
	device_class->write_firmware = fu_synaptics_prometheus_config_write_firmware;
	device_class->prepare_firmware = fu_synaptics_prometheus_config_prepare_firmware;
	device_class->setup = fu_synaptics_prometheus_config_setup;
	device_class->reload = fu_synaptics_prometheus_config_setup;
	device_class->attach = fu_synaptics_prometheus_config_attach;
	device_class->detach = fu_synaptics_prometheus_config_detach;
}

FuSynapticsPrometheusConfig *
fu_synaptics_prometheus_config_new(FuSynapticsPrometheusDevice *device)
{
	FuSynapticsPrometheusConfig *self;
	self = g_object_new(FU_TYPE_SYNAPTICS_PROMETHEUS_CONFIG, "parent", device, NULL);
	return FU_SYNAPTICS_PROMETHEUS_CONFIG(self);
}
