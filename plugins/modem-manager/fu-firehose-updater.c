/*
 * Copyright (C) 2020 Aleksander Morgado <aleksander@aleksander.es>
 * Copyright (C) 2021 Quectel Wireless Solutions Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <sys/ioctl.h>

#include "fu-firehose-updater.h"

/* Maximum amount of non-"response" (e.g. "log") XML messages that can be
 * received from the module when expecting a "response". This is just a safe
 * upper limit to avoid reading forever. */
#define MAX_RECV_MESSAGES 100

/* When initializing the conversation with the firehose interpreter, the
 * first step is to receive and process a bunch of messages sent by the
 * module. The initial timeout to receive the first message is longer in case
 * the module needs some initialization time itself; all the messages after
 * the first one are expected to be received much quicker. The default timeout
 * value should not be extremely long because the initialization phase ends
 * when we don't receive more messages, so it's expected that the timeout will
 * fully elapse after the last message sent by the module. */
#define INITIALIZE_INITIAL_TIMEOUT_MS 3000
#define INITIALIZE_TIMEOUT_MS	      250

/* Maximum amount of time to wait for a message from the module. */
#define DEFAULT_RECV_TIMEOUT_MS 15000

/* The first configure attempt sent to the module will include all the defaults
 * listed below. If the module replies with a NAK specifying a different
 * (shorter) max payload size to use, the second configure attempt will be done
 * with that new suggested max payload size value. Only 2 configure attempts are
 * therefore expected. */
#define MAX_CONFIGURE_ATTEMPTS 2

/* Defaults for the firehose configuration step. The max payload size to target
 * in bytes may end up being a different if the module requests a shorter one.
 */
#define CONFIGURE_MEMORY_NAME			      "nand"
#define CONFIGURE_VERBOSE			      0
#define CONFIGURE_ALWAYS_VALIDATE		      0
#define CONFIGURE_MAX_DIGEST_TABLE_SIZE_IN_BYTES      2048
#define CONFIGURE_MAX_PAYLOAD_SIZE_TO_TARGET_IN_BYTES 8192
#define CONFIGURE_ZLP_AWARE_HOST		      1
#define CONFIGURE_SKIP_STORAGE_INIT		      0

struct _FuFirehoseUpdater {
	GObject parent_instance;
	gchar *port;
	FuIOChannel *io_channel;
};

G_DEFINE_TYPE(FuFirehoseUpdater, fu_firehose_updater, G_TYPE_OBJECT)

static void
fu_firehose_updater_log_message(const gchar *action, GBytes *msg)
{
	const gchar *msg_data;
	gsize msg_size;
	g_autofree gchar *msg_strsafe = NULL;

	if (g_getenv("FWUPD_MODEM_MANAGER_VERBOSE") == NULL)
		return;

	msg_data = (const gchar *)g_bytes_get_data(msg, &msg_size);
	if (msg_size > G_MAXINT)
		return;

	msg_strsafe = fu_common_strsafe(msg_data, msg_size);

	g_debug("%s: %.*s", action, (gint)msg_size, msg_strsafe);
}

