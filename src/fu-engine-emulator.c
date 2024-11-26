/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include "fu-archive.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-engine-emulator.h"

struct _FuEngineEmulator {
	GObject parent_instance;
	FuEngine *engine;
	GHashTable *phase_blobs; /* (element-type int GBytes) */
};

G_DEFINE_TYPE(FuEngineEmulator, fu_engine_emulator, G_TYPE_OBJECT)

enum { PROP_0, PROP_ENGINE, PROP_LAST };

gboolean
fu_engine_emulator_save(FuEngineEmulator *self, GOutputStream *stream, GError **error)
{
	gboolean got_json = FALSE;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(FuArchive) archive = fu_archive_new(NULL, FU_ARCHIVE_FLAG_NONE, NULL);

	g_return_val_if_fail(FU_IS_ENGINE_EMULATOR(self), FALSE);
	g_return_val_if_fail(G_IS_OUTPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	for (guint phase = FU_ENGINE_EMULATOR_PHASE_SETUP; phase < FU_ENGINE_EMULATOR_PHASE_LAST;
	     phase++) {
		GBytes *blob = g_hash_table_lookup(self->phase_blobs, GINT_TO_POINTER(phase));
		g_autofree gchar *fn =
		    g_strdup_printf("%s.json", fu_engine_emulator_phase_to_string(phase));
		if (blob == NULL)
			continue;
		got_json = TRUE;
		fu_archive_add_entry(archive, fn, blob);
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
	if (!g_output_stream_write_all(stream, buf->data, buf->len, NULL, NULL, error)) {
		fu_error_convert(error);
		return FALSE;
	}
	if (!g_output_stream_flush(stream, NULL, error)) {
		fu_error_convert(error);
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
	JsonNode *root;
	g_autoptr(JsonParser) parser = json_parser_new();

	/* parse */
	if (!json_parser_load_from_data(parser,
					g_bytes_get_data(json_blob, NULL),
					g_bytes_get_size(json_blob),
					error))
		return FALSE;

	/* load into all backends */
	root = json_parser_get_root(parser);
	for (guint i = 0; i < backends->len; i++) {
		FuBackend *backend = g_ptr_array_index(backends, i);
		if (!fwupd_codec_from_json(FWUPD_CODEC(backend), root, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_engine_emulator_load_phase(FuEngineEmulator *self, FuEngineEmulatorPhase phase, GError **error)
{
	GBytes *json_blob;

	json_blob = g_hash_table_lookup(self->phase_blobs, GINT_TO_POINTER(phase));
	if (json_blob == NULL)
		return TRUE;
	return fu_engine_emulator_load_json_blob(self, json_blob, error);
}

static void
fu_engine_emulator_to_json(FuEngineEmulator *self, GPtrArray *devices, JsonBuilder *json_builder)
{
	/* not always correct, but we want to remain compatible with all the old emulation files */
	json_builder_begin_object(json_builder);
	json_builder_set_member_name(json_builder, "UsbDevices");
	json_builder_begin_array(json_builder);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);

		/* interesting? */
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG))
			continue;
		json_builder_begin_object(json_builder);
		fwupd_codec_to_json(FWUPD_CODEC(device), json_builder, FWUPD_CODEC_FLAG_NONE);
		json_builder_end_object(json_builder);
	}
	json_builder_end_array(json_builder);
	json_builder_end_object(json_builder);

	/* we've recorded these, now drop them */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG))
			continue;
		fu_device_clear_events(device);
	}
}

gboolean
fu_engine_emulator_save_phase(FuEngineEmulator *self, FuEngineEmulatorPhase phase, GError **error)
{
	GBytes *blob_old;
	g_autofree gchar *blob_new_safe = NULL;
	g_autoptr(GBytes) blob_new = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GOutputStream) ostream = g_memory_output_stream_new_resizable();
	g_autoptr(JsonBuilder) json_builder = json_builder_new();
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* all devices in all backends */
	devices = fu_engine_get_devices(self->engine, error);
	if (devices == NULL)
		return FALSE;
	fu_engine_emulator_to_json(self, devices, json_builder);

	json_root = json_builder_get_root(json_builder);
	json_generator = json_generator_new();
	json_generator_set_pretty(json_generator, TRUE);
	json_generator_set_root(json_generator, json_root);

	blob_old = g_hash_table_lookup(self->phase_blobs, GINT_TO_POINTER(phase));
	if (!json_generator_to_stream(json_generator, ostream, NULL, error))
		return FALSE;
	if (!g_output_stream_close(ostream, NULL, error))
		return FALSE;
	blob_new = g_memory_output_stream_steal_as_bytes(G_MEMORY_OUTPUT_STREAM(ostream));

	if (g_bytes_get_size(blob_new) == 0) {
		g_info("no data for phase %s", fu_engine_emulator_phase_to_string(phase));
		return TRUE;
	}
	if (blob_old != NULL && g_bytes_compare(blob_old, blob_new) == 0) {
		g_info("JSON unchanged for phase %s", fu_engine_emulator_phase_to_string(phase));
		return TRUE;
	}
	blob_new_safe = fu_strsafe_bytes(blob_new, 8000);
	g_info("JSON %s for phase %s: %s...",
	       blob_old == NULL ? "added" : "changed",
	       fu_engine_emulator_phase_to_string(phase),
	       blob_new_safe);
	g_hash_table_insert(self->phase_blobs, GINT_TO_POINTER(phase), g_steal_pointer(&blob_new));

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
	for (guint phase = FU_ENGINE_EMULATOR_PHASE_SETUP; phase < FU_ENGINE_EMULATOR_PHASE_LAST;
	     phase++) {
		g_autofree gchar *fn =
		    g_strdup_printf("%s.json", fu_engine_emulator_phase_to_string(phase));
		g_autoptr(GBytes) blob = NULL;

		/* not found */
		blob = fu_archive_lookup_by_fn(archive, fn, NULL);
		if (blob == NULL || g_bytes_get_size(blob) == 0)
			continue;
		got_json = TRUE;
		g_info("emulation for phase %s", fu_engine_emulator_phase_to_string(phase));
		if (phase == FU_ENGINE_EMULATOR_PHASE_SETUP) {
			if (!fu_engine_emulator_load_json_blob(self, blob, error))
				return FALSE;
		} else {
			g_hash_table_insert(self->phase_blobs,
					    GINT_TO_POINTER(phase),
					    g_steal_pointer(&blob));
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
	self->phase_blobs = g_hash_table_new_full(g_direct_hash,
						  g_direct_equal,
						  NULL,
						  (GDestroyNotify)g_bytes_unref);
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
