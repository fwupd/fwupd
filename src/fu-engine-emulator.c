/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include "fu-archive.h"
#include "fu-backend-private.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-engine-emulator.h"

struct _FuEngineEmulator {
	GObject parent_instance;
	FuEngine *engine;
	GHashTable *phase_blobs; /* (element-type utf-8 GBytes) */
};

G_DEFINE_TYPE(FuEngineEmulator, fu_engine_emulator, G_TYPE_OBJECT)

enum { PROP_0, PROP_ENGINE, PROP_LAST };

/* [composite_cnt:]{phase}[-write_cnt].json */
static gchar *
fu_engine_emulator_phase_to_filename(guint composite_cnt,
				     FuEngineEmulatorPhase phase,
				     guint write_cnt)
{
	g_autoptr(GString) fn = g_string_new(NULL);
	if (composite_cnt != 0)
		g_string_append_printf(fn, "%u:", composite_cnt);
	g_string_append(fn, fu_engine_emulator_phase_to_string(phase));
	if (write_cnt != FU_ENGINE_EMULATOR_WRITE_COUNT_DEFAULT)
		g_string_append_printf(fn, "-%u", write_cnt);
	g_string_append(fn, ".json");
	return g_string_free(g_steal_pointer(&fn), FALSE);
}

gboolean
fu_engine_emulator_save(FuEngineEmulator *self, GOutputStream *stream, GError **error)
{
	GHashTableIter iter;
	gboolean got_json = FALSE;
	gpointer key;
	gpointer value;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuArchive) archive = fu_archive_new(NULL, FU_ARCHIVE_FLAG_NONE, NULL);

	g_return_val_if_fail(FU_IS_ENGINE_EMULATOR(self), FALSE);
	g_return_val_if_fail(G_IS_OUTPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	g_hash_table_iter_init(&iter, self->phase_blobs);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		fu_archive_add_entry(archive, (const gchar *)key, (GBytes *)value);
		got_json = TRUE;
	}
	if (!got_json) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no emulation data, perhaps no devices have been added?");
		return FALSE;
	}

	/* write  */
	buf = fu_archive_write(archive, FU_ARCHIVE_FORMAT_ZIP, FU_ARCHIVE_COMPRESSION_GZIP, error);
	if (buf == NULL)
		return FALSE;
	blob = g_byte_array_free_to_bytes(g_steal_pointer(&buf)); /* nocheck:blocked */
	if (!fu_output_stream_write_bytes(stream, blob, NULL, error))
		return FALSE;
	if (!g_output_stream_flush(stream, NULL, error)) {
		fwupd_error_convert(error);
		return FALSE;
	}

	/* success */
	g_hash_table_remove_all(self->phase_blobs);
	return TRUE;
}

