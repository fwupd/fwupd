/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include "fu-backend.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-engine-emulator.h"
#include "fu-input-stream.h"

struct _FuEngineEmulator {
	GObject parent_instance;
	FuEngine *engine;
	GHashTable *phase_blobs; /* (element-type utf-8 GBytes) */
	gchar *last_save_fn;	 /* the last phase saved, used to merge repeated setup snapshots */
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
	return g_string_free_and_steal(g_steal_pointer(&fn));
}

gboolean
fu_engine_emulator_save(FuEngineEmulator *self, GOutputStream *stream, GError **error)
{
	GHashTableIter iter;
	gboolean got_json = FALSE;
	gpointer key;
	gpointer value;
	g_autofree gchar *fn_setup = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuFirmware) archive = fu_zip_firmware_new();

	g_return_val_if_fail(FU_IS_ENGINE_EMULATOR(self), FALSE);
	g_return_val_if_fail(G_IS_OUTPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	fn_setup = fu_engine_emulator_phase_to_filename(0,
							FU_ENGINE_EMULATOR_PHASE_SETUP,
							FU_ENGINE_EMULATOR_WRITE_COUNT_DEFAULT);
	if (!g_hash_table_contains(self->phase_blobs, fn_setup)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no enumeration data, perhaps the device was not replugged?");
		return FALSE;
	}
	g_hash_table_iter_init(&iter, self->phase_blobs);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		g_autoptr(FuFirmware) img = fu_zip_file_new();
		fu_zip_file_set_compression(FU_ZIP_FILE(img), FU_ZIP_COMPRESSION_DEFLATE);
		fu_firmware_set_id(img, (const gchar *)key);
		fu_firmware_set_bytes(img, (GBytes *)value);
		if (!fu_firmware_add_image(archive, img, error))
			return FALSE;
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
	blob = fu_firmware_write(archive, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_output_stream_write_bytes(stream, blob, NULL, error))
		return FALSE;
	if (!g_output_stream_flush(stream, NULL, error)) {
		fwupd_error_convert(error);
		return FALSE;
	}

	/* success */
	g_hash_table_remove_all(self->phase_blobs);
	g_clear_pointer(&self->last_save_fn, g_free);
	return TRUE;
}

/* the emulation files are large, so use the same limits when loading and when merging */
static FwupdJsonParser *
fu_engine_emulator_json_parser_new(void)
{
	FwupdJsonParser *json_parser = fwupd_json_parser_new();
	fwupd_json_parser_set_max_depth(json_parser, 50);
	fwupd_json_parser_set_max_items(json_parser, 5000000); /* yes, this big! */
	fwupd_json_parser_set_max_quoted(json_parser, 1000000);
	return json_parser;
}

static gboolean
fu_engine_emulator_load_json_from_stream(FuEngineEmulator *self,
					 FuInputStream *stream,
					 GError **error)
{
	GPtrArray *backends = fu_context_get_backends(fu_engine_get_context(self->engine));
	g_autoptr(FwupdJsonNode) json_node = NULL;
	g_autoptr(FwupdJsonObject) json_obj = NULL;
	g_autoptr(FwupdJsonParser) json_parser = fu_engine_emulator_json_parser_new();

	/* parse */
	json_node = fwupd_json_parser_load_from_stream2(json_parser,
							stream,
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
	g_autoptr(FuInputStream) stream = NULL;
	GBytes *json_blob;
	g_autofree gchar *fn = NULL;

	fn = fu_engine_emulator_phase_to_filename(composite_cnt, phase, write_cnt);
	json_blob = g_hash_table_lookup(self->phase_blobs, fn);
	if (json_blob == NULL) {
		g_debug("emulator not loading %s, as not found", fn);
		return TRUE;
	}

	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(json_blob));
	g_debug("emulator loading %s", fn);
	return fu_engine_emulator_load_json_from_stream(self, stream, error);
}

/* what identifies a device, ignoring when it was created; this is %NULL for a device with
 * nothing that tells it apart from other devices of the same type */
