/*
 * Copyright 2021 Quectel Wireless Solutions Co., Ltd.
 *                    Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-firehose-device.h"
#include "fu-qc-firehose-struct.h"

#define FU_QC_FIREHOSE_DEVICE_LOADED_FIREHOSE "loaded-firehose"
#define FU_QC_FIREHOSE_DEVICE_NO_ZLP	    "no-zlp"

#define FU_QC_FIREHOSE_DEVICE_RAW_BUFFER_SIZE (4 * 1024)

#define FU_QC_FIREHOSE_DEVICE_TIMEOUT_MS 500

struct _FuQcFirehoseDevice {
	FuUsbDevice parent_instance;
	guint8 ep_in;
	guint8 ep_out;
	gsize maxpktsize_in;
	gsize maxpktsize_out;
	guint64 max_payload_size;
	FuQcFirehoseFunctions supported_functions;
	gboolean rawmode;
};

G_DEFINE_TYPE(FuQcFirehoseDevice, fu_qc_firehose_device, FU_TYPE_USB_DEVICE)

static void
fu_qc_firehose_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuQcFirehoseDevice *self = FU_QC_FIREHOSE_DEVICE(device);
	g_autofree gchar *functions = fu_qc_firehose_functions_to_string(self->supported_functions);
	fwupd_codec_string_append_hex(str, idt, "EpIn", self->ep_in);
	fwupd_codec_string_append_hex(str, idt, "EpOut", self->ep_out);
	fwupd_codec_string_append_hex(str, idt, "MaxpktsizeIn", self->maxpktsize_in);
	fwupd_codec_string_append_hex(str, idt, "MaxpktsizeOut", self->maxpktsize_out);
	fwupd_codec_string_append_hex(str, idt, "MaxPayloadSize", self->max_payload_size);
	fwupd_codec_string_append(str, idt, "SupportedFunctions", functions);
	fwupd_codec_string_append_bool(str, idt, "RawMode", self->rawmode);
}

static GByteArray *
fu_qc_firehose_device_read(FuQcFirehoseDevice *self, guint timeout_ms, GError **error)
{
	gsize actual_len = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	fu_byte_array_set_size(buf, FU_QC_FIREHOSE_DEVICE_RAW_BUFFER_SIZE, 0x00);
	if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
					 self->ep_in,
					 buf->data,
					 buf->len,
					 &actual_len,
					 timeout_ms,
					 NULL,
					 error)) {
		g_prefix_error(error, "failed to do bulk transfer (read): ");
		return NULL;
	}

	g_byte_array_set_size(buf, actual_len);
	fu_dump_raw(G_LOG_DOMAIN, "rx packet", buf->data, buf->len);
	return g_steal_pointer(&buf);
}

static gboolean
fu_qc_firehose_device_write(FuQcFirehoseDevice *self, const guint8 *data, gsize sz, GError **error)
{
	gsize actual_len = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GByteArray) bufmut = g_byte_array_sized_new(sz);

	/* copy const data to mutable GByteArray */
	g_byte_array_append(bufmut, data, sz);
	chunks = fu_chunk_array_mutable_new(bufmut->data, bufmut->len, 0, 0, self->maxpktsize_out);
	if (chunks->len > 1)
		g_debug("split into %u chunks", chunks->len);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);

		fu_dump_raw(G_LOG_DOMAIN,
			    "tx packet",
			    fu_chunk_get_data(chk),
			    fu_chunk_get_data_sz(chk));
		if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
						 self->ep_out,
						 fu_chunk_get_data_out(chk),
						 fu_chunk_get_data_sz(chk),
						 &actual_len,
						 FU_QC_FIREHOSE_DEVICE_TIMEOUT_MS,
						 NULL,
						 error)) {
			g_prefix_error(error, "failed to do bulk transfer (write data): ");
			return FALSE;
		}
		if (actual_len != fu_chunk_get_data_sz(chk)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "only wrote %" G_GSIZE_FORMAT "bytes",
				    actual_len);
			return FALSE;
		}
	}

	/* sent zlp packet if needed */
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_QC_FIREHOSE_DEVICE_NO_ZLP) &&
	    sz % self->maxpktsize_out == 0) {
		if (!fu_usb_device_bulk_transfer(FU_USB_DEVICE(self),
						 self->ep_out,
						 NULL,
						 0,
						 NULL,
						 FU_QC_FIREHOSE_DEVICE_TIMEOUT_MS,
						 NULL,
						 error)) {
			g_prefix_error(error, "failed to do bulk transfer (write zlp): ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_qc_firehose_device_parse_log_text(FuQcFirehoseDevice *self, const gchar *text)
{
	if (text == NULL)
		return;
	if (g_str_has_prefix(text, "Supported Functions: ")) {
		g_auto(GStrv) split = g_strsplit(text + 21, " ", -1);
		for (guint i = 0; split[i] != NULL; i++)
			self->supported_functions |= fu_qc_firehose_functions_from_string(split[i]);
	}
}

static gboolean
fu_qc_firehose_device_read_xml_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuQcFirehoseDevice *self = FU_QC_FIREHOSE_DEVICE(device);
	const gchar *tmp;
	g_autofree gchar *xml = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GPtrArray) xn_logs = NULL;
	g_autoptr(XbNode) xn_data = NULL;
	g_autoptr(XbNode) xn_response = NULL;
	g_autoptr(XbSilo) silo = NULL;

	buf = fu_qc_firehose_device_read(self, FU_QC_FIREHOSE_DEVICE_TIMEOUT_MS, error);
	if (buf == NULL)
		return FALSE;
	xml = g_strndup((const gchar *)buf->data, buf->len);
	if (xml == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "no string data");
		return FALSE;
	}
	g_debug("XML response: %s", xml);
	silo = xb_silo_new_from_xml(xml, error);
	if (silo == NULL)
		return FALSE;

	/* parse response */
	xn_data = xb_silo_query_first(silo, "data", error);
	if (xn_data == NULL)
		return FALSE;

	/* logs to the console */
	xn_logs = xb_node_query(xn_data, "log", 0, NULL);
	if (xn_logs != NULL) {
		for (guint i = 0; i < xn_logs->len; i++) {
			XbNode *xn_log = g_ptr_array_index(xn_logs, i);
			fu_qc_firehose_device_parse_log_text(self,
							     xb_node_get_attr(xn_log, "value"));
		}
	}

	/* from configure */
	xn_response = xb_node_query_first(xn_data, "response", NULL);
	if (xn_response == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO, "no response");
		return FALSE;
	}

	/* switch to binary mode? */
	tmp = xb_node_get_attr(xn_response, "rawmode");
	if (tmp != NULL) {
		if (g_strcmp0(tmp, "true") == 0) {
			self->rawmode = TRUE;
		} else if (g_strcmp0(tmp, "false") == 0) {
			self->rawmode = FALSE;
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid rawmode value: %s",
				    tmp);
			return FALSE;
		}
	}

	/* device is giving us a better value */
	if (g_strcmp0(xb_node_get_attr(xn_response, "value"), "NAK") == 0) {
		tmp = xb_node_get_attr(xn_response, "MaxPayloadSizeToTargetInBytes");
		if (tmp == NULL) {
			tmp =
			    xb_node_get_attr(xn_response, "MaxPayloadSizeToTargetInBytesSupported");
		}
		if (tmp != NULL) {
			if (!fu_strtoull(tmp,
					 &self->max_payload_size,
					 self->maxpktsize_out,
					 G_MAXUINT64,
					 FU_INTEGER_BASE_AUTO,
					 error)) {
				g_prefix_error(error,
					       "failed to parse MaxPayloadSizeToTargetInBytes:");
				return FALSE;
			}
			g_debug("max payload size now 0x%x", (guint)self->max_payload_size);
		}
	}

	/* success */
	if (g_strcmp0(xb_node_get_attr(xn_response, "value"), "ACK") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid data @value, expected ACK and got %s",
			    xb_node_get_attr(xn_response, "value"));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qc_firehose_device_read_xml(FuQcFirehoseDevice *self, guint timeout_ms, GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_qc_firehose_device_read_xml_cb,
			       timeout_ms / FU_QC_FIREHOSE_DEVICE_TIMEOUT_MS,
			       NULL,
			       error);
}