static gboolean
validate_program_action(XbNode *program, FuArchive *archive, GError **error)
{
	const gchar *filename_attr;
	GBytes *file;
	gsize file_size;
	guint64 computed_num_partition_sectors;
	guint64 num_partition_sectors;
	guint64 sector_size_in_bytes;

	filename_attr = xb_node_get_attr(program, "filename");
	if (filename_attr == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Missing 'filename' attribute in 'program' action");
		return FALSE;
	}

	/* contents of the CAB file are flat, no subdirectories; look for the
	 * exact filename */
	file = fu_archive_lookup_by_fn(archive, filename_attr, error);
	if (file == NULL)
		return FALSE;

	file_size = g_bytes_get_size(file);

	num_partition_sectors = xb_node_get_attr_as_uint(program, "num_partition_sectors");
	if (num_partition_sectors == G_MAXUINT64) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Missing 'num_partition_sectors' attribute in 'program' action for "
			    "filename '%s'",
			    filename_attr);
		return FALSE;
	}
	sector_size_in_bytes = xb_node_get_attr_as_uint(program, "SECTOR_SIZE_IN_BYTES");
	if (sector_size_in_bytes == G_MAXUINT64) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Missing 'SECTOR_SIZE_IN_BYTES' attribute in 'program' action for "
			    "filename '%s'",
			    filename_attr);
		return FALSE;
	}

	computed_num_partition_sectors = file_size / sector_size_in_bytes;
	if ((file_size % sector_size_in_bytes) != 0)
		computed_num_partition_sectors++;

	if (computed_num_partition_sectors != num_partition_sectors) {
		g_set_error(
		    error,
		    G_IO_ERROR,
		    G_IO_ERROR_FAILED,
		    "Invalid 'num_partition_sectors' in 'program' action for filename '%s': "
		    "expected %" G_GUINT64_FORMAT " instead of %" G_GUINT64_FORMAT " bytes",
		    filename_attr,
		    computed_num_partition_sectors,
		    num_partition_sectors);
		return FALSE;
	}

	xb_node_set_data(program, "fwupd:ProgramFile", file);
	return TRUE;
}

gboolean
fu_firehose_validate_rawprogram(GBytes *rawprogram,
				FuArchive *archive,
				XbSilo **out_silo,
				GPtrArray **out_action_nodes,
				GError **error)
{
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbNode) data_node = NULL;
	g_autoptr(GPtrArray) action_nodes = NULL;

	if (!xb_builder_source_load_bytes(source, rawprogram, XB_BUILDER_SOURCE_FLAG_NONE, error))
		return FALSE;
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;

	data_node = xb_silo_get_root(silo);
	action_nodes = xb_node_get_children(data_node);
	if (action_nodes == NULL || action_nodes->len == 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "No actions given");
		return FALSE;
	}

	for (guint i = 0; i < action_nodes->len; i++) {
		XbNode *n = g_ptr_array_index(action_nodes, i);
		if ((g_strcmp0(xb_node_get_element(n), "program") == 0) &&
		    !validate_program_action(n, archive, error)) {
			return FALSE;
		}
	}

	*out_silo = g_steal_pointer(&silo);
	*out_action_nodes = g_steal_pointer(&action_nodes);
	return TRUE;
}

gboolean
fu_firehose_updater_open(FuFirehoseUpdater *self, GError **error)
{
	/* sanity check */
	if (self->port == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no firehose port provided for filename");
		return FALSE;
	}

	g_debug("opening firehose port...");
	self->io_channel = fu_io_channel_new_file(self->port, error);
	return (self->io_channel != NULL);
}

gboolean
fu_firehose_updater_close(FuFirehoseUpdater *self, GError **error)
{
	g_debug("closing firehose port...");
	if (!fu_io_channel_shutdown(self->io_channel, error))
		return FALSE;
	g_clear_object(&self->io_channel);
	return TRUE;
}

static gboolean
fu_firehose_updater_check_operation_result(XbNode *node, gboolean *out_rawmode)
{
	g_warn_if_fail(g_strcmp0(xb_node_get_element(node), "response") == 0);
	if (g_strcmp0(xb_node_get_attr(node, "value"), "ACK") != 0)
		return FALSE;
	if (out_rawmode)
		*out_rawmode = (g_strcmp0(xb_node_get_attr(node, "rawmode"), "true") == 0);
	return TRUE;
}

