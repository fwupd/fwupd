/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-goodixtp-common.h"
#include "fu-goodixtp-firmware.h"

#define MAX_CHUNK_NUM 80

struct FuGoodixChunkInfo {
	guint8 type;
	guint32 flash_addr;
};

typedef struct {
	FuFirmwareClass parent_instance;
	guint32 version;
	guint8 *fw_data;
	gint fw_len;
	gint index;
	struct FuGoodixChunkInfo chunk_info[MAX_CHUNK_NUM];
} FuGoodixtpFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuGoodixtpFirmware, fu_goodixtp_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_goodixtp_firmware_get_instance_private(o))

guint32
fu_goodixtp_firmware_get_version(FuGoodixtpFirmware *self)
{
	FuGoodixtpFirmwarePrivate *priv = GET_PRIVATE(self);
	return priv->version;
}

void
fu_goodixtp_firmware_set_version(FuGoodixtpFirmware *self, guint32 version)
{
	FuGoodixtpFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->version = version;
}

guint8 *
fu_goodixtp_firmware_get_data(FuGoodixtpFirmware *self)
{
	FuGoodixtpFirmwarePrivate *priv = GET_PRIVATE(self);
	return priv->fw_data;
}

gint
fu_goodixtp_firmware_get_len(FuGoodixtpFirmware *self)
{
	FuGoodixtpFirmwarePrivate *priv = GET_PRIVATE(self);
	return priv->fw_len;
}

guint32
fu_goodixtp_firmware_get_addr(FuGoodixtpFirmware *self, gint index)
{
	FuGoodixtpFirmwarePrivate *priv = GET_PRIVATE(self);
	return priv->chunk_info[index].flash_addr;
}

void
fu_goodixtp_add_chunk_data(FuGoodixtpFirmware *self,
			   guint8 type,
			   guint32 addr,
			   guint8 *data,
			   gint dataLen)
{
	FuGoodixtpFirmwarePrivate *priv = GET_PRIVATE(self);
	if (dataLen > RAM_BUFFER_SIZE)
		return;

	memcpy(priv->fw_data + priv->fw_len, data, dataLen);
	priv->fw_len += RAM_BUFFER_SIZE;
	priv->chunk_info[priv->index].type = type;
	priv->chunk_info[priv->index].flash_addr = addr;
	priv->index++;
}

static void
fu_goodixtp_firmware_init(FuGoodixtpFirmware *self)
{
	FuGoodixtpFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->fw_data = g_malloc0(RAM_BUFFER_SIZE * MAX_CHUNK_NUM);
}

static void
fu_goodixtp_firmware_finalize(GObject *object)
{
	FuGoodixtpFirmware *self = FU_GOODIXTP_FIRMWARE(object);
	FuGoodixtpFirmwarePrivate *priv = GET_PRIVATE(self);

	g_free(priv->fw_data);
	G_OBJECT_CLASS(fu_goodixtp_firmware_parent_class)->finalize(object);
}

static void
fu_goodixtp_firmware_class_init(FuGoodixtpFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_goodixtp_firmware_finalize;
}