static gboolean
fu_qc_firehose_device_write_xml_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuQcFirehoseDevice *self = FU_QC_FIREHOSE_DEVICE(device);
	const gchar *xml = (const gchar *)user_data;
	return fu_qc_firehose_device_write(self, (const guint8 *)xml, strlen(xml), error);
}

static gboolean
fu_qc_firehose_device_write_xml(FuQcFirehoseDevice *self, XbBuilderNode *bn, GError **error)
{
	g_autofree gchar *xml = NULL;
	xml = xb_builder_node_export(
	    bn,
	    XB_NODE_EXPORT_FLAG_ADD_HEADER | XB_NODE_EXPORT_FLAG_FORMAT_MULTILINE |
		XB_NODE_EXPORT_FLAG_FORMAT_INDENT | XB_NODE_EXPORT_FLAG_COLLAPSE_EMPTY,
	    error);
	if (xml == NULL)
		return FALSE;
#if !LIBXMLB_CHECK_VERSION(0, 3, 22)
	{
		/* firehose is *very* picky about XML and will not accept empty elements */
		GString *xml_fixed = g_string_new(xml);
		g_string_replace(xml_fixed, ">\n  </configure>", " />", 0);
		g_string_replace(xml_fixed, ">\n  </program>", " />", 0);
		g_string_replace(xml_fixed, ">\n  </erase>", " />", 0);
		g_string_replace(xml_fixed, ">\n  </patch>", " />", 0);
		g_string_replace(xml_fixed, ">\n  </setbootablestoragedrive>", " />", 0);
		g_string_replace(xml_fixed, ">\n  </power>", " />", 0);
		g_free(xml);
		xml = g_string_free(xml_fixed, FALSE);
	}
#endif
	g_debug("XML request: %s", xml);
	return fu_device_retry(FU_DEVICE(self), fu_qc_firehose_device_write_xml_cb, 5, xml, error);
}