static gboolean
fu_firehose_updater_process_response(GBytes *rsp_bytes,
				     XbSilo **out_silo,
				     XbNode **out_response_node,
				     GError **error)
{
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbNode) data_node = NULL;
	g_autoptr(GPtrArray) action_nodes = NULL;

	if (!xb_builder_source_load_bytes(source, rsp_bytes, XB_BUILDER_SOURCE_FLAG_NONE, error))
		return FALSE;
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;

	data_node = xb_silo_get_root(silo);
	if (data_node == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Missing root data node");
		return FALSE;
	}

	action_nodes = xb_node_get_children(data_node);
	if (action_nodes != NULL) {
		for (guint j = 0; j < action_nodes->len; j++) {
			XbNode *node = g_ptr_array_index(action_nodes, j);

			if (g_strcmp0(xb_node_get_element(node), "response") == 0) {
				if (out_silo)
					*out_silo = g_steal_pointer(&silo);
				if (out_response_node)
					*out_response_node = g_object_ref(node);
				return TRUE;
			}

			if (g_strcmp0(xb_node_get_element(node), "log") == 0) {
				const gchar *value_attr = xb_node_get_attr(node, "value");
				if (value_attr)
					g_debug("device log: %s", value_attr);
			}
		}
	}

	if (out_silo != NULL)
		*out_silo = NULL;
	if (out_response_node != NULL)
		*out_response_node = NULL;
	return TRUE;
}

static gboolean
fu_firehose_updater_send_and_receive(FuFirehoseUpdater *self,
				     GByteArray *take_cmd_bytearray,
				     XbSilo **out_silo,
				     XbNode **out_response_node,
				     GError **error)
{
	if (take_cmd_bytearray) {
		const gchar *cmd_header = "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<data>\n";
		const gchar *cmd_trailer = "</data>";
		g_autoptr(GBytes) cmd_bytes = NULL;

		g_byte_array_prepend(take_cmd_bytearray,
				     (const guint8 *)cmd_header,
				     strlen(cmd_header));
		g_byte_array_append(take_cmd_bytearray,
				    (const guint8 *)cmd_trailer,
				    strlen(cmd_trailer));
		cmd_bytes = g_byte_array_free_to_bytes(take_cmd_bytearray);

		fu_firehose_updater_log_message("writing", cmd_bytes);
		if (!fu_io_channel_write_bytes(self->io_channel,
					       cmd_bytes,
					       1500,
					       FU_IO_CHANNEL_FLAG_FLUSH_INPUT,
					       error)) {
			g_prefix_error(error, "Failed to write command: ");
			return FALSE;
		}
	}

	for (guint i = 0; i < MAX_RECV_MESSAGES; i++) {
		g_autoptr(GBytes) rsp_bytes = NULL;
		g_autoptr(XbSilo) silo = NULL;
		g_autoptr(XbNode) response_node = NULL;

		rsp_bytes = fu_io_channel_read_bytes(self->io_channel,
						     -1,
						     DEFAULT_RECV_TIMEOUT_MS,
						     FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
						     error);
		if (rsp_bytes == NULL) {
			g_prefix_error(error, "Failed to read XML message: ");
			return FALSE;
		}

		fu_firehose_updater_log_message("reading", rsp_bytes);
		if (!fu_firehose_updater_process_response(rsp_bytes,
							  &silo,
							  &response_node,
							  error)) {
			g_prefix_error(error, "Failed to parse XML message: ");
			return FALSE;
		}

		if (silo != NULL && response_node != NULL) {
			*out_silo = g_steal_pointer(&silo);
			*out_response_node = g_steal_pointer(&response_node);
			return TRUE;
		}

		/* continue until we get a 'response_node' */
	}

	g_set_error(error,
		    G_IO_ERROR,
		    G_IO_ERROR_TIMED_OUT,
		    "Didn't get any response in the last %d messages",
		    MAX_RECV_MESSAGES);
	return FALSE;
}

