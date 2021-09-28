/*
 * Copyright (C) 2021 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-genesys-scaler-firmware.h"

struct _FuGenesysScalerFirmware {
	FuFirmwareClass parent_instance;
	FuGenesysMtkFooter footer;
	guint protect_sector_addr[2];
	gsize protect_sector_size[2];
	guint public_key_addr;
	gsize public_key_size;
	guint addr;
};

G_DEFINE_TYPE(FuGenesysScalerFirmware, fu_genesys_scaler_firmware, FU_TYPE_FIRMWARE)

void
fu_genesys_scaler_firmware_decrypt(guint8 *buf, gsize bufsz)
{
	const gchar *key = "mstar";
	const gsize keylen = strlen(key);

	for (guint i = 0; i < bufsz; i++)
		buf[i] ^= key[i % keylen];
}

static gboolean
fu_genesys_scaler_firmware_parse(FuFirmware *firmware,
				 GBytes *fw,
				 guint64 addr_start,
				 guint64 addr_end,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuGenesysScalerFirmware *self = FU_GENESYS_SCALER_FIRMWARE(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	buf = g_bytes_get_data(fw, &bufsz);
	if (!fu_memcpy_safe((guint8 *)&self->footer,
			    sizeof(self->footer),
			    0, /* dst */
			    buf,
			    bufsz,
			    bufsz - sizeof(self->footer), /* src */
			    sizeof(self->footer),
			    error))
		return FALSE;
	fu_genesys_scaler_firmware_decrypt((guint8 *)&self->footer, sizeof(self->footer));
	if (memcmp(self->footer.data.header.default_head,
		   MTK_RSA_HEADER,
		   sizeof(self->footer.data.header.default_head)) != 0) {
		g_autofree gchar *str = NULL;
		str = fu_common_strsafe((const gchar *)self->footer.data.header.default_head,
					sizeof(self->footer.data.header.default_head));
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "invalid footer, expected %s, and got %s",
			    MTK_RSA_HEADER,
			    str);
		return FALSE;
	}

	if (self->footer.data.header.configuration_setting.bits.second_image) {
		if (!fu_common_read_uint32_safe(
			self->footer.data.header.second_image_program_addr,
			sizeof(self->footer.data.header.second_image_program_addr),
			0,
			&self->addr,
			G_LITTLE_ENDIAN,
			error))
			return FALSE;
	}
	if (self->footer.data.header.configuration_setting.bits.decrypt_mode) {
		if (!fu_common_read_uint32_safe(
			self->footer.data.header.scaler_public_key_addr,
			sizeof(self->footer.data.header.scaler_public_key_addr),
			0,
			&self->public_key_addr,
			G_LITTLE_ENDIAN,
			error))
			return FALSE;
		self->public_key_size = 0x1000;
	}

	if (self->footer.data.header.configuration_setting.bits.special_protect_sector) {
		if (self->footer.data.header.protect_sector[0].area.size) {
			self->protect_sector_addr[0] =
			    (self->footer.data.header.protect_sector[0].area.addr_high << 16) |
			    (self->footer.data.header.protect_sector[0].area.addr_low[1] << 8) |
			    (self->footer.data.header.protect_sector[0].area.addr_low[0]);
			self->protect_sector_addr[0] *= 0x1000;
			self->protect_sector_size[0] =
			    self->footer.data.header.protect_sector[0].area.size * 0x1000;
		}

		if (self->footer.data.header.protect_sector[1].area.size) {
			self->protect_sector_addr[1] =
			    (self->footer.data.header.protect_sector[1].area.addr_high << 16) |
			    (self->footer.data.header.protect_sector[1].area.addr_low[1] << 8) |
			    (self->footer.data.header.protect_sector[1].area.addr_low[0]);
			self->protect_sector_addr[1] *= 0x1000;
			self->protect_sector_size[1] =
			    self->footer.data.header.protect_sector[1].area.size * 0x1000;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_genesys_scaler_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuGenesysScalerFirmware *self = FU_GENESYS_SCALER_FIRMWARE(firmware);

	if (self->footer.data.header.model_name[0] != '\0') {
		fu_xmlb_builder_insert_kv(bn,
					  "model_name",
					  (const gchar *)self->footer.data.header.model_name);
	}
	if (self->footer.data.header.scaler_group[0] != '\0') {
		fu_xmlb_builder_insert_kv(bn,
					  "scaler_group",
					  (const gchar *)self->footer.data.header.scaler_group);
	}
	if (self->footer.data.header.panel_type[0] != '\0') {
		fu_xmlb_builder_insert_kv(bn,
					  "panel_type",
					  (const gchar *)self->footer.data.header.panel_type);
	}
	if (self->footer.data.header.scaler_packet_date[0] != '\0') {
		fu_xmlb_builder_insert_kv(
		    bn,
		    "scaler_packet_date",
		    (const gchar *)self->footer.data.header.scaler_packet_date);
	}
	if (self->footer.data.header.scaler_packet_version[0] != '\0') {
		fu_xmlb_builder_insert_kv(
		    bn,
		    "scaler_packet_version",
		    (const gchar *)self->footer.data.header.scaler_packet_version);
	}
	fu_xmlb_builder_insert_kx(bn,
				  "configuration_setting",
				  self->footer.data.header.configuration_setting.r8);

	if (self->footer.data.header.configuration_setting.bits.second_image)
		fu_xmlb_builder_insert_kx(bn, "second_image_program_addr", self->addr);

	if (self->footer.data.header.configuration_setting.bits.decrypt_mode) {
		gchar N[0x200 + 1] = {'\0'};
		gchar E[0x006 + 1] = {'\0'};

		fu_xmlb_builder_insert_kx(bn, "public_key_addr", self->public_key_addr);
		fu_xmlb_builder_insert_kx(bn, "public_key_size", self->public_key_size);

		memcpy(N, self->footer.data.public_key.N + 4, sizeof(N) - 1);
		fu_xmlb_builder_insert_kv(bn, "N", N);

		memcpy(E, self->footer.data.public_key.E + 4, sizeof(E) - 1);
		fu_xmlb_builder_insert_kv(bn, "E", E);
	}

	if (self->footer.data.header.configuration_setting.bits.special_protect_sector) {
		if (self->protect_sector_size[0]) {
			fu_xmlb_builder_insert_kx(bn,
						  "protect_sector_addr0",
						  self->protect_sector_addr[0]);
			fu_xmlb_builder_insert_kx(bn,
						  "protect_sector_size0",
						  self->protect_sector_size[0]);
		}

		if (self->protect_sector_size[1]) {
			fu_xmlb_builder_insert_kx(bn,
						  "protect_sector_addr1",
						  self->protect_sector_addr[1]);
			fu_xmlb_builder_insert_kx(bn,
						  "protect_sector_size1",
						  self->protect_sector_size[1]);
		}
	}

	if (self->footer.data.header.configuration_setting.bits.boot_code_size_in_header) {
		fu_xmlb_builder_insert_kx(bn,
					  "boot_code_size",
					  self->footer.data.header.boot_code_size);
	}
	fu_xmlb_builder_insert_kx(bn, "addr", self->addr);
}

static gboolean
fu_genesys_scaler_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuGenesysScalerFirmware *self = FU_GENESYS_SCALER_FIRMWARE(firmware);
	const gchar *tmp;

	/* optional properties */
	tmp = xb_node_query_text(n, "model_name", NULL);
	if (tmp != NULL) {
		if (!fu_memcpy_safe((guint8 *)&self->footer.data.header.model_name,
				    sizeof(self->footer.data.header.model_name),
				    0x0, /* dst */
				    (const guint8 *)tmp,
				    strlen(tmp),
				    0x0, /* src */
				    strlen(tmp),
				    error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_genesys_scaler_firmware_write(FuFirmware *firmware, GError **error)
{
	FuGenesysScalerFirmware *self = FU_GENESYS_SCALER_FIRMWARE(firmware);
	FuGenesysMtkFooter footer = {0x0};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;

	/* payload */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob);

	/* "encrypted" footer */
	if (!fu_memcpy_safe((guint8 *)&footer,
			    sizeof(footer),
			    0, /* dst */
			    (guint8 *)&self->footer,
			    sizeof(self->footer),
			    0, /* src */
			    sizeof(footer),
			    error))
		return NULL;
	if (!fu_memcpy_safe((guint8 *)&footer.data.header.default_head,
			    sizeof(footer.data.header.default_head),
			    0, /* dst */
			    (guint8 *)&MTK_RSA_HEADER,
			    strlen(MTK_RSA_HEADER),
			    0, /* src */
			    strlen(MTK_RSA_HEADER),
			    error))
		return NULL;
	fu_genesys_scaler_firmware_decrypt((guint8 *)&footer, sizeof(footer));
	g_byte_array_append(buf, (const guint8 *)&footer, sizeof(footer));

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_genesys_scaler_firmware_init(FuGenesysScalerFirmware *self)
{
}

static void
fu_genesys_scaler_firmware_class_init(FuGenesysScalerFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_genesys_scaler_firmware_parse;
	klass_firmware->export = fu_genesys_scaler_firmware_export;
	klass_firmware->build = fu_genesys_scaler_firmware_build;
	klass_firmware->write = fu_genesys_scaler_firmware_write;
}

FuFirmware *
fu_genesys_scaler_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GENESYS_SCALER_FIRMWARE, NULL));
}