static gboolean
fu_qc_firehose_device_send_configure(FuQcFirehoseDevice *self,
				     const gchar *storage,
				     gboolean ignore_nak,
				     GError **error)
{
	gboolean no_zlp = fu_device_has_private_flag(FU_DEVICE(self), FU_QC_FIREHOSE_DEVICE_NO_ZLP);
	g_autofree gchar *max_payload_size_str = NULL;
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("data");
	g_autoptr(GError) error_local = NULL;

	/* <data><configure MemoryName="nand"... /></data> */
	max_payload_size_str = g_strdup_printf("%" G_GUINT64_FORMAT, self->max_payload_size);
	xb_builder_node_insert_text(bn,
				    "configure",
				    NULL,
				    "MemoryName",
				    storage,
				    "MaxPayloadSizeToTargetInBytes",
				    max_payload_size_str,
				    "Verbose",
				    "0",
				    "ZlpAwareHost",
				    no_zlp ? "0" : "1",
				    "AlwaysValidate",
				    "0",
				    "MaxDigestTableSizeInBytes",
				    "2048",
				    "SkipStorageInit",
				    "0",
				    NULL);
	if (!fu_qc_firehose_device_write_xml(self, bn, error))
		return FALSE;
	if (!fu_qc_firehose_device_read_xml(self, 5000, &error_local)) {
		/* we're sending our initial suggestion */
		if (ignore_nak &&
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_debug("ignoring, as we've got updated config: %s", error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qc_firehose_device_configure(FuQcFirehoseDevice *self, const gchar *storage, GError **error)
{
	guint64 max_payload_size_old = self->max_payload_size;

	/* sanity check */
	if ((self->supported_functions & FU_QC_FIREHOSE_FUNCTIONS_CONFIGURE) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "configure is not supported");
		return FALSE;
	}

	/* retry if remote proposed different size */
	if (!fu_qc_firehose_device_send_configure(self, storage, TRUE, error))
		return FALSE;
	if (max_payload_size_old != self->max_payload_size) {
		if (!fu_qc_firehose_device_send_configure(self, storage, FALSE, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qc_firehose_device_erase(FuQcFirehoseDevice *self, XbNode *xn, GError **error)
{
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("data");
	g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, xb_node_get_element(xn), NULL);
	const gchar *names[] = {
	    "PAGES_PER_BLOCK",
	    "SECTOR_SIZE_IN_BYTES",
	    "num_partition_sectors",
	    "start_sector",
	};

	/* sanity check */
	if ((self->supported_functions & FU_QC_FIREHOSE_FUNCTIONS_ERASE) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "erase is not supported");
		return FALSE;
	}
	for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
		const gchar *value = xb_node_get_attr(xn, names[i]);
		if (value != NULL)
			xb_builder_node_set_attr(bc, names[i], value);
	}
	if (!fu_qc_firehose_device_write_xml(self, bn, error))
		return FALSE;
	return fu_qc_firehose_device_read_xml(self, 30000, error);
}

static gboolean
fu_qc_firehose_device_write_blocks(FuQcFirehoseDevice *self,
				   FuChunkArray *chunks,
				   FuProgress *progress,
				   GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_qc_firehose_device_write(self,
						 fu_chunk_get_data(chk),
						 fu_chunk_get_data_sz(chk),
						 error))
			return FALSE;

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gchar *
fu_qc_firehose_device_convert_to_image_id(const gchar *filename, GError **error)
{
	g_autofree gchar *filename_safe = NULL;

	/* sanity check */
	if (filename == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no firmware value");
		return NULL;
	}
	filename_safe = g_strdup(filename);
	g_strdelimit(filename_safe, "\\", '/');
	return g_path_get_basename(filename_safe);
}

static gboolean
fu_qc_firehose_device_program(FuQcFirehoseDevice *self,
			      FuFirmware *firmware,
			      XbNode *xn,
			      FuProgress *progress,
			      GError **error)
{
	guint64 sector_size = xb_node_get_attr_as_uint(xn, "SECTOR_SIZE_IN_BYTES");
	guint64 num_sectors = xb_node_get_attr_as_uint(xn, "num_partition_sectors");
	const gchar *filename = xb_node_get_attr(xn, "filename");
	g_autofree gchar *filename_basename = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_padded = NULL;
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("data");
	g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, xb_node_get_element(xn), NULL);
	const gchar *names[] = {
	    "PAGES_PER_BLOCK",
	    "SECTOR_SIZE_IN_BYTES",
	    "filename",
	    "num_partition_sectors",
	    "physical_partition_number",
	    "start_sector",
	    "last_sector",
	};

	/* sanity check */
	if ((self->supported_functions & FU_QC_FIREHOSE_FUNCTIONS_PROGRAM) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "program is not supported");
		return FALSE;
	}

	/* skip any empty filenames */
	filename_basename = fu_qc_firehose_device_convert_to_image_id(filename, error);
	if (filename_basename == NULL)
		return FALSE;
	blob = fu_firmware_get_image_by_id_bytes(firmware, filename_basename, error);
	if (blob == NULL)
		return FALSE;

	/* copy across */
	for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
		const gchar *value = xb_node_get_attr(xn, names[i]);
		if (value != NULL)
			xb_builder_node_set_attr(bc, names[i], value);
	}
	if (!fu_qc_firehose_device_write_xml(self, bn, error))
		return FALSE;
	if (!fu_qc_firehose_device_read_xml(self, 5 * FU_QC_FIREHOSE_DEVICE_TIMEOUT_MS, error)) {
		g_prefix_error(error, "failed to setup: ");
		return FALSE;
	}

	/* sanity check */
	if (!self->rawmode) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device did enter rawmode");
		return FALSE;
	}

	/* the num_partition_sectors is wrong in the autogenerated XML file for some reason */
	if (num_sectors * sector_size < g_bytes_get_size(blob)) {
		g_autofree gchar *num_sectors_str = NULL;

		num_sectors = g_bytes_get_size(blob) / sector_size;
		if ((g_bytes_get_size(blob) % sector_size) != 0)
			num_sectors++;

		/* we also have to modify what we sent the device... */
		g_debug("fixing num_sectors to 0x%x", (guint)num_sectors);
		num_sectors_str = g_strdup_printf("%u", (guint)num_sectors);
		xb_builder_node_set_attr(bc, "num_partition_sectors", num_sectors_str);
	}

	/* write data */
	blob_padded = fu_bytes_pad(blob, num_sectors * sector_size, 0xFF);
	chunks = fu_chunk_array_new_from_bytes(blob_padded, 0x0, 0x0, self->max_payload_size);
	if (!fu_qc_firehose_device_write_blocks(self, chunks, progress, error))
		return FALSE;
	if (!fu_qc_firehose_device_read_xml(self, 30000, error))
		return FALSE;

	/* sanity check */
	if (self->rawmode) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device did leave rawmode");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qc_firehose_device_apply_patch(FuQcFirehoseDevice *self, XbNode *xn, GError **error)
{
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("data");
	g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, xb_node_get_element(xn), NULL);
	const gchar *names[] = {
	    "SECTOR_SIZE_IN_BYTES",
	    "byte_offset",
	    "filename",
	    "physical_partition_number",
	    "size_in_bytes",
	    "start_sector",
	    "value",
	};

	/* sanity check */
	if ((self->supported_functions & FU_QC_FIREHOSE_FUNCTIONS_PATCH) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "patch is not supported");
		return FALSE;
	}
	for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
		const gchar *value = xb_node_get_attr(xn, names[i]);
		if (value != NULL)
			xb_builder_node_set_attr(bc, names[i], value);
	}
	if (!fu_qc_firehose_device_write_xml(self, bn, error))
		return FALSE;
	return fu_qc_firehose_device_read_xml(self, 5000, error);
}

