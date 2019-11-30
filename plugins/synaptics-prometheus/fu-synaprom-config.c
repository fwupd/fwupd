/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-synaprom-common.h"
#include "fu-synaprom-config.h"
#include "fu-synaprom-firmware.h"

struct _FuSynapromConfig {
	FuDevice		 parent_instance;
	guint32			 configid1;		/* config ID1 */
	guint32			 configid2;		/* config ID2 */
};

/* Iotas can exceed the size of available RAM in the part.
 * In order to allow the host to read them the IOTA_FIND command supports
 * transferring iotas with multiple commands */
typedef struct __attribute__((packed)) {
	guint16			 itype;			/* type of iotas to find */
	guint16			 flags;			/* flags, see below */
	guint8			 maxniotas;		/* maximum number of iotas to return, 0 = unlimited */
	guint8			 firstidx;		/* first index of iotas to return */
	guint8			 dummy[2];
	guint32			 offset;		/* byte offset of data to return */
	guint32			 nbytes;		/* maximum number of bytes to return */
} FuSynapromCmdIotaFind;

/* this is followed by a chain of iotas, as follows */
typedef struct __attribute__((packed)) {
	guint16		 status;
	guint32		 fullsize;
	guint16		 nbytes;
	guint16		 itype;
} FuSynapromReplyIotaFindHdr;

/* this iota contains the configuration id and version */
typedef struct __attribute__((packed)) {
	guint32		 config_id1;	/* YYMMDD */
	guint32		 config_id2;	/* HHMMSS */
	guint16		 version;
	guint16		 unused[3];
} FuSynapromIotaConfigVersion;

#define FU_SYNAPROM_CMD_IOTA_FIND_FLAGS_ALLIOTAS	0x0001	/* itype ignored*/
#define FU_SYNAPROM_CMD_IOTA_FIND_FLAGS_READMAX		0x0002	/* nbytes ignored */
#define FU_SYNAPROM_MAX_IOTA_READ_SIZE			(64 * 1024) /* max size of iota data returned */

#define FU_SYNAPROM_IOTA_ITYPE_CONFIG_VERSION		0x0009	/* Configuration id and version */

G_DEFINE_TYPE (FuSynapromConfig, fu_synaprom_config, FU_TYPE_DEVICE)

static gboolean
fu_synaprom_config_setup (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	FuSynapromConfig *self = FU_SYNAPROM_CONFIG (device);
	FuSynapromCmdIotaFind cmd = { 0x0 };
	FuSynapromIotaConfigVersion cfg;
	FuSynapromReplyIotaFindHdr hdr;
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) reply = NULL;
	g_autoptr(GByteArray) request = NULL;

	/* get IOTA */
	cmd.itype = GUINT16_TO_LE((guint16)FU_SYNAPROM_IOTA_ITYPE_CONFIG_VERSION);
	cmd.flags = GUINT16_TO_LE((guint16)FU_SYNAPROM_CMD_IOTA_FIND_FLAGS_READMAX);
	request = fu_synaprom_request_new (FU_SYNAPROM_CMD_IOTA_FIND, &cmd, sizeof(cmd));
	reply = fu_synaprom_reply_new (sizeof(FuSynapromReplyIotaFindHdr) + FU_SYNAPROM_MAX_IOTA_READ_SIZE);
	if (!fu_synaprom_device_cmd_send (FU_SYNAPROM_DEVICE (parent),
					  request, reply, 5000, error))
		return FALSE;
	if (reply->len < sizeof(hdr) + sizeof(cfg)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "CFG return data invalid size: 0x%04x",
			     reply->len);
		return FALSE;
	}
	memcpy (&hdr, reply->data, sizeof(hdr));
	if (GUINT32_FROM_LE(hdr.itype) != FU_SYNAPROM_IOTA_ITYPE_CONFIG_VERSION) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "CFG iota had invalid itype: 0x%04x",
			     GUINT32_FROM_LE(hdr.itype));
		return FALSE;
	}
	if (!fu_memcpy_safe ((guint8 *) &cfg, sizeof(cfg), 0x0,		/* dst */
			     reply->data, reply->len, sizeof(hdr),	/* src */
			     sizeof(cfg), error))
		return FALSE;
	self->configid1 = GUINT32_FROM_LE(cfg.config_id1);
	self->configid2 = GUINT32_FROM_LE(cfg.config_id2);
	g_debug ("id1=%u, id2=%u, ver=%u",
		 self->configid1, self->configid2,
		 GUINT16_FROM_LE(cfg.version));

	/* no downgrades are allowed */
	version = g_strdup_printf ("%04u", GUINT16_FROM_LE(cfg.version));
	fu_device_set_version (FU_DEVICE (self), version, FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_version_lowest (FU_DEVICE (self), version);
	return TRUE;
}