static gboolean
fu_firehose_updater_initialize(FuFirehoseUpdater *self, GError **error)
{
	guint n_msg = 0;

	for (guint i = 0; i < MAX_RECV_MESSAGES; i++) {
		g_autoptr(GBytes) rsp_bytes = NULL;

		rsp_bytes = fu_io_channel_read_bytes(
		    self->io_channel,
		    -1,
		    (i == 0 ? INITIALIZE_INITIAL_TIMEOUT_MS : INITIALIZE_TIMEOUT_MS),
		    FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
		    NULL);
		if (rsp_bytes == NULL)
			break;

		fu_firehose_updater_log_message("reading", rsp_bytes);
		if (!fu_firehose_updater_process_response(rsp_bytes, NULL, NULL, error)) {
			g_prefix_error(error, "Failed to parse XML message: ");
			return FALSE;
		}

		n_msg++;
	}

	if (n_msg == 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Couldn't read initial firehose messages from device");
		return FALSE;
	}

	return TRUE;
}

static guint
fu_firehose_updater_configure(FuFirehoseUpdater *self, GError **error)
{
	gint max_payload_size = CONFIGURE_MAX_PAYLOAD_SIZE_TO_TARGET_IN_BYTES;

	for (guint i = 0; i < MAX_CONFIGURE_ATTEMPTS; i++) {
		GByteArray *cmd_bytearray = NULL;
		g_autoptr(XbSilo) rsp_silo = NULL;
		g_autoptr(XbNode) rsp_node = NULL;
		GString *cmd_str = g_string_new(NULL);

		g_string_append_printf(cmd_str, "<configure");
		g_string_append_printf(cmd_str, " MemoryName=\"%s\"", CONFIGURE_MEMORY_NAME);
		g_string_append_printf(cmd_str, " Verbose=\"%d\"", CONFIGURE_VERBOSE);
		g_string_append_printf(cmd_str,
				       " AlwaysValidate=\"%d\"",
				       CONFIGURE_ALWAYS_VALIDATE);
		g_string_append_printf(cmd_str,
				       " MaxDigestTableSizeInBytes=\"%d\"",
				       CONFIGURE_MAX_DIGEST_TABLE_SIZE_IN_BYTES);
		g_string_append_printf(cmd_str,
				       " MaxPayloadSizeToTargetInBytes=\"%d\"",
				       max_payload_size);
		g_string_append_printf(cmd_str, " ZlpAwareHost=\"%d\"", CONFIGURE_ZLP_AWARE_HOST);
		g_string_append_printf(cmd_str,
				       " SkipStorageInit=\"%d\"",
				       CONFIGURE_SKIP_STORAGE_INIT);
		g_string_append_printf(cmd_str, "/>");

		cmd_bytearray = g_bytes_unref_to_array(g_string_free_to_bytes(cmd_str));

		if (!fu_firehose_updater_send_and_receive(self,
							  cmd_bytearray,
							  &rsp_silo,
							  &rsp_node,
							  error)) {
			g_prefix_error(error, "Failed to run configure command: ");
			return 0;
		}

		/* retry if we're told to use a different max payload size */
		if (!fu_firehose_updater_check_operation_result(rsp_node, NULL)) {
			guint64 suggested_max_payload_size;
			g_autoptr(XbNode) root = NULL;

			root = xb_silo_get_root(rsp_silo);
			suggested_max_payload_size =
			    xb_node_get_attr_as_uint(root, "MaxPayloadSizeToTargetInBytes");
			if ((suggested_max_payload_size > G_MAXINT) ||
			    ((gint)suggested_max_payload_size == max_payload_size)) {
				break;
			}

			suggested_max_payload_size = max_payload_size;
			continue;
		}

		/* if operation is successful, return the max payload size we requested */
		return max_payload_size;
	}

	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Configure operation failed");
	return 0;
}