static gboolean
fu_qc_firehose_device_set_bootable(FuQcFirehoseDevice *self, guint part, GError **error)
{
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("data");
	g_autofree gchar *partstr = g_strdup_printf("%u", part);

	/* <data><setbootablestoragedrive value="1" /></data> */
	xb_builder_node_insert_text(bn, "setbootablestoragedrive", NULL, "value", partstr, NULL);
	if (!fu_qc_firehose_device_write_xml(self, bn, error))
		return FALSE;
	if (!fu_qc_firehose_device_read_xml(self, FU_QC_FIREHOSE_DEVICE_TIMEOUT_MS, error)) {
		g_prefix_error(error, "failed to mark partition %u as bootable: ", part);
		return FALSE;
	}
	g_debug("partition %u is now bootable", part);
	return TRUE;
}

static gboolean
fu_qc_firehose_device_reset(FuQcFirehoseDevice *self, GError **error)
{
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("data");

	/* <data><power value="reset /></data> */
	xb_builder_node_insert_text(bn, "power", NULL, "value", "reset", NULL);
	if (!fu_qc_firehose_device_write_xml(self, bn, error))
		return FALSE;
	return fu_qc_firehose_device_read_xml(self, 5000, NULL);
}

static gboolean
fu_qc_firehose_device_erase_targets(FuQcFirehoseDevice *self,
				    GPtrArray *xns,
				    FuProgress *progress,
				    GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, xns->len);

	/* each action in the list */
	for (guint i = 0; i < xns->len; i++) {
		XbNode *xn = g_ptr_array_index(xns, i);
		if (!fu_qc_firehose_device_erase(self, xn, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qc_firehose_device_program_targets(FuQcFirehoseDevice *self,
				      FuFirmware *firmware,
				      GPtrArray *xns,
				      FuProgress *progress,
				      GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, xns->len);

	/* each action in the list */
	for (guint i = 0; i < xns->len; i++) {
		XbNode *xn = g_ptr_array_index(xns, i);
		const gchar *filename = xb_node_get_attr(xn, "filename");
		if (filename == NULL || g_strcmp0(filename, "") != 0) {
			if (!fu_qc_firehose_device_program(self,
							   firmware,
							   xn,
							   fu_progress_get_child(progress),
							   error))
				return FALSE;
		} else {
			g_debug("skipping as filename not provided");
		}
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qc_firehose_device_patch_targets(FuQcFirehoseDevice *self,
				    GPtrArray *xns,
				    FuProgress *progress,
				    GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, xns->len);

	/* each action in the list */
	for (guint i = 0; i < xns->len; i++) {
		XbNode *xn = g_ptr_array_index(xns, i);
		if (!fu_qc_firehose_device_apply_patch(self, xn, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static guint64
fu_qc_firehose_device_find_bootable(FuQcFirehoseDevice *self, GPtrArray *xns)
{
	for (guint i = 0; i < xns->len; i++) {
		XbNode *xn = g_ptr_array_index(xns, i);
		const gchar *filename = xb_node_get_attr(xn, "filename");
		if (filename == NULL)
			continue;
		if (g_pattern_match_simple("*xbl.mbn", filename) ||
		    g_pattern_match_simple("*xbl_a.mbn", filename) ||
		    g_pattern_match_simple("*sbl1.mbn", filename)) {
			return xb_node_get_attr_as_uint(xn, "physical_partition_number");
		}
	}
	return G_MAXUINT64;
}

static gboolean
fu_qc_firehose_device_write_firmware_payload(FuQcFirehoseDevice *self,
					     FuFirmware *firmware,
					     FuProgress *progress,
					     FwupdInstallFlags flags,
					     GError **error)
{
	const gchar *fnglob = "firehose-rawprogram.xml|rawprogram_*.xml";
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) xns_erase = NULL;
	g_autoptr(GPtrArray) xns_program = NULL;
	g_autoptr(GPtrArray) xns_patch = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "patch");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, NULL);

	/* load XML */
	blob = fu_firmware_get_image_by_id_bytes(firmware, fnglob, error);
	if (blob == NULL) {
		g_prefix_error(error, "failed to find %s: ", fnglob);
		return FALSE;
	}
	if (!xb_builder_source_load_bytes(source, blob, XB_BUILDER_SOURCE_FLAG_NONE, error)) {
		g_prefix_error(error, "failed to load %s: ", fnglob);
		return FALSE;
	}
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL) {
		g_prefix_error(error, "failed to compile %s: ", fnglob);
		return FALSE;
	}

	/* clear buffer */
	if (!fu_qc_firehose_device_read_xml(self,
					    5 * FU_QC_FIREHOSE_DEVICE_TIMEOUT_MS,
					    &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		g_debug("ignoring: %s", error_local->message);
	}

	/* hardcode storage */
	if (!fu_qc_firehose_device_configure(self, "nand", error)) {
		g_prefix_error(error, "failed to configure: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* erase */
	xns_erase = xb_silo_query(silo, "data/erase", 0, NULL);
	if (xns_erase != NULL) {
		if (!fu_qc_firehose_device_erase_targets(self,
							 xns_erase,
							 fu_progress_get_child(progress),
							 error)) {
			g_prefix_error(error, "failed to erase targets: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* program */
	xns_program = xb_silo_query(silo, "data/program", 0, NULL);
	if (xns_program != NULL) {
		if (!fu_qc_firehose_device_program_targets(self,
							   firmware,
							   xns_program,
							   fu_progress_get_child(progress),
							   error)) {
			g_prefix_error(error, "failed to program targets: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* patch */
	xns_patch = xb_silo_query(silo, "data/patch", 0, NULL);
	if (xns_patch != NULL) {
		if (!fu_qc_firehose_device_patch_targets(self,
							 xns_patch,
							 fu_progress_get_child(progress),
							 error)) {
			g_prefix_error(error, "failed to patch targets: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* find the bootable partition */
	if (xns_program != NULL &&
	    (self->supported_functions & FU_QC_FIREHOSE_FUNCTIONS_SETBOOTABLESTORAGEDRIVE) > 0) {
		guint64 bootable = fu_qc_firehose_device_find_bootable(self, xns_program);
		if (bootable != G_MAXUINT64) {
			g_debug("setting partition %u bootable", (guint)bootable);
			if (!fu_qc_firehose_device_set_bootable(self, (guint)bootable, error)) {
				g_prefix_error(error, "failed to set bootable: ");
				return FALSE;
			}
		}
	}

	/* reset, back to runtime */
	if (self->supported_functions & FU_QC_FIREHOSE_FUNCTIONS_POWER) {
		if (!fu_qc_firehose_device_reset(self, error)) {
			g_prefix_error(error, "failed to reset: ");
			return FALSE;
		}
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	}
	fu_progress_step_done(progress);

	/* success */
	fu_device_remove_private_flag(FU_DEVICE(self), FU_QC_FIREHOSE_DEVICE_LOADED_FIREHOSE);
	return TRUE;
}

static void
fu_qc_firehose_device_parse_eps(FuQcFirehoseDevice *self, GPtrArray *endpoints)
{
	for (guint i = 0; i < endpoints->len; i++) {
		FuUsbEndpoint *ep = g_ptr_array_index(endpoints, i);
		if (fu_usb_endpoint_get_direction(ep) == FU_USB_DIRECTION_DEVICE_TO_HOST) {
			self->ep_in = fu_usb_endpoint_get_address(ep);
			self->maxpktsize_in = fu_usb_endpoint_get_maximum_packet_size(ep);
		} else {
			self->ep_out = fu_usb_endpoint_get_address(ep);
			self->maxpktsize_out = fu_usb_endpoint_get_maximum_packet_size(ep);
		}
	}
}

static gboolean
fu_qc_firehose_device_probe(FuDevice *device, GError **error)
{
	FuQcFirehoseDevice *self = FU_QC_FIREHOSE_DEVICE(device);
	g_autoptr(GPtrArray) intfs = NULL;

	/* most devices have a BCD version of 0.0 (i.e. unset), but we still want to show the
	 * device in gnome-firmware -- allow overwriting if the descriptor has something better */
	fu_device_set_version(device, "0.0");

	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS(fu_qc_firehose_device_parent_class)->probe(device, error))
		return FALSE;

	/* parse usb interfaces and find suitable endpoints */
	intfs = fu_usb_device_get_interfaces(FU_USB_DEVICE(self), error);
	if (intfs == NULL)
		return FALSE;
	for (guint i = 0; i < intfs->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (fu_usb_interface_get_class(intf) == 0xFF &&
		    fu_usb_interface_get_subclass(intf) == 0xFF &&
		    (fu_usb_interface_get_protocol(intf) == 0xFF ||
		     fu_usb_interface_get_protocol(intf) == 0x11)) {
			g_autoptr(GPtrArray) endpoints = fu_usb_interface_get_endpoints(intf);
			if (endpoints == NULL || endpoints->len == 0)
				continue;
			fu_qc_firehose_device_parse_eps(self, endpoints);
			fu_usb_device_add_interface(FU_USB_DEVICE(self),
						    fu_usb_interface_get_number(intf));
			return TRUE;
		}
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no update interface found");
	return FALSE;
}

static gboolean
fu_qc_firehose_device_sahara_hello(FuQcFirehoseDevice *self, GByteArray *buf, GError **error)
{
	g_autoptr(FuQcFirehoseSaharaPktHello) st = NULL;
	g_autoptr(FuQcFirehoseSaharaPktHelloResp) st_resp =
	    fu_qc_firehose_sahara_pkt_hello_resp_new();

	/* re-parse and reply */
	st = fu_qc_firehose_sahara_pkt_hello_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	fu_qc_firehose_sahara_pkt_hello_resp_set_mode(st_resp,
						      fu_qc_firehose_sahara_pkt_hello_get_mode(st));
	return fu_qc_firehose_device_write(self, st_resp->data, st_resp->len, error);
}

static gboolean
fu_qc_firehose_device_sahara_read(FuQcFirehoseDevice *self,
				  GByteArray *buf,
				  GBytes *blob,
				  GError **error)
{
	g_autoptr(FuQcFirehoseSaharaPktRead) st = NULL;
	g_autoptr(GBytes) blob_chunk = NULL;

	/* re-parse and reply */
	st = fu_qc_firehose_sahara_pkt_read_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	blob_chunk = fu_bytes_new_offset(blob,
					 fu_qc_firehose_sahara_pkt_read_get_offset(st),
					 fu_qc_firehose_sahara_pkt_read_get_length(st),
					 error);
	if (blob_chunk == NULL) {
		g_prefix_error(error, "failed to get bootloader chunk: ");
		return FALSE;
	}
	return fu_qc_firehose_device_write(self,
					   g_bytes_get_data(blob_chunk, NULL),
					   g_bytes_get_size(blob_chunk),
					   error);
}

static gboolean
fu_qc_firehose_device_sahara_read64(FuQcFirehoseDevice *self,
				    GByteArray *buf,
				    GBytes *blob,
				    GError **error)
{
	g_autoptr(FuQcFirehoseSaharaPktRead64) st = NULL;
	g_autoptr(GBytes) blob_chunk = NULL;

	/* re-parse and reply */
	st = fu_qc_firehose_sahara_pkt_read64_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	blob_chunk = fu_bytes_new_offset(blob,
					 fu_qc_firehose_sahara_pkt_read64_get_offset(st),
					 fu_qc_firehose_sahara_pkt_read64_get_length(st),
					 error);
	if (blob_chunk == NULL) {
		g_prefix_error(error, "failed to get bootloader chunk: ");
		return FALSE;
	}
	return fu_qc_firehose_device_write(self,
					   g_bytes_get_data(blob_chunk, NULL),
					   g_bytes_get_size(blob_chunk),
					   error);
}

static gboolean
fu_qc_firehose_device_sahara_eoi(FuQcFirehoseDevice *self, GByteArray *buf, GError **error)
{
	FuQcFirehoseSaharaStatus status;
	g_autoptr(FuQcFirehoseSaharaPktEndOfImage) st = NULL;
	g_autoptr(FuQcFirehoseSaharaPktDone) st_resp = fu_qc_firehose_sahara_pkt_done_new();

	/* re-parse and reply */
	st = fu_qc_firehose_sahara_pkt_end_of_image_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	status = fu_qc_firehose_sahara_pkt_end_of_image_get_status(st);
	if (status != FU_QC_FIREHOSE_SAHARA_STATUS_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid image status for EndOfImage 0x%x: %s",
			    status,
			    fu_qc_firehose_sahara_status_to_string(status));
		return FALSE;
	}
	return fu_qc_firehose_device_write(self, st_resp->data, st_resp->len, error);
}

static gboolean
fu_qc_firehose_device_sahara_done(FuQcFirehoseDevice *self, GByteArray *buf, GError **error)
{
	FuQcFirehoseSaharaStatus status;
	g_autoptr(FuQcFirehoseSaharaPktDoneResp) st = NULL;

	/* re-parse */
	st = fu_qc_firehose_sahara_pkt_done_resp_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	status = fu_qc_firehose_sahara_pkt_done_resp_get_status(st);
	if (status != FU_QC_FIREHOSE_SAHARA_STATUS_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "invalid image status for Done 0x%x: %s",
			    status,
			    fu_qc_firehose_sahara_status_to_string(status));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_qc_firehose_device_sahara_write_firmware(FuQcFirehoseDevice *self,
					    FuFirmware *firmware,
					    FuProgress *progress,
					    FwupdInstallFlags flags,
					    GError **error)
{
	const gchar *fnglob = "firehose-prog.mbn|prog_nand*.mbn|prog_firehose*";
	gboolean done = FALSE;
	g_autoptr(GBytes) blob = NULL;

	blob = fu_firmware_get_image_by_id_bytes(firmware, fnglob, error);
	if (blob == NULL) {
		g_prefix_error(error, "failed to find %s: ", fnglob);
		return FALSE;
	}
	while (!done) {
		FuQcFirehoseSaharaCommandId cmd_id;
		g_autoptr(FuQcFirehoseSaharaPkt) pkt = NULL;
		g_autoptr(GByteArray) buf = NULL;

		buf = fu_qc_firehose_device_read(self, FU_QC_FIREHOSE_DEVICE_TIMEOUT_MS, error);
		if (buf == NULL) {
			g_prefix_error(error, "failed to get device response: ");
			return FALSE;
		}
		pkt = fu_qc_firehose_sahara_pkt_parse(buf->data, buf->len, 0x0, error);
		if (pkt == NULL)
			return FALSE;
		if (buf->len != fu_qc_firehose_sahara_pkt_get_hdr_length(pkt)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "invalid packet header");
			return FALSE;
		}

		/* parse the response */
		cmd_id = fu_qc_firehose_sahara_pkt_get_command_id(pkt);
		switch (cmd_id) {
		case FU_QC_FIREHOSE_SAHARA_COMMAND_ID_HELLO:
			if (!fu_qc_firehose_device_sahara_hello(self, buf, error))
				return FALSE;
			break;
		case FU_QC_FIREHOSE_SAHARA_COMMAND_ID_READ:
			if (!fu_qc_firehose_device_sahara_read(self, buf, blob, error))
				return FALSE;
			break;
		case FU_QC_FIREHOSE_SAHARA_COMMAND_ID_END_OF_IMAGE:
			if (!fu_qc_firehose_device_sahara_eoi(self, buf, error))
				return FALSE;
			break;
		case FU_QC_FIREHOSE_SAHARA_COMMAND_ID_DONE_RESPONSE:
			if (!fu_qc_firehose_device_sahara_done(self, buf, error))
				return FALSE;
			done = TRUE;
			break;
		case FU_QC_FIREHOSE_SAHARA_COMMAND_ID_READ64:
			if (!fu_qc_firehose_device_sahara_read64(self, buf, blob, error))
				return FALSE;
			break;
		default:
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid command ID 0x%x: %s",
				    cmd_id,
				    fu_qc_firehose_sahara_command_id_to_string(cmd_id));
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qc_firehose_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuQcFirehoseDevice *self = FU_QC_FIREHOSE_DEVICE(device);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "sahara");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 95, "firehose");

	/* we've not loaded the sahara binary yet */
	if (!fu_device_has_private_flag(FU_DEVICE(self), FU_QC_FIREHOSE_DEVICE_LOADED_FIREHOSE)) {
		if (!fu_qc_firehose_device_sahara_write_firmware(self,
								 firmware,
								 fu_progress_get_child(progress),
								 flags,
								 error))
			return FALSE;
		fu_device_add_private_flag(FU_DEVICE(self), FU_QC_FIREHOSE_DEVICE_LOADED_FIREHOSE);
	}
	fu_progress_step_done(progress);

	/* use firehose XML */
	if (!fu_qc_firehose_device_write_firmware_payload(self,
							  firmware,
							  fu_progress_get_child(progress),
							  flags,
							  error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_qc_firehose_device_replace(FuDevice *device, FuDevice *donor)
{
	if (fu_device_has_private_flag(donor, FU_QC_FIREHOSE_DEVICE_LOADED_FIREHOSE))
		fu_device_add_private_flag(device, FU_QC_FIREHOSE_DEVICE_LOADED_FIREHOSE);
	if (fu_device_has_private_flag(donor, FU_QC_FIREHOSE_DEVICE_NO_ZLP))
		fu_device_add_private_flag(device, FU_QC_FIREHOSE_DEVICE_NO_ZLP);
}

static void
fu_qc_firehose_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 99, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "reload");
}

static void
fu_qc_firehose_device_init(FuQcFirehoseDevice *self)
{
	self->max_payload_size = 0x100000;
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.qualcomm.firehose");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ARCHIVE_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_register_private_flag(FU_DEVICE(self), FU_QC_FIREHOSE_DEVICE_NO_ZLP);
	fu_device_register_private_flag(FU_DEVICE(self), FU_QC_FIREHOSE_DEVICE_LOADED_FIREHOSE);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x00);
	fu_device_retry_add_recovery(FU_DEVICE(self), FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, NULL);
}

static void
fu_qc_firehose_device_class_init(FuQcFirehoseDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_qc_firehose_device_to_string;
	device_class->probe = fu_qc_firehose_device_probe;
	device_class->replace = fu_qc_firehose_device_replace;
	device_class->write_firmware = fu_qc_firehose_device_write_firmware;
	device_class->set_progress = fu_qc_firehose_device_set_progress;
}