static gchar *
fu_engine_emulator_json_device_identity(FwupdJsonObject *json_device)
{
	const gchar *gtype = fwupd_json_object_get_string(json_device, "GType", NULL);
	const gchar *platform_id = fwupd_json_object_get_string(json_device, "PlatformId", NULL);
	const gchar *backend_id = fwupd_json_object_get_string(json_device, "BackendId", NULL);
	gint64 vid = 0;
	gint64 pid = 0;

	if ((platform_id == NULL || platform_id[0] == '\0') &&
	    (backend_id == NULL || backend_id[0] == '\0'))
		return NULL;
	(void)fwupd_json_object_get_integer_with_default(json_device, "IdVendor", &vid, 0, NULL);
	(void)fwupd_json_object_get_integer_with_default(json_device, "IdProduct", &pid, 0, NULL);
	return g_strdup_printf("%s|%s|%s|%" G_GINT64_FORMAT "|%" G_GINT64_FORMAT,
			       gtype != NULL ? gtype : "",
			       platform_id != NULL ? platform_id : "",
			       backend_id != NULL ? backend_id : "",
			       vid,
			       pid);
}

/* the identity of a device in a snapshot, which does not change between snapshots; this is
 * %NULL if the device cannot be told apart from others, as the creation time alone is not
 * enough to know it is the same device */
static gchar *
fu_engine_emulator_json_device_key(FwupdJsonObject *json_device)
{
	const gchar *created = fwupd_json_object_get_string(json_device, "Created", NULL);
	g_autofree gchar *identity = NULL;

	if (created == NULL)
		return NULL;

	/* Created only has a resolution of one second with older versions of GLib, so a hub and
	 * the HID it exposes can have the same value */
	identity = fu_engine_emulator_json_device_identity(json_device);
	if (identity == NULL)
		return NULL;
	return g_strdup_printf("%s|%s", created, identity);
}

/* a copy of @json_new, but with the events of @json_old inserted before the new events */
static FwupdJsonObject *
fu_engine_emulator_json_device_merge(FwupdJsonObject *json_old,
				     FwupdJsonObject *json_new,
				     GError **error)
{
	g_autoptr(FwupdJsonArray) json_events = fwupd_json_array_new();
	g_autoptr(FwupdJsonArray) json_events_new = NULL;
	g_autoptr(FwupdJsonArray) json_events_old = NULL;
	g_autoptr(FwupdJsonObject) json_merged = fwupd_json_object_new();

	/* everything except the events comes from the newest snapshot */
	for (guint i = 0; i < fwupd_json_object_get_size(json_new); i++) {
		const gchar *key;
		g_autoptr(FwupdJsonNode) json_node = NULL;
		key = fwupd_json_object_get_key_for_index(json_new, i, error);
		if (key == NULL)
			return NULL;
		if (g_strcmp0(key, "Events") == 0)
			continue;
		json_node = fwupd_json_object_get_node_for_index(json_new, i, error);
		if (json_node == NULL)
			return NULL;
		fwupd_json_object_add_node(json_merged, key, json_node);
	}

	/* old events first, so the order of the original events is kept */
	json_events_old = fwupd_json_object_get_array(json_old, "Events", NULL);
	json_events_new = fwupd_json_object_get_array(json_new, "Events", NULL);
	for (guint i = 0; json_events_old != NULL && i < fwupd_json_array_get_size(json_events_old);
	     i++) {
		g_autoptr(FwupdJsonNode) json_node = NULL;
		json_node = fwupd_json_array_get_node(json_events_old, i, error);
		if (json_node == NULL)
			return NULL;
		fwupd_json_array_add_node(json_events, json_node);
	}
	for (guint i = 0; json_events_new != NULL && i < fwupd_json_array_get_size(json_events_new);
	     i++) {
		g_autoptr(FwupdJsonNode) json_node = NULL;
		json_node = fwupd_json_array_get_node(json_events_new, i, error);
		if (json_node == NULL)
			return NULL;
		fwupd_json_array_add_node(json_events, json_node);
	}
	if (fwupd_json_array_get_size(json_events) > 0)
		fwupd_json_object_add_array(json_merged, "Events", json_events);
	return g_steal_pointer(&json_merged);
}