static gboolean
fu_firehose_updater_reset(FuFirehoseUpdater *self, GError **error)
{
	guint recv_cnt = 20;
	const gchar *cmd_str = "<power value=\"reset\" />";
	GByteArray *cmd_bytearray = NULL;
	g_autoptr(XbSilo) rsp_silo = NULL;
	g_autoptr(XbNode) rsp_node = NULL;

	cmd_bytearray =
	    g_byte_array_append(g_byte_array_new(), (const guint8 *)cmd_str, strlen(cmd_str));
	if (!fu_firehose_updater_send_and_receive(self,
						  cmd_bytearray,
						  &rsp_silo,
						  &rsp_node,
						  error)) {
		g_prefix_error(error, "Failed to run reset command: ");
		return FALSE;
	}

	if (!fu_firehose_updater_check_operation_result(rsp_node, NULL)) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Reset operation failed");
		return FALSE;
	}

	/* read out all of the remaining messages. otherwise modem won't go into reset */
	while (--recv_cnt &&
	       fu_firehose_updater_send_and_receive(self, NULL, &rsp_silo, &rsp_node, NULL))
		;

	g_warn_if_fail(recv_cnt > 0);

	return TRUE;
}

static gboolean
fu_firehose_updater_send_program_file(FuFirehoseUpdater *self,
				      const gchar *program_filename,
				      GBytes *program_file,
				      guint payload_size,
				      guint sector_size,
				      GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;
	FuChunk *chk;

	chunks = fu_chunk_array_new_from_bytes(program_file, 0, 0, payload_size);
	/* last block needs to be padded to the next payload_size,
	 * so that we always send full sectors */
	chk = g_ptr_array_index(chunks, chunks->len - 1);
	if (fu_chunk_get_data_sz(chk) != payload_size) {
		g_autoptr(GBytes) padded_bytes = NULL;
		g_autofree guint8 *padded_block = g_malloc0(payload_size);
		g_return_val_if_fail(padded_block != NULL, FALSE);
		memcpy(padded_block, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		padded_bytes = g_bytes_new(padded_block, payload_size);
		fu_chunk_set_bytes(chk, padded_bytes);
		g_return_val_if_fail(fu_chunk_get_data_sz(chk) == payload_size, FALSE);
	}
	for (guint i = 0; i < chunks->len; i++) {
		chk = g_ptr_array_index(chunks, i);

		/* log only in blocks of 250 plus first/last */
		if (i == 0 || i == (chunks->len - 1) || (i + 1) % 250 == 0)
			g_debug("sending %u bytes in block %u/%u of file '%s'",
				fu_chunk_get_data_sz(chk),
				i + 1,
				chunks->len,
				program_filename);

		if (!fu_io_channel_write_bytes(self->io_channel,
					       fu_chunk_get_bytes(chk),
					       1500,
					       FU_IO_CHANNEL_FLAG_FLUSH_INPUT,
					       error)) {
			g_prefix_error(error,
				       "Failed to write block %u/%u of file '%s': ",
				       i + 1,
				       chunks->len,
				       program_filename);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_firehose_updater_actions_validate(GPtrArray *action_nodes,
				     guint max_payload_size,
				     GError **error)
{
	g_return_val_if_fail(action_nodes != NULL, FALSE);

	for (guint i = 0; i < action_nodes->len; i++) {
		const gchar *name = NULL;
		const gchar *program_filename = NULL;
		GBytes *program_file = NULL;
		guint64 program_sector_size_in_bytes = 0;

		XbNode *node = g_ptr_array_index(action_nodes, i);
		const gchar *action = xb_node_get_element(node);

		if (g_strcmp0(action, "program") != 0)
			continue;

		name = "fwupd:ProgramFile";
		program_file = xb_node_get_data(node, name);
		if (program_file == NULL) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Failed to validate program file '%s' command: "
				    "failed to get %s",
				    program_filename,
				    name);
			return FALSE;
		}
		name = "filename";
		program_filename = xb_node_get_attr(node, name);
		if (program_filename == NULL) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Failed to validate program file '%s' command: "
				    "failed to get %s",
				    program_filename,
				    name);
			return FALSE;
		}
		name = "SECTOR_SIZE_IN_BYTES";
		program_sector_size_in_bytes = xb_node_get_attr_as_uint(node, name);
		if (program_sector_size_in_bytes > max_payload_size) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Failed to validate program file '%s' command: "
				    "requested sector size bigger (%" G_GUINT64_FORMAT " bytes) "
				    "than maximum payload size agreed with device (%u bytes)",
				    program_filename,
				    program_sector_size_in_bytes,
				    max_payload_size);
			return FALSE;
		}
	}

	return TRUE;
}

