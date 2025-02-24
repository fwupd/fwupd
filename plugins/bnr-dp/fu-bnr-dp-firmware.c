/*
 * Copyright 2024 B&R Industrial Automation GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-bnr-dp-common.h"
#include "fu-bnr-dp-firmware.h"
#include "fu-bnr-dp-struct.h"

struct _FuBnrDpFirmware {
	FuFirmwareClass parent_instance;

	/* mandatory XML header attributes, not part of payload. additionally, "Ver" (version) is
	 * also mandatory */
	guint64 device_id;	  /* Dev */
	gchar *usage;		  /* Use */
	gchar function;		  /* Fct */
	guint64 variant;	  /* Var */
	guint64 payload_length;	  /* Len */
	guint16 payload_checksum; /* Chk */
	gchar *material;	  /* Mat */
	gchar *creation_date;	  /* Date (nullable) */
	gchar *comment;		  /* Rem (nullable) */
};

G_DEFINE_TYPE(FuBnrDpFirmware, fu_bnr_dp_firmware, FU_TYPE_FIRMWARE)

static void
fu_bnr_dp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuBnrDpFirmware *self = FU_BNR_DP_FIRMWARE(firmware);

	fu_xmlb_builder_insert_kx(bn, "device_id", self->device_id);
	fu_xmlb_builder_insert_kv(bn, "usage", self->usage);
	fu_xmlb_builder_insert_kx(bn, "function", self->function);
	fu_xmlb_builder_insert_kx(bn, "variant", self->variant);
	fu_xmlb_builder_insert_kx(bn, "payload_length", self->payload_length);
	fu_xmlb_builder_insert_kx(bn, "payload_checksum", self->payload_checksum);
	fu_xmlb_builder_insert_kv(bn, "material", self->material);
	fu_xmlb_builder_insert_kv(bn, "creation_date", self->creation_date);
	fu_xmlb_builder_insert_kv(bn, "comment", self->comment);
}

static gchar *
fu_bnr_dp_firmware_convert_version(FuFirmware *self, guint64 version_raw)
{
	return fu_bnr_dp_version_to_string(version_raw);
}

static gboolean
fu_bnr_dp_firmware_attribute_parse_u64(XbNode *root,
				       const gchar *attribute,
				       guint64 *value,
				       GError **error)
{
	*value = xb_node_get_attr_as_uint(root, attribute);
	if (*value == G_MAXUINT64) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "missing or invalid header attribute: '%s'",
			    attribute);
		return FALSE;
	}

	return TRUE;
}

static gchar *
fu_bnr_dp_firmware_attribute_parse_string(XbNode *root, const gchar *attribute, GError **error)
{
	const gchar *value;

	value = xb_node_get_attr(root, attribute);
	if (value == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "missing or invalid header attribute: '%s'",
			    attribute);
		return NULL;
	}

	return g_strdup(value);
}