/* the events of @json_old that is the same device as @json_new are merged into @json_new */
static FwupdJsonObject *
fu_engine_emulator_json_device_find_and_merge(FwupdJsonArray *json_devices_old,
					      FwupdJsonObject *json_new,
					      GError **error)
{
	g_autofree gchar *key_new = fu_engine_emulator_json_device_key(json_new);

	if (key_new == NULL || json_devices_old == NULL)
		return fwupd_json_object_ref(json_new);
	for (guint i = 0; i < fwupd_json_array_get_size(json_devices_old); i++) {
		g_autofree gchar *key_old = NULL;
		g_autoptr(FwupdJsonObject) json_old = NULL;
		json_old = fwupd_json_array_get_object(json_devices_old, i, error);
		if (json_old == NULL)
			return NULL;
		key_old = fu_engine_emulator_json_device_key(json_old);
		if (g_strcmp0(key_old, key_new) == 0)
			return fu_engine_emulator_json_device_merge(json_old, json_new, error);
	}
	return fwupd_json_object_ref(json_new);
}

/* if @blob_old is set, the events it contains for each device are kept and the new events
 * are appended -- this is required as the events are cleared after every snapshot */
static gboolean
fu_engine_emulator_to_json(FuEngineEmulator *self,
			   GPtrArray *devices,
			   GBytes *blob_old,
			   FwupdJsonObject *json_obj,
			   GError **error)
{
	g_autoptr(FwupdJsonArray) json_arr = fwupd_json_array_new();
	g_autoptr(FwupdJsonArray) json_devices_old = NULL;
	g_autoptr(GHashTable) identities_new =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	/* the old snapshot */
	if (blob_old != NULL) {
		g_autoptr(FwupdJsonNode) json_node_old = NULL;
		g_autoptr(FwupdJsonObject) json_obj_old = NULL;
		g_autoptr(FwupdJsonParser) json_parser = fu_engine_emulator_json_parser_new();

		json_node_old = fwupd_json_parser_load_from_bytes(json_parser,
								  blob_old,
								  FWUPD_JSON_LOAD_FLAG_NONE,
								  error);
		if (json_node_old == NULL)
			return FALSE;
		json_obj_old = fwupd_json_node_get_object(json_node_old, error);
		if (json_obj_old == NULL)
			return FALSE;
		json_devices_old = fwupd_json_object_get_array(json_obj_old, "UsbDevices", error);
		if (json_devices_old == NULL)
			return FALSE;
	}

	/* not always correct, but we want to remain compatible with all the old emulation files */
	fwupd_json_object_add_string(json_obj, "FwupdVersion", PACKAGE_VERSION);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		g_autoptr(FwupdJsonObject) json_device = fwupd_json_object_new();
		g_autoptr(FwupdJsonObject) json_device_merged = NULL;
		gchar *identity_new;

		/* interesting? */
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG))
			continue;
		fu_device_add_json(device, json_device, FWUPD_CODEC_FLAG_NONE);
		identity_new = fu_engine_emulator_json_device_identity(json_device);
		if (identity_new != NULL)
			g_hash_table_add(identities_new, identity_new);

		/* a device without an identity is never merged with an old one */
		json_device_merged = fu_engine_emulator_json_device_find_and_merge(json_devices_old,
										   json_device,
										   error);
		if (json_device_merged == NULL)
			return FALSE;
		fwupd_json_array_add_object(json_arr, json_device_merged);
	}

	/* devices removed since the last snapshot, e.g. a HID that went away, are still part of
	 * the setup; a device that came back is replaced as it has been enumerated again, but
	 * only if it can be told apart from other devices of the same type */
	for (guint i = 0;
	     json_devices_old != NULL && i < fwupd_json_array_get_size(json_devices_old);
	     i++) {
		g_autofree gchar *identity_old = NULL;
		g_autoptr(FwupdJsonObject) json_old = NULL;
		json_old = fwupd_json_array_get_object(json_devices_old, i, error);
		if (json_old == NULL)
			return FALSE;
		identity_old = fu_engine_emulator_json_device_identity(json_old);
		if (identity_old != NULL && g_hash_table_contains(identities_new, identity_old))
			continue;
		fwupd_json_array_add_object(json_arr, json_old);
	}
	fwupd_json_object_add_array(json_obj, "UsbDevices", json_arr);

	/* we've recorded these, now drop them */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_EMULATION_TAG))
			continue;
		fu_device_clear_events(device);
	}
	return TRUE;
}

