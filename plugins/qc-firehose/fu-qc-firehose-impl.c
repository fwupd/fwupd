/*
 * Copyright 2021 Quectel Wireless Solutions Co., Ltd.
 *                    Ivan Mikhanchuk <ivan.mikhanchuk@quectel.com>
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-qc-firehose-impl.h"

G_DEFINE_INTERFACE(FuQcFirehoseImpl, fu_qc_firehose_impl, FU_TYPE_DEVICE)

#define FU_QC_FIREHOSE_IMPL_TIMEOUT_MS 500

static void
fu_qc_firehose_impl_default_init(FuQcFirehoseImplInterface *iface)
{
}

static GByteArray *
fu_qc_firehose_impl_read(FuQcFirehoseImpl *self, guint timeout_ms, GError **error)
{
	FuQcFirehoseImplInterface *iface;

	g_return_val_if_fail(FU_IS_QC_FIREHOSE_IMPL(self), NULL);

	iface = FU_QC_FIREHOSE_IMPL_GET_IFACE(self);
	if (iface->read == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->read not implemented");
		return NULL;
	}
	return (*iface->read)(self, timeout_ms, error);
}

static gboolean
fu_qc_firehose_impl_write(FuQcFirehoseImpl *self, const guint8 *buf, gsize bufsz, GError **error)
{
	FuQcFirehoseImplInterface *iface;

	g_return_val_if_fail(FU_IS_QC_FIREHOSE_IMPL(self), FALSE);

	iface = FU_QC_FIREHOSE_IMPL_GET_IFACE(self);
	if (iface->write == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "iface->write not implemented");
		return FALSE;
	}
	return (*iface->write)(self, buf, bufsz, error);
}

static gboolean
fu_qc_firehose_impl_has_function(FuQcFirehoseImpl *self, FuQcFirehoseFunctions func)
{
	FuQcFirehoseImplInterface *iface;

	g_return_val_if_fail(FU_IS_QC_FIREHOSE_IMPL(self), FALSE);

	iface = FU_QC_FIREHOSE_IMPL_GET_IFACE(self);
	if (iface->has_function == NULL)
		return FALSE;
	return (*iface->has_function)(self, func);
}

static void
fu_qc_firehose_impl_add_function(FuQcFirehoseImpl *self, FuQcFirehoseFunctions func)
{
	FuQcFirehoseImplInterface *iface;

	g_return_if_fail(FU_IS_QC_FIREHOSE_IMPL(self));

	iface = FU_QC_FIREHOSE_IMPL_GET_IFACE(self);
	if (iface->add_function == NULL)
		return;
	return (*iface->add_function)(self, func);
}

static void
fu_qc_firehose_impl_parse_log_text(FuQcFirehoseImpl *self, const gchar *text)
{
	if (text == NULL)
		return;
	if (g_str_has_prefix(text, "Supported Functions: ")) {
		g_auto(GStrv) split = g_strsplit(text + 21, " ", -1);
		for (guint i = 0; split[i] != NULL; i++) {
			fu_qc_firehose_impl_add_function(
			    self,
			    fu_qc_firehose_functions_from_string(split[i]));
		}
	}
}

typedef struct {
	FuFirmware *firmware;
	gboolean no_zlp;
	gboolean rawmode;
	guint64 max_payload_size;
} FuQcFirehoseImplHelper;

static gboolean
fu_qc_firehose_impl_read_xml_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuQcFirehoseImpl *self = FU_QC_FIREHOSE_IMPL(device);
	FuQcFirehoseImplHelper *helper = (FuQcFirehoseImplHelper *)user_data;
	const gchar *tmp;
	g_autofree gchar *xml = NULL;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GPtrArray) xn_logs = NULL;
	g_autoptr(XbNode) xn_data = NULL;
	g_autoptr(XbNode) xn_response = NULL;
	g_autoptr(XbSilo) silo = NULL;

	buf = fu_qc_firehose_impl_read(self, FU_QC_FIREHOSE_IMPL_TIMEOUT_MS, error);
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
			fu_qc_firehose_impl_parse_log_text(self, xb_node_get_attr(xn_log, "value"));
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
			helper->rawmode = TRUE;
		} else if (g_strcmp0(tmp, "false") == 0) {
			helper->rawmode = FALSE;
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
					 &helper->max_payload_size,
					 0x0,
					 G_MAXUINT64,
					 FU_INTEGER_BASE_AUTO,
					 error)) {
				g_prefix_error(error,
					       "failed to parse MaxPayloadSizeToTargetInBytes:");
				return FALSE;
			}
			g_debug("max payload size now 0x%x", (guint)helper->max_payload_size);
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
fu_qc_firehose_impl_read_xml(FuQcFirehoseImpl *self,
			     guint timeout_ms,
			     FuQcFirehoseImplHelper *helper,
			     GError **error)
{
	return fu_device_retry(FU_DEVICE(self),
			       fu_qc_firehose_impl_read_xml_cb,
			       timeout_ms / FU_QC_FIREHOSE_IMPL_TIMEOUT_MS,
			       helper,
			       error);
}

static gboolean
fu_qc_firehose_impl_write_xml_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuQcFirehoseImpl *self = FU_QC_FIREHOSE_IMPL(device);
	const gchar *xml = (const gchar *)user_data;
	return fu_qc_firehose_impl_write(self, (const guint8 *)xml, strlen(xml), error);
}

static gboolean
fu_qc_firehose_impl_write_xml(FuQcFirehoseImpl *self, XbBuilderNode *bn, GError **error)
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
	return fu_device_retry(FU_DEVICE(self), fu_qc_firehose_impl_write_xml_cb, 5, xml, error);
}

static gboolean
fu_qc_firehose_impl_send_configure(FuQcFirehoseImpl *self,
				   const gchar *storage,
				   gboolean ignore_nak,
				   FuQcFirehoseImplHelper *helper,
				   GError **error)
{
	g_autofree gchar *max_payload_size_str = NULL;
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("data");
	g_autoptr(GError) error_local = NULL;

	/* <data><configure MemoryName="nand"... /></data> */
	max_payload_size_str = g_strdup_printf("%" G_GUINT64_FORMAT, helper->max_payload_size);
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
				    helper->no_zlp ? "0" : "1",
				    "AlwaysValidate",
				    "0",
				    "MaxDigestTableSizeInBytes",
				    "2048",
				    "SkipStorageInit",
				    "0",
				    NULL);
	if (!fu_qc_firehose_impl_write_xml(self, bn, error))
		return FALSE;
	if (!fu_qc_firehose_impl_read_xml(self, 5000, helper, &error_local)) {
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
fu_qc_firehose_impl_configure(FuQcFirehoseImpl *self,
			      const gchar *storage,
			      FuQcFirehoseImplHelper *helper,
			      GError **error)
{
	guint64 max_payload_size_old = helper->max_payload_size;

	/* sanity check */
	if (!fu_qc_firehose_impl_has_function(self, FU_QC_FIREHOSE_FUNCTIONS_CONFIGURE)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "configure is not supported");
		return FALSE;
	}

	/* retry if remote proposed different size */
	if (!fu_qc_firehose_impl_send_configure(self, storage, TRUE, helper, error))
		return FALSE;
	if (max_payload_size_old != helper->max_payload_size) {
		if (!fu_qc_firehose_impl_send_configure(self, storage, FALSE, helper, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qc_firehose_impl_erase(FuQcFirehoseImpl *self,
			  XbNode *xn,
			  FuQcFirehoseImplHelper *helper,
			  GError **error)
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
	if (!fu_qc_firehose_impl_has_function(self, FU_QC_FIREHOSE_FUNCTIONS_ERASE)) {
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
	if (!fu_qc_firehose_impl_write_xml(self, bn, error))
		return FALSE;
	return fu_qc_firehose_impl_read_xml(self, 30000, helper, error);
}

static gboolean
fu_qc_firehose_impl_write_blocks(FuQcFirehoseImpl *self,
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
		if (!fu_qc_firehose_impl_write(self,
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
fu_qc_firehose_impl_convert_to_image_id(const gchar *filename, GError **error)
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
fu_qc_firehose_impl_program(FuQcFirehoseImpl *self,
			    XbNode *xn,
			    FuQcFirehoseImplHelper *helper,
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
	if (!fu_qc_firehose_impl_has_function(self, FU_QC_FIREHOSE_FUNCTIONS_PROGRAM)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "program is not supported");
		return FALSE;
	}

	/* skip any empty filenames */
	filename_basename = fu_qc_firehose_impl_convert_to_image_id(filename, error);
	if (filename_basename == NULL)
		return FALSE;
	blob = fu_firmware_get_image_by_id_bytes(helper->firmware, filename_basename, error);
	if (blob == NULL)
		return FALSE;

	/* copy across */
	for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
		const gchar *value = xb_node_get_attr(xn, names[i]);
		if (value != NULL)
			xb_builder_node_set_attr(bc, names[i], value);
	}
	if (!fu_qc_firehose_impl_write_xml(self, bn, error))
		return FALSE;
	if (!fu_qc_firehose_impl_read_xml(self,
					  5 * FU_QC_FIREHOSE_IMPL_TIMEOUT_MS,
					  helper,
					  error)) {
		g_prefix_error(error, "failed to setup: ");
		return FALSE;
	}

	/* sanity check */
	if (!helper->rawmode) {
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
	chunks = fu_chunk_array_new_from_bytes(blob_padded, 0x0, 0x0, helper->max_payload_size);
	if (!fu_qc_firehose_impl_write_blocks(self, chunks, progress, error))
		return FALSE;
	if (!fu_qc_firehose_impl_read_xml(self, 30000, helper, error))
		return FALSE;

	/* sanity check */
	if (helper->rawmode) {
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
fu_qc_firehose_impl_apply_patch(FuQcFirehoseImpl *self,
				XbNode *xn,
				FuQcFirehoseImplHelper *helper,
				GError **error)
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
	if (!fu_qc_firehose_impl_has_function(self, FU_QC_FIREHOSE_FUNCTIONS_PATCH)) {
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
	if (!fu_qc_firehose_impl_write_xml(self, bn, error))
		return FALSE;
	return fu_qc_firehose_impl_read_xml(self, 5000, helper, error);
}

static gboolean
fu_qc_firehose_impl_set_bootable(FuQcFirehoseImpl *self,
				 guint part,
				 FuQcFirehoseImplHelper *helper,
				 GError **error)
{
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("data");
	g_autofree gchar *partstr = g_strdup_printf("%u", part);

	/* <data><setbootablestoragedrive value="1" /></data> */
	xb_builder_node_insert_text(bn, "setbootablestoragedrive", NULL, "value", partstr, NULL);
	if (!fu_qc_firehose_impl_write_xml(self, bn, error))
		return FALSE;
	if (!fu_qc_firehose_impl_read_xml(self, FU_QC_FIREHOSE_IMPL_TIMEOUT_MS, helper, error)) {
		g_prefix_error(error, "failed to mark partition %u as bootable: ", part);
		return FALSE;
	}
	g_debug("partition %u is now bootable", part);
	return TRUE;
}

gboolean
fu_qc_firehose_impl_reset(FuQcFirehoseImpl *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(XbBuilderNode) bn = xb_builder_node_new("data");
	FuQcFirehoseImplHelper helper = {0x0};

	/* <data><power value="reset /></data> */
	xb_builder_node_insert_text(bn, "power", NULL, "value", "reset", NULL);
	if (!fu_qc_firehose_impl_write_xml(self, bn, error))
		return FALSE;
	if (!fu_qc_firehose_impl_read_xml(self, 5000, &helper, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT)) {
			g_debug("ignoring: %s", error_local->message);
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qc_firehose_impl_erase_targets(FuQcFirehoseImpl *self,
				  GPtrArray *xns,
				  FuQcFirehoseImplHelper *helper,
				  FuProgress *progress,
				  GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, xns->len);

	/* each action in the list */
	for (guint i = 0; i < xns->len; i++) {
		XbNode *xn = g_ptr_array_index(xns, i);
		if (!fu_qc_firehose_impl_erase(self, xn, helper, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_qc_firehose_impl_program_targets(FuQcFirehoseImpl *self,
				    GPtrArray *xns,
				    FuQcFirehoseImplHelper *helper,
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
			if (!fu_qc_firehose_impl_program(self,
							 xn,
							 helper,
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
fu_qc_firehose_impl_patch_targets(FuQcFirehoseImpl *self,
				  GPtrArray *xns,
				  FuQcFirehoseImplHelper *helper,
				  FuProgress *progress,
				  GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, xns->len);

	/* each action in the list */
	for (guint i = 0; i < xns->len; i++) {
		XbNode *xn = g_ptr_array_index(xns, i);
		if (!fu_qc_firehose_impl_apply_patch(self, xn, helper, error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static guint64
fu_qc_firehose_impl_find_bootable(FuQcFirehoseImpl *self, GPtrArray *xns)
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

gboolean
fu_qc_firehose_impl_write_firmware(FuQcFirehoseImpl *self,
				   FuFirmware *firmware,
				   gboolean no_zlp,
				   FuProgress *progress,
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
	FuQcFirehoseImplHelper helper = {
	    .no_zlp = no_zlp,
	    .rawmode = FALSE,
	    .max_payload_size = 0x100000,
	    .firmware = firmware,
	};

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 1, "patch");

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
	if (!fu_qc_firehose_impl_read_xml(self,
					  5 * FU_QC_FIREHOSE_IMPL_TIMEOUT_MS,
					  &helper,
					  &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		g_debug("ignoring: %s", error_local->message);
	}

	/* hardcode storage */
	if (!fu_qc_firehose_impl_configure(self, "nand", &helper, error)) {
		g_prefix_error(error, "failed to configure: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* erase */
	xns_erase = xb_silo_query(silo, "data/erase", 0, NULL);
	if (xns_erase != NULL) {
		if (!fu_qc_firehose_impl_erase_targets(self,
						       xns_erase,
						       &helper,
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
		if (!fu_qc_firehose_impl_program_targets(self,
							 xns_program,
							 &helper,
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
		if (!fu_qc_firehose_impl_patch_targets(self,
						       xns_patch,
						       &helper,
						       fu_progress_get_child(progress),
						       error)) {
			g_prefix_error(error, "failed to patch targets: ");
			return FALSE;
		}
	}
	fu_progress_step_done(progress);

	/* find the bootable partition */
	if (xns_program != NULL &&
	    fu_qc_firehose_impl_has_function(self,
					     FU_QC_FIREHOSE_FUNCTIONS_SETBOOTABLESTORAGEDRIVE)) {
		guint64 bootable = fu_qc_firehose_impl_find_bootable(self, xns_program);
		if (bootable != G_MAXUINT64) {
			g_debug("setting partition %u bootable", (guint)bootable);
			if (!fu_qc_firehose_impl_set_bootable(self,
							      (guint)bootable,
							      &helper,
							      error)) {
				g_prefix_error(error, "failed to set bootable: ");
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}