static gboolean
fu_bnr_dp_firmware_header_parse(FuBnrDpFirmware *self, XbSilo *silo, GError **error)
{
	guint64 tmp_u64 = 0;
	g_autofree gchar *chk_str = NULL;
	g_autofree gchar *fct_str = NULL;
	g_autoptr(XbNode) root = NULL;

	root = xb_silo_get_root(silo);
	if (root == NULL || g_strcmp0(xb_node_get_element(root), "Firmware") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid or missing firmware header element");
		return FALSE;
	}

	if (!fu_bnr_dp_firmware_attribute_parse_u64(root, "Dev", &self->device_id, error))
		return FALSE;

	if (!fu_bnr_dp_firmware_attribute_parse_u64(root, "Ver", &tmp_u64, error))
		return FALSE;
	fu_firmware_set_version_raw(FU_FIRMWARE(self), tmp_u64);

	self->usage = fu_bnr_dp_firmware_attribute_parse_string(root, "Use", error);
	if (self->usage == NULL)
		return FALSE;
	if (g_strcmp0(self->usage, "fw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported usage string in XML header: '%s'",
			    self->usage);
		return FALSE;
	}

	fct_str = fu_bnr_dp_firmware_attribute_parse_string(root, "Fct", error);
	if (fct_str == NULL)
		return FALSE;
	if (strlen(fct_str) != 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported Fct: '%s'",
			    fct_str);
		return FALSE;
	}

	/* function compatibility check */
	self->function = fct_str[0];
	if (self->function != '_') {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unexpected function (Fct) value in XML header: '%c' (0x%hX)",
			    self->function,
			    self->function);
		return FALSE;
	}

	if (!fu_bnr_dp_firmware_attribute_parse_u64(root, "Var", &self->variant, error))
		return FALSE;
	if (!fu_bnr_dp_firmware_attribute_parse_u64(root, "Len", &self->payload_length, error))
		return FALSE;

	chk_str = fu_bnr_dp_firmware_attribute_parse_string(root, "Chk", error);
	if (chk_str == NULL)
		return FALSE;
	if (!fu_strtoull(chk_str, &tmp_u64, 0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
		return FALSE;
	self->payload_checksum = (guint16)tmp_u64;

	self->material = fu_bnr_dp_firmware_attribute_parse_string(root, "Mat", error);
	if (self->material == NULL)
		return FALSE;

	/* these are optional and may be NULL */
	self->creation_date = fu_bnr_dp_firmware_attribute_parse_string(root, "Date", NULL);
	self->comment = fu_bnr_dp_firmware_attribute_parse_string(root, "Rem", NULL);

	/* success */
	return TRUE;
}

static guint16
fu_bnr_dp_firmware_checksum_finish(guint16 csum)
{
	return ~csum + 1;
}

static gboolean
fu_bnr_dp_firmware_stream_checksum(GInputStream *stream, guint16 *csum, GError **error)
{
	if (!fu_input_stream_compute_sum16(stream, csum, error))
		return FALSE;
	*csum = fu_bnr_dp_firmware_checksum_finish(*csum);
	return TRUE;
}

static guint16
fu_bnr_dp_firmware_buf_checksum(const guint8 *buf, gsize bufsz)
{
	return fu_bnr_dp_firmware_checksum_finish(fu_sum16(buf, bufsz));
}

static gboolean
fu_bnr_dp_firmware_payload_parse(FuBnrDpFirmware *self,
				 GInputStream *stream,
				 gsize payload_offset,
				 GError **error)
{
	gsize streamsz = 0;
	guint16 xml_checksum = 0;
	guint16 crc = G_MAXUINT16;
	g_autoptr(GInputStream) payload_stream = NULL;

	payload_stream = fu_partial_input_stream_new(stream, payload_offset, G_MAXSIZE, error);
	if (payload_stream == NULL)
		return FALSE;

	if (!fu_input_stream_size(payload_stream, &streamsz, error))
		return FALSE;
	if (self->payload_length != streamsz) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "unexpected firmware payload length (header specified: %" G_GUINT64_FORMAT
		    ", actual: %" G_GSIZE_FORMAT ")",
		    self->payload_length,
		    streamsz);
		return FALSE;
	}
	if (streamsz != FU_BNR_DP_FIRMWARE_SIZE) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "unexpected firmware payload length (must be: %d, actual: %" G_GSIZE_FORMAT ")",
		    FU_BNR_DP_FIRMWARE_SIZE,
		    streamsz);
		return FALSE;
	}

	/* the XML header has a simple sum checksum for the payload */
	if (!fu_bnr_dp_firmware_stream_checksum(payload_stream, &xml_checksum, error))
		return FALSE;
	if (self->payload_checksum != xml_checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "checksum mismatch in firmware payload (XML header specified: 0x%X, "
			    "actual: 0x%X)",
			    self->payload_checksum,
			    xml_checksum);
		return FALSE;
	}

	/* we can do a CRC16 check on this type of payload as well */
	if (!fu_input_stream_compute_crc16(payload_stream, FU_CRC_KIND_B16_BNR, &crc, error))
		return FALSE;
	if (crc != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "CRC mismatch in firmware payload: 0x%X",
			    crc);
		return FALSE;
	}

	/* discard the XML header and keep only the payload */
	if (!fu_firmware_set_stream(FU_FIRMWARE(self), payload_stream, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_bnr_dp_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuBnrDpFirmware *self = FU_BNR_DP_FIRMWARE(firmware);
	guint8 byte = 0;
	guint8 header_separator[] = {0x0};
	gsize separator_idx = 0;
	g_autoptr(GBytes) header = NULL;
	g_autoptr(XbBuilderSource) builder_source = xb_builder_source_new();
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbSilo) silo = NULL;

	if (!fu_input_stream_read_u8(stream, 0, &byte, error))
		return FALSE;

	/* find the index of the first null byte, indicating the end of the XML header */
	if (!fu_input_stream_find(stream,
				  header_separator,
				  sizeof(header_separator),
				  &separator_idx,
				  error))
		return FALSE;

	/* read XML header */
	header = fu_input_stream_read_bytes(stream, 0, separator_idx, NULL, error);
	if (header == NULL)
		return FALSE;
	if (!xb_builder_source_load_bytes(builder_source,
					  header,
					  XB_BUILDER_SOURCE_FLAG_NONE,
					  error))
		return FALSE;
	xb_builder_import_source(builder, builder_source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_SINGLE_ROOT, NULL, error);
	if (silo == NULL)
		return FALSE;
	if (!fu_bnr_dp_firmware_header_parse(self, silo, error))
		return FALSE;
	if (!fu_bnr_dp_firmware_payload_parse(self, stream, separator_idx + 1, error))
		return FALSE;

	/* success */
	return TRUE;
}

