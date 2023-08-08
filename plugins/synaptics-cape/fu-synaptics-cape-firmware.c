/*
 * Copyright (C) 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-cape-firmware.h"
#include "fu-synaptics-cape-struct.h"

typedef struct {
	guint16 vid;
	guint16 pid;
} FuSynapticsCapeFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuSynapticsCapeFirmware, fu_synaptics_cape_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_synaptics_cape_firmware_get_instance_private(o))

guint16
fu_synaptics_cape_firmware_get_vid(FuSynapticsCapeFirmware *self)
{
	FuSynapticsCapeFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), 0);
	return priv->vid;
}

void
fu_synaptics_cape_firmware_set_vid(FuSynapticsCapeFirmware *self, guint16 vid)
{
	FuSynapticsCapeFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self));
	priv->vid = vid;
}

guint16
fu_synaptics_cape_firmware_get_pid(FuSynapticsCapeFirmware *self)
{
	FuSynapticsCapeFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), 0);
	return priv->pid;
}

void
fu_synaptics_cape_firmware_set_pid(FuSynapticsCapeFirmware *self, guint16 pid)
{
	FuSynapticsCapeFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self));
	priv->pid = pid;
}

static void
fu_synaptics_cape_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);
	FuSynapticsCapeFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "vid", priv->vid);
	fu_xmlb_builder_insert_kx(bn, "pid", priv->pid);
}

static gboolean
fu_synaptics_cape_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);
	FuSynapticsCapeFirmwarePrivate *priv = GET_PRIVATE(self);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "vid", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		priv->vid = tmp;
	tmp = xb_node_query_text_as_uint(n, "pid", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		priv->pid = tmp;

	/* success */
	return TRUE;
}

static void
fu_synaptics_cape_firmware_init(FuSynapticsCapeFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_synaptics_cape_firmware_class_init(FuSynapticsCapeFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->export = fu_synaptics_cape_firmware_export;
	klass_firmware->build = fu_synaptics_cape_firmware_build;
}