static FuFirmware *
fu_synaprom_config_prepare_firmware (FuDevice *device,
				     GBytes *fw,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuSynapromConfig *self = FU_SYNAPROM_CONFIG (device);
	FuSynapromFirmwareCfgHeader hdr;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuFirmware) firmware = fu_synaprom_firmware_new ();
	guint32 product;
	guint32 id1;

	/* parse the firmware */
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	/* check the update header product and version */
	blob = fu_firmware_get_image_by_id_bytes (firmware, "cfg-update-header", error);
	if (blob == NULL)
		return NULL;
	if (g_bytes_get_size (blob) != sizeof(hdr)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "CFG metadata is invalid");
		return NULL;
	}
	memcpy (&hdr, g_bytes_get_data (blob, NULL), sizeof(hdr));
	product = GUINT32_FROM_LE(hdr.product);
	if (product != FU_SYNAPROM_PRODUCT_PROMETHEUS) {
		if (flags & FWUPD_INSTALL_FLAG_FORCE) {
			g_warning ("CFG metadata not compatible, "
				   "got 0x%02x expected 0x%02x",
				   product, (guint) FU_SYNAPROM_PRODUCT_PROMETHEUS);
		} else {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "CFG metadata not compatible, "
				     "got 0x%02x expected 0x%02x",
				     product, (guint) FU_SYNAPROM_PRODUCT_PROMETHEUS);
			return NULL;
		}
	}
	id1 = GUINT32_FROM_LE(hdr.id1);
	if (id1 != self->configid1) {
		if (flags & FWUPD_INSTALL_FLAG_FORCE) {
			g_warning ("CFG version not compatible, "
				   "got %u expected %u",
				   id1, self->configid1);
		} else {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "CFG version not compatible, "
				     "got %u expected %u",
				     id1, self->configid1);
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer (&firmware);
}

static gboolean
fu_synaprom_config_write_firmware (FuDevice *device,
				   FuFirmware *firmware,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	g_autoptr(GBytes) fw = NULL;

	/* get default image */
	fw = fu_firmware_get_image_by_id_bytes (firmware, "cfg-update-payload", error);
	if (fw == NULL)
		return FALSE;

	/* I assume the CFG/MFW difference is detected in the device...*/
	return fu_synaprom_device_write_fw (FU_SYNAPROM_DEVICE (parent), fw, error);
}

static void
fu_synaprom_config_init (FuSynapromConfig *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.synaptics.prometheus.config");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_logical_id (FU_DEVICE (self), "cfg");
	fu_device_set_name (FU_DEVICE (self), "Prometheus IOTA Config");
}

static void
fu_synaprom_config_constructed (GObject *obj)
{
	FuSynapromConfig *self = FU_SYNAPROM_CONFIG (obj);
	FuDevice *parent = fu_device_get_parent (FU_DEVICE (self));
	g_autofree gchar *devid = NULL;

	/* append the firmware kind to the generated GUID */
	devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X-cfg",
				 fu_usb_device_get_vid (FU_USB_DEVICE (parent)),
				 fu_usb_device_get_pid (FU_USB_DEVICE (parent)));
	fu_device_add_instance_id (FU_DEVICE (self), devid);

	G_OBJECT_CLASS (fu_synaprom_config_parent_class)->constructed (obj);
}

static gboolean
fu_synaprom_config_open (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	return fu_device_open (parent, error);
}

static gboolean
fu_synaprom_config_close (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	return fu_device_close (parent, error);
}

static gboolean
fu_synaprom_config_attach (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	return fu_device_attach (parent, error);
}

static gboolean
fu_synaprom_config_detach (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	return fu_device_detach (parent, error);
}

static void
fu_synaprom_config_flags_notify_cb (FuDevice *parent, GParamSpec *pspec, FuDevice *device)
{
	fu_device_incorporate_flag (device, parent, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
}

static void
fu_synaprom_config_class_init (FuSynapromConfigClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = fu_synaprom_config_constructed;
	klass_device->write_firmware = fu_synaprom_config_write_firmware;
	klass_device->prepare_firmware = fu_synaprom_config_prepare_firmware;
	klass_device->open = fu_synaprom_config_open;
	klass_device->close = fu_synaprom_config_close;
	klass_device->setup = fu_synaprom_config_setup;
	klass_device->reload = fu_synaprom_config_setup;
	klass_device->attach = fu_synaprom_config_attach;
	klass_device->detach = fu_synaprom_config_detach;
}

FuSynapromConfig *
fu_synaprom_config_new (FuSynapromDevice *device)
{
	FuSynapromConfig *self;
	self = g_object_new (FU_TYPE_SYNAPROM_CONFIG,
			     "parent", device,
			     NULL);

	/* mirror the bootloader flag on the parent to the child */
	if (fu_device_has_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	g_signal_connect (device, "notify::flags",
			  G_CALLBACK (fu_synaprom_config_flags_notify_cb), self);
	return FU_SYNAPROM_CONFIG (self);
}