/* set FuBnrDpFirmware private data to information from device */
gboolean
fu_bnr_dp_firmware_parse_from_device(FuBnrDpFirmware *self,
				     const FuStructBnrDpFactoryData *st_factory_data,
				     const FuStructBnrDpPayloadHeader *st_fw_header,
				     GError **error)
{
	guint64 version = 0;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GDateTime) now = g_date_time_new_now_local();

	bytes = fu_firmware_get_bytes_with_patches(FU_FIRMWARE(self), error);
	if (bytes == NULL)
		return FALSE;

	self->device_id = fu_bnr_dp_effective_product_num(st_factory_data);
	self->usage = g_strdup("fw");
	self->function = '_';
	self->variant = fu_bnr_dp_effective_compat_id(st_factory_data);
	self->payload_length = g_bytes_get_size(bytes);
	self->payload_checksum =
	    fu_bnr_dp_firmware_buf_checksum(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
	self->material = fu_struct_bnr_dp_factory_data_get_identification(st_factory_data);
	self->creation_date = g_date_time_format(now, "%d.%m.%Y");
	self->comment = g_strdup("created by " PACKAGE_NAME " " PACKAGE_VERSION);

	if (!fu_bnr_dp_version_from_header(st_fw_header, &version, error))
		return FALSE;
	fu_firmware_set_version_raw(FU_FIRMWARE(self), version);

	return TRUE;
}

static GByteArray *
fu_bnr_dp_firmware_write(FuFirmware *firmware, GError **error)
{
	FuBnrDpFirmware *self = FU_BNR_DP_FIRMWARE(firmware);
	g_autofree gchar *xml = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) payload = NULL;
	g_autoptr(XbBuilderNode) bn = NULL;

	g_autofree gchar *device_id = g_strdup_printf("%" G_GUINT64_FORMAT, self->device_id);
	g_autofree gchar *version =
	    g_strdup_printf("%" G_GUINT64_FORMAT, fu_firmware_get_version_raw(firmware));
	g_autofree gchar *function = g_strdup_printf("%c", self->function);
	g_autofree gchar *variant = g_strdup_printf("%" G_GUINT64_FORMAT, self->variant);
	g_autofree gchar *payload_length =
	    g_strdup_printf("%" G_GUINT64_FORMAT, self->payload_length);
	g_autofree gchar *payload_checksum = g_strdup_printf("0x%X", self->payload_checksum);

	bn = xb_builder_node_insert(NULL,
				    "Firmware",
				    "Dev",
				    device_id,
				    "Ver",
				    version,
				    "Use",
				    self->usage,
				    "Fct",
				    function,
				    "Var",
				    variant,
				    "Len",
				    payload_length,
				    "Chk",
				    payload_checksum,
				    "Mat",
				    self->material,
				    "Date",
				    self->creation_date,
				    "Rem",
				    self->comment,
				    NULL);
	if (bn == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to build firmware XML header");
		return NULL;
	}
	xml = xb_builder_node_export(bn, XB_NODE_EXPORT_FLAG_NONE, error);
	if (xml == NULL)
		return NULL;

	/* start with xml header including null byte */
	g_byte_array_append(buf, (guint8 *)xml, strlen(xml) + 1);

	/* append payload after null byte */
	payload = fu_firmware_get_bytes_with_patches(firmware, error);
	if (payload == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, payload);

	/* success */
	return g_steal_pointer(&buf);
}

