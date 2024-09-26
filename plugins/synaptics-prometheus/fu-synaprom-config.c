/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-synaprom-common.h"
#include "fu-synaprom-config.h"
#include "fu-synaprom-firmware.h"
#include "fu-synaprom-struct.h"

struct _FuSynapromConfig {
	FuDevice parent_instance;
	guint32 configid1; /* config ID1 */
	guint32 configid2; /* config ID2 */
};

#define FU_SYNAPROM_CMD_IOTA_FIND_FLAGS_ALLIOTAS 0x0001	     /* itype ignored*/
#define FU_SYNAPROM_CMD_IOTA_FIND_FLAGS_READMAX	 0x0002	     /* nbytes ignored */
#define FU_SYNAPROM_MAX_IOTA_READ_SIZE		 (64 * 1024) /* max size of iota data returned */

#define FU_SYNAPROM_IOTA_ITYPE_CONFIG_VERSION 0x0009 /* Configuration id and version */

G_DEFINE_TYPE(FuSynapromConfig, fu_synaprom_config, FU_TYPE_DEVICE)

static gboolean
fu_synaprom_config_setup(FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	FuSynapromConfig *self = FU_SYNAPROM_CONFIG(device);
	g_autofree gchar *configid1_str = NULL;
	g_autofree gchar *configid2_str = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) reply = NULL;
	g_autoptr(GByteArray) request = NULL;
	g_autoptr(GByteArray) st_cfg = NULL;
	g_autoptr(GByteArray) st_hdr = NULL;
	g_autoptr(GByteArray) st_cmd = fu_struct_synaprom_cmd_iota_find_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* get IOTA */
	fu_struct_synaprom_cmd_iota_find_set_itype(st_cmd, FU_SYNAPROM_IOTA_ITYPE_CONFIG_VERSION);
	fu_struct_synaprom_cmd_iota_find_set_flags(st_cmd, FU_SYNAPROM_CMD_IOTA_FIND_FLAGS_READMAX);
	request = fu_synaprom_request_new(FU_SYNAPROM_CMD_IOTA_FIND, st_cmd->data, st_cmd->len);
	reply = fu_synaprom_reply_new(FU_STRUCT_SYNAPROM_REPLY_IOTA_FIND_HDR_SIZE +
				      FU_SYNAPROM_MAX_IOTA_READ_SIZE);
	if (!fu_synaprom_device_cmd_send(FU_SYNAPROM_DEVICE(parent),
					 request,
					 reply,
					 progress,
					 5000,
					 error))
		return FALSE;
	if (reply->len < FU_STRUCT_SYNAPROM_REPLY_IOTA_FIND_HDR_SIZE +
			     FU_STRUCT_SYNAPROM_IOTA_CONFIG_VERSION_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "CFG return data invalid size: 0x%04x",
			    reply->len);
		return FALSE;
	}
	st_hdr = fu_struct_synaprom_reply_iota_find_hdr_parse(reply->data, reply->len, 0x0, error);
	if (st_hdr == NULL)
		return FALSE;
	if (fu_struct_synaprom_reply_iota_find_hdr_get_itype(st_hdr) !=
	    FU_SYNAPROM_IOTA_ITYPE_CONFIG_VERSION) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "CFG iota had invalid itype: 0x%04x",
			    fu_struct_synaprom_reply_iota_find_hdr_get_itype(st_hdr));
		return FALSE;
	}
	st_cfg = fu_struct_synaprom_iota_config_version_parse(reply->data,
							      reply->len,
							      st_hdr->len,
							      error);
	if (st_cfg == NULL)
		return FALSE;
	self->configid1 = fu_struct_synaprom_iota_config_version_get_config_id1(st_cfg);
	self->configid2 = fu_struct_synaprom_iota_config_version_get_config_id2(st_cfg);

	/* we should have made these a %08% uint32_t... */
	configid1_str = g_strdup_printf("%u", self->configid1);
	configid2_str = g_strdup_printf("%u", self->configid2);

	/* append the configid to the generated GUID */
	fu_device_add_instance_str(device, "CFG1", configid1_str);
	fu_device_add_instance_str(device, "CFG2", configid2_str);
	if (!fu_device_build_instance_id(device, error, "USB", "VID", "PID", "CFG1", "CFG2", NULL))
		return FALSE;

	/* no downgrades are allowed */
	version =
	    g_strdup_printf("%04u", fu_struct_synaprom_iota_config_version_get_version(st_cfg));
	fu_device_set_version(FU_DEVICE(self), version);
	fu_device_set_version_lowest(FU_DEVICE(self), version);
	return TRUE;
}