gboolean
fu_engine_emulator_save_phase(FuEngineEmulator *self,
			      guint composite_cnt,
			      FuEngineEmulatorPhase phase,
			      guint write_cnt,
			      GError **error)
{
	GBytes *blob_old;
	GBytes *blob_merge = NULL;
	g_autofree gchar *blob_new_safe = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(GBytes) blob_new = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();

	/* all devices in all backends */
	devices = fu_engine_get_devices(self->engine, error);
	if (devices == NULL)
		return FALSE;

	/* the events are cleared after every snapshot, so when the setup is saved more than once
	 * in a row, e.g. as each device is added or replugged, keep the events from the previous
	 * snapshot -- but a setup saved after any other phase replaces the old one */
	fn = fu_engine_emulator_phase_to_filename(composite_cnt, phase, write_cnt);
	blob_old = g_hash_table_lookup(self->phase_blobs, fn);
	if (phase == FU_ENGINE_EMULATOR_PHASE_SETUP && g_strcmp0(self->last_save_fn, fn) == 0)
		blob_merge = blob_old;
	if (!fu_engine_emulator_to_json(self, devices, blob_merge, json_obj, error))
		return FALSE;
	g_free(self->last_save_fn);
	self->last_save_fn = g_strdup(fn);

	g_debug("saving %s", fn);
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

/* events can be produced after the last snapshot, e.g. when a plugin uses the device from
 * ->device_added(), so save them if the setup is still the last thing that was saved */
gboolean
fu_engine_emulator_save_setup_pending(FuEngineEmulator *self,
				      guint composite_cnt,
				      guint write_cnt,
				      GError **error)
{
	g_autofree gchar *fn = fu_engine_emulator_phase_to_filename(composite_cnt,
								    FU_ENGINE_EMULATOR_PHASE_SETUP,
								    write_cnt);
	g_autoptr(GError) error_local = NULL;

	if (g_strcmp0(self->last_save_fn, fn) != 0)
		return TRUE;
	if (!fu_engine_emulator_save_phase(self,
					   composite_cnt,
					   FU_ENGINE_EMULATOR_PHASE_SETUP,
					   write_cnt,
					   &error_local)) {
		/* no devices are connected, e.g. all removed since the last snapshot, which
		 * leaves the saved setup untouched */
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
			g_debug("not saving pending setup events: %s", error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_engine_emulator_load_phases(FuEngineEmulator *self,
			       FuFirmware *archive,
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
		blob = fu_firmware_get_image_by_id_bytes(archive, fn, NULL);
		if (blob == NULL || g_bytes_get_size(blob) == 0)
			continue;
		*got_json = TRUE;
		g_info("emulation for phase %s [%u]",
		       fu_engine_emulator_phase_to_string(phase),
		       write_cnt);
		if (composite_cnt == 0 && write_cnt == FU_ENGINE_EMULATOR_WRITE_COUNT_DEFAULT &&
		    phase == FU_ENGINE_EMULATOR_PHASE_SETUP) {
			g_autoptr(FuInputStream) stream = NULL;
			stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(blob));
			if (!fu_engine_emulator_load_json_from_stream(self, stream, error))
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
fu_engine_emulator_load(FuEngineEmulator *self, FuInputStream *stream, GError **error)
{
	gboolean got_json = FALSE;
	const gchar *json_empty = "{\"UsbDevices\":[]}";
	g_autoptr(FuFirmware) archive = fu_zip_firmware_new();
	g_autoptr(FuFirmware) img_composite = NULL;
	g_autoptr(FuInputStream) stream_empty = NULL;
	g_autoptr(GError) error_archive = NULL;

	g_return_val_if_fail(FU_IS_ENGINE_EMULATOR(self), FALSE);
	g_return_val_if_fail(FWUPD_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* unload any existing devices */
	stream_empty = FWUPD_INPUT_STREAM(
	    fwupd_memory_input_stream_new_from_data(json_empty, strlen(json_empty), NULL));
	if (!fu_engine_emulator_load_json_from_stream(self, stream_empty, error))
		return FALSE;
	g_hash_table_remove_all(self->phase_blobs);

	/* load archive */
	if (!fu_firmware_parse_stream(archive,
				      stream,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error_archive)) {
		g_debug("no archive found, using JSON as phase setup: %s", error_archive->message);
		return fu_engine_emulator_load_json_from_stream(self, stream, error);
	}

	/* fix up some invalid names */
	img_composite = fu_firmware_get_image_by_id(archive, "*:composite-cleanup.json", NULL);
	if (img_composite != NULL) {
		g_debug("fixing composite cleanup image ID: %s", fu_firmware_get_id(img_composite));
		fu_firmware_set_id(img_composite, "composite-cleanup.json");
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
	g_free(self->last_save_fn);
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