static gboolean
fu_engine_emulator_load_json_blob(FuEngineEmulator *self, GBytes *json_blob, GError **error)
{
	GPtrArray *backends = fu_context_get_backends(fu_engine_get_context(self->engine));
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonParser) json_parser = fwupd_json_parser_new();

	/* set appropriate limits */
	fwupd_json_parser_set_max_depth(json_parser, 50);
	fwupd_json_parser_set_max_items(json_parser, 5000000); /* yes, this big! */
	fwupd_json_parser_set_max_quoted(json_parser, 1000000);

	/* parse */
	json_node = fwupd_json_parser_load_from_bytes(json_parser,
						      json_blob,
						      FWUPD_JSON_LOAD_FLAG_TRUSTED |
							  FWUPD_JSON_LOAD_FLAG_STATIC_KEYS,
						      error);
	if (json_node == NULL)
		return FALSE;
	json_obj = fwupd_json_node_get_object(json_node, error);
	if (json_obj == NULL)
		return FALSE;

	/* load into all backends */
	for (guint i = 0; i < backends->len; i++) {
		FuBackend *backend = g_ptr_array_index(backends, i);
		if (!fwupd_codec_from_json(FWUPD_CODEC(backend), json_obj, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_engine_emulator_load_phase(FuEngineEmulator *self,
			      guint composite_cnt,
			      FuEngineEmulatorPhase phase,
			      guint write_cnt,
			      GError **error)
{
	GBytes *json_blob;
	g_autofree gchar *fn = NULL;

	fn = fu_engine_emulator_phase_to_filename(composite_cnt, phase, write_cnt);
	json_blob = g_hash_table_lookup(self->phase_blobs, fn);
	if (json_blob == NULL) {
		g_debug("emulator not loading %s, as not found", fn);
		return TRUE;
	}
	g_debug("emulator loading %s", fn);
	return fu_engine_emulator_load_json_blob(self, json_blob, error);
}

static void
fu_engine_emulator_to_json(FuEngineEmulator *self, GPtrArray *devices, FwupdJsonObject *json_obj)
{
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();

	/* not always correct, but we want to remain compatible with all the old emulation files */
	fwupd_json_object_add_string(json_obj, "FwupdVersion", PACKAGE_VERSION);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		g_autoptr(FwupdJsonObject) json_device = fwupd_json_object_new();

		/* interesting? */
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG))
			continue;
		fu_device_add_json(device, json_device, FWUPD_CODEC_FLAG_NONE);
		fwupd_json_array_add_object(json_arr, json_device);
	}
	fwupd_json_object_add_array(json_obj, "UsbDevices", json_arr);

	/* we've recorded these, now drop them */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG))
			continue;
		fu_device_clear_events(device);
	}
}

gboolean
fu_engine_emulator_save_phase(FuEngineEmulator *self,
			      guint composite_cnt,
			      FuEngineEmulatorPhase phase,
			      guint write_cnt,
			      GError **error)
{
	GBytes *blob_old;
	g_autofree gchar *blob_new_safe = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(GBytes) blob_new = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();

	/* all devices in all backends */
	devices = fu_engine_get_devices(self->engine, error);
	if (devices == NULL)
		return FALSE;
	fu_engine_emulator_to_json(self, devices, json_obj);

	fn = fu_engine_emulator_phase_to_filename(composite_cnt, phase, write_cnt);
	g_debug("saving %s", fn);
	blob_old = g_hash_table_lookup(self->phase_blobs, fn);
	blob_new = fwupd_json_object_to_bytes(json_obj,
					      FWUPD_JSON_EXPORT_FLAG_INDENT |
						  FWUPD_JSON_EXPORT_FLAG_TRAILING_NEWLINE);

	if (g_bytes_get_size(blob_new) == 0) {
		g_info("no data for phase %s [%u]",
		       fu_engine_emulator_phase_to_string(phase),
		       write_cnt);
		return TRUE;
	}
	if (blob_old != NULL && g_bytes_compare(blob_old, blob_new) == 0) {
		g_info("JSON unchanged for phase %s [%u]",
		       fu_engine_emulator_phase_to_string(phase),
		       write_cnt);
		return TRUE;
	}
	blob_new_safe = fu_strsafe_bytes(blob_new, 8000);
	g_info("JSON %s for phase %s [%u]: %s…",
	       blob_old == NULL ? "added" : "changed",
	       fu_engine_emulator_phase_to_string(phase),
	       write_cnt,
	       blob_new_safe);
	g_hash_table_insert(self->phase_blobs, g_steal_pointer(&fn), g_steal_pointer(&blob_new));

	/* success */
	return TRUE;
}