static FuFirmware *
fu_synaprom_config_prepare_firmware(FuDevice *device,
				    GInputStream *stream,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuSynapromConfig *self = FU_SYNAPROM_CONFIG(device);
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(GByteArray) st_hdr = NULL;
	g_autoptr(GInputStream) stream_hdr = NULL;
	g_autoptr(FuFirmware) firmware = fu_synaprom_firmware_new();
	g_autoptr(FuFirmware) img_hdr = NULL;

	if (fu_synaprom_device_get_product_type(FU_SYNAPROM_DEVICE(parent)) ==
	    FU_SYNAPROM_PRODUCT_TYPE_TRITON) {
		if (!fu_synaprom_firmware_set_signature_size(FU_SYNAPROM_FIRMWARE(firmware),
							     FU_SYNAPROM_FIRMWARE_TRITON_SIGSIZE))
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
	st_hdr = fu_struct_synaprom_cfg_hdr_parse_stream(stream_hdr, 0x0, error);
	if (st_hdr == NULL) {
		g_prefix_error(error, "CFG metadata is invalid: ");
		return NULL;
	}
	if (fu_struct_synaprom_cfg_hdr_get_product(st_hdr) != FU_SYNAPROM_PRODUCT_PROMETHEUS) {
		if (flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) {
			g_warning("CFG metadata not compatible, "
				  "got 0x%02x expected 0x%02x",
				  fu_struct_synaprom_cfg_hdr_get_product(st_hdr),
				  (guint)FU_SYNAPROM_PRODUCT_PROMETHEUS);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "CFG metadata not compatible, "
				    "got 0x%02x expected 0x%02x",
				    fu_struct_synaprom_cfg_hdr_get_product(st_hdr),
				    (guint)FU_SYNAPROM_PRODUCT_PROMETHEUS);
			return NULL;
		}
	}
	if (fu_struct_synaprom_cfg_hdr_get_id1(st_hdr) != self->configid1 ||
	    fu_struct_synaprom_cfg_hdr_get_id2(st_hdr) != self->configid2) {
		if (flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) {
			g_warning("CFG version not compatible, "
				  "got %u:%u expected %u:%u",
				  fu_struct_synaprom_cfg_hdr_get_id1(st_hdr),
				  fu_struct_synaprom_cfg_hdr_get_id2(st_hdr),
				  self->configid1,
				  self->configid2);
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "CFG version not compatible, "
				    "got %u:%u expected %u:%u",
				    fu_struct_synaprom_cfg_hdr_get_id1(st_hdr),
				    fu_struct_synaprom_cfg_hdr_get_id2(st_hdr),
				    self->configid1,
				    self->configid2);
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_synaprom_config_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_image_by_id_bytes(firmware, "cfg-update-payload", error);
	if (fw == NULL)
		return FALSE;

	/* I assume the CFG/MFW difference is detected in the device...*/
	return fu_synaprom_device_write_fw(FU_SYNAPROM_DEVICE(parent), fw, progress, error);
}

static void
fu_synaprom_config_init(FuSynapromConfig *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.synaptics.prometheus.config");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_logical_id(FU_DEVICE(self), "cfg");
	fu_device_set_name(FU_DEVICE(self), "Prometheus IOTA Config");
	fu_device_set_summary(FU_DEVICE(self), "Fingerprint reader config");
	fu_device_add_icon(FU_DEVICE(self), "auth-fingerprint");
}

static void
fu_synaprom_config_constructed(GObject *obj)
{
	FuSynapromConfig *self = FU_SYNAPROM_CONFIG(obj);
	FuDevice *parent = fu_device_get_parent(FU_DEVICE(self));
	g_autofree gchar *devid = NULL;

	/* append the firmware kind to the generated GUID */
	devid = g_strdup_printf("USB\\VID_%04X&PID_%04X-cfg",
				fu_device_get_vid(parent),
				fu_device_get_pid(parent));
	fu_device_add_instance_id(FU_DEVICE(self), devid);

	G_OBJECT_CLASS(fu_synaprom_config_parent_class)->constructed(obj);
}

static gboolean
fu_synaprom_config_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	return fu_device_attach_full(parent, progress, error);
}

static gboolean
fu_synaprom_config_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	return fu_device_detach_full(parent, progress, error);
}

static void
fu_synaprom_config_class_init(FuSynapromConfigClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_synaprom_config_constructed;
	device_class->write_firmware = fu_synaprom_config_write_firmware;
	device_class->prepare_firmware = fu_synaprom_config_prepare_firmware;
	device_class->setup = fu_synaprom_config_setup;
	device_class->reload = fu_synaprom_config_setup;
	device_class->attach = fu_synaprom_config_attach;
	device_class->detach = fu_synaprom_config_detach;
}

FuSynapromConfig *
fu_synaprom_config_new(FuSynapromDevice *device)
{
	FuSynapromConfig *self;
	self = g_object_new(FU_TYPE_SYNAPROM_CONFIG, "parent", device, NULL);
	return FU_SYNAPROM_CONFIG(self);
}