static gsize
fu_firehose_updater_actions_get_total_file_size(GPtrArray *action_nodes)
{
	gsize total_bytes = 0;
	g_return_val_if_fail(action_nodes != NULL, 0);

	for (guint i = 0; i < action_nodes->len; i++) {
		GBytes *program_file = NULL;
		XbNode *node = g_ptr_array_index(action_nodes, i);
		const gchar *action = xb_node_get_element(node);

		if (g_strcmp0(action, "program") != 0)
			continue;

		program_file = xb_node_get_data(node, "fwupd:ProgramFile");

		if (program_file != NULL)
			total_bytes += g_bytes_get_size(program_file);
	}

	return total_bytes;
}

static gboolean
fu_firehose_updater_run_action_program(FuFirehoseUpdater *self,
				       XbNode *node,
				       gboolean rawmode,
				       guint max_payload_size,
				       gsize *sent_bytes,
				       GError **error)
{
	GBytes *program_file = NULL;
	const gchar *program_filename = NULL;
	guint64 program_sector_size = 0;
	guint payload_size = 0;
	g_autoptr(XbSilo) rsp_silo = NULL;
	g_autoptr(XbNode) rsp_node = NULL;

	program_file = xb_node_get_data(node, "fwupd:ProgramFile");
	if (program_file == NULL)
		return FALSE;
	program_filename = xb_node_get_attr(node, "filename");
	if (program_filename == NULL)
		return FALSE;
	program_sector_size = xb_node_get_attr_as_uint(node, "SECTOR_SIZE_IN_BYTES");
	if (program_sector_size == G_MAXUINT64)
		return FALSE;

	if (rawmode == FALSE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Failed to download program file '%s': rawmode not enabled",
			    program_filename);
		return FALSE;
	}

	while ((payload_size + (guint)program_sector_size) < max_payload_size)
		payload_size += (guint)program_sector_size;

	g_debug("sending program file '%s' (%zu bytes)",
		program_filename,
		g_bytes_get_size(program_file));
	if (!fu_firehose_updater_send_program_file(self,
						   program_filename,
						   program_file,
						   payload_size,
						   program_sector_size,
						   error)) {
		g_prefix_error(error, "Failed to send program file '%s': ", program_filename);
		return FALSE;
	}

	g_debug("waiting for program file download confirmation...");
	if (!fu_firehose_updater_send_and_receive(self, NULL, &rsp_silo, &rsp_node, error)) {
		g_prefix_error(error,
			       "Download confirmation not received for file '%s': ",
			       program_filename);
		return FALSE;
	}

	if (!fu_firehose_updater_check_operation_result(rsp_node, &rawmode)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Download confirmation failed for file '%s'",
			    program_filename);
		return FALSE;
	}

	if (rawmode != FALSE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "Download confirmation failed for file '%s': rawmode still enabled",
			    program_filename);
		return FALSE;
	}

	if (sent_bytes != NULL)
		*sent_bytes += g_bytes_get_size(program_file);

	return TRUE;
}

