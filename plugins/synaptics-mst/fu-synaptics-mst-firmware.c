/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-mst-common.h"
#include "fu-synaptics-mst-firmware.h"

struct _FuSynapticsMstFirmware {
	FuFirmwareClass parent_instance;
	guint16 board_id;
	FuSynapticsMstFamily family;
};

G_DEFINE_TYPE(FuSynapticsMstFirmware, fu_synaptics_mst_firmware, FU_TYPE_FIRMWARE)

#define ADDR_CUSTOMER_ID_CAYENNE 0X20E
#define ADDR_CUSTOMER_ID 0X10E

guint16
fu_synaptics_mst_firmware_get_board_id(FuSynapticsMstFirmware *self)
{
	g_return_val_if_fail(FU_IS_SYNAPTICS_MST_FIRMWARE(self), 0);
	return self->board_id;
}

static void
fu_synaptics_mst_firmware_export(FuFirmware *firmware,
				 FuFirmwareExportFlags flags,
				 XbBuilderNode *bn)
{
	FuSynapticsMstFirmware *self = FU_SYNAPTICS_MST_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "board_id", self->board_id);
}

static gboolean
fu_synaptics_mst_firmware_parse(FuFirmware *firmware,
				GBytes *fw,
				gsize offset,
				FwupdInstallFlags flags,
				GError **error)
{
	FuSynapticsMstFirmware *self = FU_SYNAPTICS_MST_FIRMWARE(firmware);
	const guint8 *buf;
	gsize bufsz;
	guint16 addr;
	switch (self->family) {
	case FU_SYNAPTICS_MST_FAMILY_TESLA:
	case FU_SYNAPTICS_MST_FAMILY_LEAF:
	case FU_SYNAPTICS_MST_FAMILY_PANAMERA:
		addr = ADDR_CUSTOMER_ID;
		break;
	case FU_SYNAPTICS_MST_FAMILY_CAYENNE:
	case FU_SYNAPTICS_MST_FAMILY_SPYDER:
		addr = ADDR_CUSTOMER_ID_CAYENNE;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Unsupported chip family");
		return FALSE;
	}
	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    addr,
				    &self->board_id,
				    G_BIG_ENDIAN,
				    error))
		return FALSE;

	/* success */
	return TRUE;
}

static GByteArray *
fu_synaptics_mst_firmware_write(FuFirmware *firmware, GError **error)
{
	FuSynapticsMstFirmware *self = FU_SYNAPTICS_MST_FIRMWARE(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;

	guint16 addr;
	switch (self->family) {
	case FU_SYNAPTICS_MST_FAMILY_TESLA:
	case FU_SYNAPTICS_MST_FAMILY_LEAF:
	case FU_SYNAPTICS_MST_FAMILY_PANAMERA:
		addr = ADDR_CUSTOMER_ID;
		break;
	case FU_SYNAPTICS_MST_FAMILY_CAYENNE:
	case FU_SYNAPTICS_MST_FAMILY_SPYDER:
		addr = ADDR_CUSTOMER_ID_CAYENNE;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Unsupported chip family");
		return FALSE;
	}
	/* assumed header */
	fu_byte_array_set_size(buf, ADDR_CUSTOMER_ID + sizeof(guint16), 0x00);
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     addr,
				     fu_firmware_get_idx(firmware),
				     G_BIG_ENDIAN,
				     error))
		return NULL;

	/* payload */
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob);

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_synaptics_rmi_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuSynapticsMstFirmware *self = FU_SYNAPTICS_MST_FIRMWARE(firmware);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "board_id", NULL);
	if (tmp != G_MAXUINT64)
		self->board_id = tmp;

	/* success */
	return TRUE;
}

static void
fu_synaptics_mst_firmware_init(FuSynapticsMstFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_synaptics_mst_firmware_class_init(FuSynapticsMstFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_synaptics_mst_firmware_parse;
	klass_firmware->export = fu_synaptics_mst_firmware_export;
	klass_firmware->write = fu_synaptics_mst_firmware_write;
	klass_firmware->build = fu_synaptics_rmi_firmware_build;
}

FuFirmware *
fu_synaptics_mst_firmware_new(void)
{
	FuSynapticsMstFirmware *self = g_object_new(FU_TYPE_SYNAPTICS_MST_FIRMWARE, NULL);
	/* default chip family as Tesla */
	self->family = FU_SYNAPTICS_MST_FAMILY_TESLA;
	return FU_FIRMWARE(self);
}

void
fu_synaptics_mst_firmware_set_family(FuSynapticsMstFirmware *self, guint8 family)
{
	self->family = (FuSynapticsMstFamily)family;
}
