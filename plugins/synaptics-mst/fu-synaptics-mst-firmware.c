/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

#define ADDR_CUSTOMER_ID_CARRERA 0x620E
#define ADDR_CUSTOMER_ID_CAYENNE 0x20E
#define ADDR_CUSTOMER_ID_TESLA	 0x10E
#define ADDR_CONFIG_CARRERA	 0x6200
#define ADDR_CONFIG_CAYENNE	 0x200
#define ADDR_CONFIG_TESLA	 0x100

guint16
fu_synaptics_mst_firmware_get_board_id(FuSynapticsMstFirmware *self)
{
	g_return_val_if_fail(FU_IS_SYNAPTICS_MST_FIRMWARE(self), 0);
	return self->board_id;
}

void
fu_synaptics_mst_firmware_set_family(FuSynapticsMstFirmware *self, FuSynapticsMstFamily family)
{
	g_return_if_fail(FU_IS_SYNAPTICS_MST_FIRMWARE(self));
	self->family = family;
}

static void
fu_synaptics_mst_firmware_export(FuFirmware *firmware,
				 FuFirmwareExportFlags flags,
				 XbBuilderNode *bn)
{
	FuSynapticsMstFirmware *self = FU_SYNAPTICS_MST_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "board_id", self->board_id);
	fu_xmlb_builder_insert_kv(bn, "family", fu_synaptics_mst_family_to_string(self->family));
}

static gboolean
fu_synaptics_mst_firmware_detect_family(FuSynapticsMstFirmware *self,
					GInputStream *stream,
					gsize offset,
					GError **error)
{
	guint16 addrs[] = {ADDR_CONFIG_TESLA, ADDR_CONFIG_CAYENNE, ADDR_CONFIG_CARRERA};
	for (guint i = 0; i < G_N_ELEMENTS(addrs); i++) {
		g_autoptr(GByteArray) st = NULL;
		st = fu_struct_synaptics_firmware_config_parse_stream(stream,
								      offset + addrs[i],
								      error);
		if (st == NULL)
			return FALSE;
		if ((fu_struct_synaptics_firmware_config_get_magic1(st) & 0x80) &&
		    (fu_struct_synaptics_firmware_config_get_magic2(st) & 0x80)) {
			self->family = fu_struct_synaptics_firmware_config_get_version(st) >> 4;
			return TRUE;
		}
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unable to autodetect chip family");
	return FALSE;
}

static gboolean
fu_synaptics_mst_firmware_parse(FuFirmware *firmware,
				GInputStream *stream,
				FwupdInstallFlags flags,
				GError **error)
{
	FuSynapticsMstFirmware *self = FU_SYNAPTICS_MST_FIRMWARE(firmware);
	guint16 addr;

	/* if device family not specified by caller, try to get from firmware file */
	if (self->family == FU_SYNAPTICS_MST_FAMILY_UNKNOWN) {
		if (!fu_synaptics_mst_firmware_detect_family(self, stream, 0x0, error))
			return FALSE;
	}

	switch (self->family) {
	case FU_SYNAPTICS_MST_FAMILY_TESLA:
	case FU_SYNAPTICS_MST_FAMILY_LEAF:
	case FU_SYNAPTICS_MST_FAMILY_PANAMERA:
		addr = ADDR_CUSTOMER_ID_TESLA;
		break;
	case FU_SYNAPTICS_MST_FAMILY_CAYENNE:
	case FU_SYNAPTICS_MST_FAMILY_SPYDER:
		addr = ADDR_CUSTOMER_ID_CAYENNE;
		break;
	case FU_SYNAPTICS_MST_FAMILY_CARRERA:
		addr = ADDR_CUSTOMER_ID_CARRERA;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported chip family %s",
			    fu_synaptics_mst_family_to_string(self->family));
		return FALSE;
	}
	if (!fu_input_stream_read_u16(stream, addr, &self->board_id, G_BIG_ENDIAN, error))
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
		addr = ADDR_CUSTOMER_ID_TESLA;
		break;
	case FU_SYNAPTICS_MST_FAMILY_CAYENNE:
	case FU_SYNAPTICS_MST_FAMILY_SPYDER:
		addr = ADDR_CUSTOMER_ID_CAYENNE;
		break;
	case FU_SYNAPTICS_MST_FAMILY_CARRERA:
		addr = ADDR_CUSTOMER_ID_CARRERA;
		break;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported chip family");
		return NULL;
	}
	/* assumed header */
	fu_byte_array_set_size(buf, addr + sizeof(guint16), 0x00);
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
fu_synaptics_mst_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuSynapticsMstFirmware *self = FU_SYNAPTICS_MST_FIRMWARE(firmware);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "board_id", NULL);
	if (tmp != G_MAXUINT64)
		self->board_id = tmp;
	tmp = xb_node_query_text_as_uint(n, "family", NULL);
	if (tmp != G_MAXUINT64)
		self->family = tmp;

	/* success */
	return TRUE;
}

static void
fu_synaptics_mst_firmware_init(FuSynapticsMstFirmware *self)
{
	self->family = FU_SYNAPTICS_MST_FAMILY_UNKNOWN;
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_synaptics_mst_firmware_class_init(FuSynapticsMstFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_synaptics_mst_firmware_parse;
	firmware_class->export = fu_synaptics_mst_firmware_export;
	firmware_class->write = fu_synaptics_mst_firmware_write;
	firmware_class->build = fu_synaptics_mst_firmware_build;
}

FuFirmware *
fu_synaptics_mst_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_SYNAPTICS_MST_FIRMWARE, NULL));
}