static gboolean
fu_firehose_updater_run_action(FuFirehoseUpdater *self,
			       XbNode *node,
			       guint max_payload_size,
			       gsize *sent_bytes,
			       GError **error)
{
	const gchar *action;
	gchar *cmd_str = NULL;
	gboolean rawmode = FALSE;
	GByteArray *cmd_bytearray = NULL;
	g_autoptr(XbSilo) rsp_silo = NULL;
	g_autoptr(XbNode) rsp_node = NULL;

	action = xb_node_get_element(node);

#if LIBXMLB_CHECK_VERSION(0, 2, 2)
	cmd_str = xb_node_export(node, XB_NODE_EXPORT_FLAG_COLLAPSE_EMPTY, error);
#else
	cmd_str = xb_node_export(node, XB_NODE_EXPORT_FLAG_NONE, error);
#endif
	if (cmd_str == NULL)
		return FALSE;
	cmd_bytearray = g_byte_array_new_take((guint8 *)cmd_str, strlen(cmd_str));

	g_debug("running command '%s'...", action);
	if (!fu_firehose_updater_send_and_receive(self,
						  cmd_bytearray,
						  &rsp_silo,
						  &rsp_node,
						  error)) {
		g_prefix_error(error, "Failed to run command '%s': ", action);
		return FALSE;
	}

	if (!fu_firehose_updater_check_operation_result(rsp_node, &rawmode)) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Command '%s' failed", action);
		return FALSE;
	}

	if (g_strcmp0(action, "program") == 0)
		return fu_firehose_updater_run_action_program(self,
							      node,
							      rawmode,
							      max_payload_size,
							      sent_bytes,
							      error);

	return TRUE;
}

static gboolean
fu_firehose_updater_run_actions(FuFirehoseUpdater *self,
				XbSilo *silo,
				GPtrArray *action_nodes,
				guint max_payload_size,
				FuProgress *progress,
				GError **error)
{
	gsize sent_bytes = 0;
	gsize total_bytes = 0;

	g_warn_if_fail(action_nodes->len > 0);

	if (!fu_firehose_updater_actions_validate(action_nodes, max_payload_size, error))
		return FALSE;

	total_bytes = fu_firehose_updater_actions_get_total_file_size(action_nodes);

	for (guint i = 0; i < action_nodes->len; i++) {
		XbNode *node = g_ptr_array_index(action_nodes, i);

		if (!fu_firehose_updater_run_action(self,
						    node,
						    max_payload_size,
						    &sent_bytes,
						    error))
			return FALSE;

		fu_progress_set_percentage_full(progress, sent_bytes, total_bytes);
	}

	return TRUE;
}

gboolean
fu_firehose_updater_write(FuFirehoseUpdater *self,
			  XbSilo *silo,
			  GPtrArray *action_nodes,
			  FuProgress *progress,
			  GError **error)
{
	guint max_payload_size;
	gboolean result;
	g_autoptr(GError) error_local = NULL;

	if (!fu_firehose_updater_initialize(self, error))
		return FALSE;

	max_payload_size = fu_firehose_updater_configure(self, error);
	if (max_payload_size == 0)
		return FALSE;

	result = fu_firehose_updater_run_actions(self,
						 silo,
						 action_nodes,
						 max_payload_size,
						 progress,
						 error);

	if (!fu_firehose_updater_reset(self, &error_local)) {
		if (result)
			g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	return result;
}

static void
fu_firehose_updater_init(FuFirehoseUpdater *self)
{
}

static void
fu_firehose_updater_finalize(GObject *object)
{
	FuFirehoseUpdater *self = FU_FIREHOSE_UPDATER(object);
	g_warn_if_fail(self->io_channel == NULL);
	g_free(self->port);
	G_OBJECT_CLASS(fu_firehose_updater_parent_class)->finalize(object);
}

static void
fu_firehose_updater_class_init(FuFirehoseUpdaterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_firehose_updater_finalize;
}

FuFirehoseUpdater *
fu_firehose_updater_new(const gchar *port)
{
	FuFirehoseUpdater *self = g_object_new(FU_TYPE_FIREHOSE_UPDATER, NULL);
	self->port = g_strdup(port);
	return self;
}