static gboolean
fu_engine_emulator_load_phases(FuEngineEmulator *self,
			       FuArchive *archive,
			       guint composite_cnt,
			       guint write_cnt,
			       gboolean *got_json,
			       GError **error)
{
	for (FuEngineEmulatorPhase phase = FU_ENGINE_EMULATOR_PHASE_SETUP;
	     phase < FU_ENGINE_EMULATOR_PHASE_LAST;
	     phase++) {
		g_autofree gchar *fn = NULL;
		g_autoptr(GBytes) blob = NULL;

		/* not found */
		fn = fu_engine_emulator_phase_to_filename(composite_cnt, phase, write_cnt);
		blob = fu_archive_lookup_by_fn(archive, fn, NULL);
		if (blob == NULL || g_bytes_get_size(blob) == 0)
			continue;
		*got_json = TRUE;
		g_info("emulation for phase %s [%u]",
		       fu_engine_emulator_phase_to_string(phase),
		       write_cnt);
		if (composite_cnt == 0 && write_cnt == FU_ENGINE_EMULATOR_WRITE_COUNT_DEFAULT &&
		    phase == FU_ENGINE_EMULATOR_PHASE_SETUP) {
			if (!fu_engine_emulator_load_json_blob(self, blob, error))
				return FALSE;
		} else {
			g_hash_table_insert(self->phase_blobs,
					    g_steal_pointer(&fn),
					    g_steal_pointer(&blob));
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_engine_emulator_load(FuEngineEmulator *self, GInputStream *stream, GError **error)
{
	gboolean got_json = FALSE;
	const gchar *json_empty = "{\"UsbDevices\":[]}";
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) json_blob = g_bytes_new_static(json_empty, strlen(json_empty));
	g_autoptr(GError) error_archive = NULL;

	g_return_val_if_fail(FU_IS_ENGINE_EMULATOR(self), FALSE);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* unload any existing devices */
	if (!fu_engine_emulator_load_json_blob(self, json_blob, error))
		return FALSE;
	g_hash_table_remove_all(self->phase_blobs);

	/* load archive */
	archive = fu_archive_new_stream(stream, FU_ARCHIVE_FLAG_NONE, &error_archive);
	if (archive == NULL) {
		g_autoptr(GBytes) blob = NULL;
		g_debug("no archive found, using JSON as phase setup: %s", error_archive->message);
		blob = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, NULL, error);
		if (blob == NULL)
			return FALSE;
		return fu_engine_emulator_load_json_blob(self, blob, error);
	}

	/* load JSON files from archive */
	for (guint composite_cnt = 0; composite_cnt < FU_ENGINE_EMULATOR_COMPOSITE_MAX;
	     composite_cnt++) {
		for (guint write_cnt = 0; write_cnt < FU_ENGINE_EMULATOR_WRITE_COUNT_MAX;
		     write_cnt++) {
			if (!fu_engine_emulator_load_phases(self,
							    archive,
							    composite_cnt,
							    write_cnt,
							    &got_json,
							    error))
				return FALSE;
		}
	}
	if (!got_json) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no emulation data found in archive");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_engine_emulator_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuEngineEmulator *self = FU_ENGINE_EMULATOR(object);
	switch (prop_id) {
	case PROP_ENGINE:
		g_value_set_object(value, self->engine);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_engine_emulator_set_property(GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	FuEngineEmulator *self = FU_ENGINE_EMULATOR(object);
	switch (prop_id) {
	case PROP_ENGINE:
		self->engine = g_value_dup_object(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_engine_emulator_init(FuEngineEmulator *self)
{
	self->phase_blobs =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_bytes_unref);
}

static void
fu_engine_emulator_finalize(GObject *obj)
{
	FuEngineEmulator *self = FU_ENGINE_EMULATOR(obj);
	g_hash_table_unref(self->phase_blobs);
	G_OBJECT_CLASS(fu_engine_emulator_parent_class)->finalize(obj);
}

static void
fu_engine_emulator_dispose(GObject *obj)
{
	FuEngineEmulator *self = FU_ENGINE_EMULATOR(obj);
	g_clear_object(&self->engine);
	G_OBJECT_CLASS(fu_engine_emulator_parent_class)->dispose(obj);
}

static void
fu_engine_emulator_class_init(FuEngineEmulatorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_engine_emulator_finalize;
	object_class->dispose = fu_engine_emulator_dispose;
	object_class->get_property = fu_engine_emulator_get_property;
	object_class->set_property = fu_engine_emulator_set_property;

	pspec =
	    g_param_spec_object("engine",
				NULL,
				NULL,
				FU_TYPE_ENGINE,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_ENGINE, pspec);
}

FuEngineEmulator *
fu_engine_emulator_new(FuEngine *engine)
{
	return FU_ENGINE_EMULATOR(g_object_new(FU_TYPE_ENGINE_EMULATOR, "engine", engine, NULL));
}