/* add firmware patch that increments the boot counter embedded in the firmware */
gboolean
fu_bnr_dp_firmware_patch_boot_counter(FuBnrDpFirmware *self,
				      guint32 active_boot_counter,
				      GError **error)
{
	guint16 crc;
	g_autoptr(FuStructBnrDpPayloadHeader) st_header = NULL;
	g_autoptr(GBytes) image = NULL;
	g_autoptr(GBytes) patch = NULL;

	/* practically impossible under normal conditions, would indicate some form of corruption.
	 * could technically be worked around by resetting the active boot counter */
	if (active_boot_counter == G_MAXUINT32) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "update count exhausted");
		return FALSE;
	}

	image = fu_firmware_get_bytes(FU_FIRMWARE(self), error);
	st_header = fu_struct_bnr_dp_payload_header_parse(g_bytes_get_data(image, NULL),
							  g_bytes_get_size(image),
							  FU_BNR_DP_FIRMWARE_HEADER_OFFSET,
							  error);
	if (st_header == NULL)
		return FALSE;

	/* check that the current CRC was correct */
	crc = fu_crc16(FU_CRC_KIND_B16_BNR,
		       st_header->data,
		       FU_STRUCT_BNR_DP_PAYLOAD_HEADER_SIZE - sizeof(crc));
	if (fu_struct_bnr_dp_payload_header_get_crc(st_header) != crc) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "CRC mismatch in firmware binary header (header specified: 0x%X, actual: 0x%X)",
		    fu_struct_bnr_dp_payload_header_get_crc(st_header),
		    crc);
		return FALSE;
	}

	/* set new counter */
	g_info("incrementing boot counter: %u => %u", active_boot_counter, active_boot_counter + 1);
	fu_struct_bnr_dp_payload_header_set_counter(st_header, active_boot_counter + 1);

	/* clear CRC error flag if set for some reason */
	fu_struct_bnr_dp_payload_header_set_flags(
	    st_header,
	    fu_struct_bnr_dp_payload_header_get_flags(st_header) &
		~FU_BNR_DP_PAYLOAD_FLAGS_CRC_ERROR);

	/* update checksum */
	crc = fu_crc16(FU_CRC_KIND_B16_BNR,
		       st_header->data,
		       FU_STRUCT_BNR_DP_PAYLOAD_HEADER_SIZE - sizeof(crc));

	fu_struct_bnr_dp_payload_header_set_crc(st_header, crc);

	patch = g_bytes_new(st_header->data, st_header->len);
	fu_firmware_add_patch(FU_FIRMWARE(self), FU_BNR_DP_FIRMWARE_HEADER_OFFSET, patch);

	return TRUE;
}

/* do checks that can only be done with data from an opened device */
gboolean
fu_bnr_dp_firmware_check(FuBnrDpFirmware *self,
			 const FuStructBnrDpFactoryData *st_factory_data,
			 const FuStructBnrDpPayloadHeader *st_active_header,
			 const FuStructBnrDpPayloadHeader *st_fw_header,
			 FwupdInstallFlags flags,
			 GError **error)
{
	guint64 active_version = 0;
	guint64 fw_version = 0;
	guint32 product_num;
	guint16 compat_id;
	g_autofree gchar *fw_version_str = NULL;

	/* compare versions */
	if (!fu_bnr_dp_version_from_header(st_active_header, &active_version, error))
		return FALSE;
	if (!fu_bnr_dp_version_from_header(st_fw_header, &fw_version, error))
		return FALSE;
	fw_version_str = fu_bnr_dp_version_to_string(fw_version);
	if (fu_firmware_get_version_raw(FU_FIRMWARE(self)) != fw_version) {
		if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "versions in firmware XML header (%s) and binary payload (%s) "
				    "are inconsistent",
				    fu_firmware_get_version(FU_FIRMWARE(self)),
				    fw_version_str);
			return FALSE;
		}
		g_warning("forcing installation of firmware with inconsistent XML header (%s) and "
			  "binary payload (%s) versions",
			  fu_firmware_get_version(FU_FIRMWARE(self)),
			  fw_version_str);
	}

	/* check for compatibility of device/firmware combination. customized products use separate
	 * product numbers but set the parent product number to the original stock product. since
	 * these customizations are generally mechanical, they shall not make the firmware
	 * incompatible */
	product_num = fu_bnr_dp_effective_product_num(st_factory_data);
	if (product_num != G_MAXUINT32 && product_num != self->device_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware file is not for a compatible device (expected id: 0x%X, "
			    "received id: 0x%" G_GUINT64_FORMAT "X)",
			    product_num,
			    self->device_id);
		return FALSE;
	}

	/* variant compatibility check, similar to device id check */
	compat_id = fu_bnr_dp_effective_compat_id(st_factory_data);
	if (compat_id != G_MAXUINT16 && compat_id != self->variant) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware file is not for a compatible variant (expected: 0x%X, "
			    "received: 0x%" G_GUINT64_FORMAT "X)",
			    compat_id,
			    self->variant);
		return FALSE;
	}

	return TRUE;
}

static void
fu_bnr_dp_firmware_init(FuBnrDpFirmware *self)
{
}

static void
fu_bnr_dp_firmware_finalize(GObject *object)
{
	FuBnrDpFirmware *self = FU_BNR_DP_FIRMWARE(object);

	g_free(self->usage);
	g_free(self->material);
	g_free(self->creation_date);
	g_free(self->comment);

	G_OBJECT_CLASS(fu_bnr_dp_firmware_parent_class)->finalize(object);
}

static void
fu_bnr_dp_firmware_class_init(FuBnrDpFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);

	object_class->finalize = fu_bnr_dp_firmware_finalize;

	firmware_class->convert_version = fu_bnr_dp_firmware_convert_version;
	firmware_class->export = fu_bnr_dp_firmware_export;
	firmware_class->parse = fu_bnr_dp_firmware_parse;
	firmware_class->write = fu_bnr_dp_firmware_write;
}

FuFirmware *
fu_bnr_dp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_BNR_DP_FIRMWARE, NULL));
}
