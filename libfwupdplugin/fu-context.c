/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuContext"

#include "config.h"

#include "fu-context-private.h"
#include "fu-hwids.h"
#include "fu-smbios-private.h"

/**
 * SECTION:fu-context
 * @short_description: a context shared between all the plugins and the daemon
 *
 * An object that represents the shared system state. This object is shared
 * between the engine, the plugins and the devices.
 */

typedef struct {
	FuHwids			*hwids;
	FuSmbios		*smbios;
	FuQuirks		*quirks;
	GHashTable		*runtime_versions;
	GHashTable		*compile_versions;
	GPtrArray		*udev_subsystems;
	GHashTable		*firmware_gtypes;
	FuBatteryState		 battery_state;
	guint			 battery_level;
	guint			 battery_threshold;
} FuContextPrivate;

enum {
	SIGNAL_SECURITY_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_BATTERY_STATE,
	PROP_BATTERY_LEVEL,
	PROP_BATTERY_THRESHOLD,
	PROP_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (FuContext, fu_context, G_TYPE_OBJECT)

#define GET_PRIVATE(o) (fu_context_get_instance_private (o))

/**
 * fu_context_get_smbios_string:
 * @self: A #FuContext
 * @structure_type: A SMBIOS structure type, e.g. %FU_SMBIOS_STRUCTURE_TYPE_BIOS
 * @offset: A SMBIOS offset
 *
 * Gets a hardware SMBIOS string.
 *
 * The @type and @offset can be referenced from the DMTF SMBIOS specification:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.1.1.pdf
 *
 * Returns: A string, or %NULL
 *
 * Since: 1.6.0
 **/
const gchar *
fu_context_get_smbios_string (FuContext *self, guint8 structure_type, guint8 offset)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), NULL);
	return fu_smbios_get_string (priv->smbios, structure_type, offset, NULL);
}

/**
 * fu_context_get_smbios_data:
 * @self: A #FuContext
 * @structure_type: A SMBIOS structure type, e.g. %FU_SMBIOS_STRUCTURE_TYPE_BIOS
 *
 * Gets a hardware SMBIOS data.
 *
 * Returns: (transfer full): A #GBytes, or %NULL
 *
 * Since: 1.6.0
 **/
GBytes *
fu_context_get_smbios_data (FuContext *self, guint8 structure_type)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), NULL);
	return fu_smbios_get_data (priv->smbios, structure_type, NULL);
}

/**
 * fu_context_get_smbios_integer:
 * @self: A #FuContext
 * @type: A structure type, e.g. %FU_SMBIOS_STRUCTURE_TYPE_BIOS
 * @offset: A structure offset
 *
 * Reads an integer value from the SMBIOS string table of a specific structure.
 *
 * The @type and @offset can be referenced from the DMTF SMBIOS specification:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.1.1.pdf
 *
 * Returns: an integer, or %G_MAXUINT if invalid or not found
 *
 * Since: 1.6.0
 **/
guint
fu_context_get_smbios_integer (FuContext *self, guint8 type, guint8 offset)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), G_MAXUINT);
	return fu_smbios_get_integer (priv->smbios, type, offset, NULL);
}

/**
 * fu_context_has_hwid_guid:
 * @self: A #FuContext
 * @guid: A GUID, e.g. `059eb22d-6dc7-59af-abd3-94bbe017f67c`
 *
 * Finds out if a hardware GUID exists.
 *
 * Returns: %TRUE if the GUID exists
 *
 * Since: 1.6.0
 **/
gboolean
fu_context_has_hwid_guid (FuContext *self, const gchar *guid)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), FALSE);
	return fu_hwids_has_guid (priv->hwids, guid);
}

/**
 * fu_context_get_hwid_guids:
 * @self: A #FuContext
 *
 * Returns all the HWIDs defined in the system. All hardware IDs on a
 * specific system can be shown using the `fwupdmgr hwids` command.
 *
 * Returns: (transfer none) (element-type utf8): An array of GUIDs
 *
 * Since: 1.6.0
 **/
GPtrArray *
fu_context_get_hwid_guids (FuContext *self)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), NULL);
	return fu_hwids_get_guids (priv->hwids);
}

/**
 * fu_context_get_hwid_value:
 * @self: A #FuContext
 * @key: A DMI ID, e.g. `BiosVersion`
 *
 * Gets the cached value for one specific key that is valid ASCII and suitable
 * for display.
 *
 * Returns: the string, e.g. `1.2.3`, or %NULL if not found
 *
 * Since: 1.6.0
 **/
const gchar *
fu_context_get_hwid_value (FuContext *self, const gchar *key)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	return fu_hwids_get_value (priv->hwids, key);
}

/**
 * fu_context_get_hwid_replace_value:
 * @self: A #FuContext
 * @keys: A key, e.g. `HardwareID-3` or %FU_HWIDS_KEY_PRODUCT_SKU
 * @error: A #GError or %NULL
 *
 * Gets the replacement value for a specific key. All hardware IDs on a
 * specific system can be shown using the `fwupdmgr hwids` command.
 *
 * Returns: (transfer full): a string, or %NULL for error.
 *
 * Since: 1.6.0
 **/
gchar *
fu_context_get_hwid_replace_value (FuContext *self, const gchar *keys, GError **error)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), NULL);
	g_return_val_if_fail (keys != NULL, NULL);
	return fu_hwids_get_replace_values (priv->hwids, keys, error);
}

/**
 * fu_context_add_runtime_version:
 * @self: A #FuContext
 * @component_id: An AppStream component id, e.g. "org.gnome.Software"
 * @version: A version string, e.g. "1.2.3"
 *
 * Sets a runtime version of a specific dependency.
 *
 * Since: 1.6.0
 **/
void
fu_context_add_runtime_version (FuContext *self,
				const gchar *component_id,
				const gchar *version)
{
	FuContextPrivate *priv = GET_PRIVATE (self);

	g_return_if_fail (FU_IS_CONTEXT (self));
	g_return_if_fail (component_id != NULL);
	g_return_if_fail (version != NULL);

	if (priv->runtime_versions == NULL)
		return;
	g_hash_table_insert (priv->runtime_versions,
			     g_strdup (component_id),
			     g_strdup (version));
}

/**
 * fu_context_set_runtime_versions:
 * @self: A #FuContext
 * @runtime_versions: A #GHashTables
 *
 * Sets the runtime versions for a plugin
 *
 * Since: 1.6.0
 **/
void
fu_context_set_runtime_versions (FuContext *self, GHashTable *runtime_versions)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_CONTEXT (self));
	g_return_if_fail (runtime_versions != NULL);
	priv->runtime_versions = g_hash_table_ref (runtime_versions);
}

/**
 * fu_context_add_compile_version:
 * @self: A #FuContext
 * @component_id: An AppStream component id, e.g. "org.gnome.Software"
 * @version: A version string, e.g. "1.2.3"
 *
 * Sets a compile-time version of a specific dependency.
 *
 * Since: 1.6.0
 **/
void
fu_context_add_compile_version (FuContext *self,
				const gchar *component_id,
				const gchar *version)
{
	FuContextPrivate *priv = GET_PRIVATE (self);

	g_return_if_fail (FU_IS_CONTEXT (self));
	g_return_if_fail (component_id != NULL);
	g_return_if_fail (version != NULL);

	if (priv->compile_versions == NULL)
		return;
	g_hash_table_insert (priv->compile_versions,
			     g_strdup (component_id),
			     g_strdup (version));
}

/**
 * fu_context_set_compile_versions:
 * @self: A #FuContext
 * @compile_versions: A #GHashTables
 *
 * Sets the compile time versions for a plugin
 *
 * Since: 1.6.0
 **/
void
fu_context_set_compile_versions (FuContext *self, GHashTable *compile_versions)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_CONTEXT (self));
	g_return_if_fail (compile_versions != NULL);
	priv->compile_versions = g_hash_table_ref (compile_versions);
}

/**
 * fu_context_add_udev_subsystem:
 * @self: a #FuContext
 * @subsystem: a subsystem name, e.g. `pciport`
 *
 * Registers the udev subsystem to be watched by the daemon.
 *
 * Plugins can use this method only in fu_plugin_init()
 *
 * Since: 1.6.0
 **/
void
fu_context_add_udev_subsystem (FuContext *self, const gchar *subsystem)
{
	FuContextPrivate *priv = GET_PRIVATE (self);

	g_return_if_fail (FU_IS_CONTEXT (self));
	g_return_if_fail (subsystem != NULL);

	for (guint i = 0; i < priv->udev_subsystems->len; i++) {
		const gchar *subsystem_tmp = g_ptr_array_index (priv->udev_subsystems, i);
		if (g_strcmp0 (subsystem_tmp, subsystem) == 0)
			return;
	}
	g_debug ("added udev subsystem watch of %s", subsystem);
	g_ptr_array_add (priv->udev_subsystems, g_strdup (subsystem));
}

/**
 * fu_context_get_udev_subsystems:
 * @self: A #FuContext
 *
 * Gets the udev subsystems required by all plugins.
 *
 * Returns: (transfer none) (element-type utf8): List of subsystems
 *
 * Since: 1.6.0
 **/
GPtrArray *
fu_context_get_udev_subsystems (FuContext *self)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), NULL);
	return priv->udev_subsystems;
}

/**
 * fu_context_add_firmware_gtype:
 * @self: a #FuContext
 * @id: (nullable): An optional string describing the type, e.g. "ihex"
 * @gtype: a #GType e.g. `FU_TYPE_FOO_FIRMWARE`
 *
 * Adds a firmware #GType which is used when creating devices. If @id is not
 * specified then it is guessed using the #GType name.
 *
 * Plugins can use this method only in fu_plugin_init()
 *
 * Since: 1.6.0
 **/
void
fu_context_add_firmware_gtype (FuContext *self, const gchar *id, GType gtype)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_CONTEXT (self));
	g_return_if_fail (id != NULL);
	g_return_if_fail (gtype != G_TYPE_INVALID);
	g_hash_table_insert (priv->firmware_gtypes, g_strdup (id), GSIZE_TO_POINTER (gtype));
}

/**
 * fu_context_get_firmware_gtype_by_id:
 * @self: a #FuContext
 * @id: An string describing the type, e.g. "ihex"
 *
 * Returns the #GType using the firmware @id.
 *
 * Returns: a #GType, or %G_TYPE_INVALID
 *
 * Since: 1.6.0
 **/
GType
fu_context_get_firmware_gtype_by_id (FuContext *self, const gchar *id)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), G_TYPE_INVALID);
	g_return_val_if_fail (id != NULL, G_TYPE_INVALID);
	return GPOINTER_TO_SIZE (g_hash_table_lookup (priv->firmware_gtypes, id));
}

static gint
fu_context_gtypes_sort_cb (gconstpointer a, gconstpointer b)
{
	const gchar *stra = *((const gchar **) a);
	const gchar *strb = *((const gchar **) b);
	return g_strcmp0 (stra, strb);
}

/**
 * fu_context_get_firmware_gtype_ids:
 * @self: a #FuContext
 *
 * Returns all the firmware #GType IDs.
 *
 * Returns: (transfer none) (element-type utf8): List of subsystems
 *
 * Since: 1.6.0
 **/
GPtrArray *
fu_context_get_firmware_gtype_ids (FuContext *self)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	GPtrArray *firmware_gtypes = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GList) keys = g_hash_table_get_keys (priv->firmware_gtypes);

	g_return_val_if_fail (FU_IS_CONTEXT (self), NULL);

	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *id = l->data;
		g_ptr_array_add (firmware_gtypes, g_strdup (id));
	}
	g_ptr_array_sort (firmware_gtypes, fu_context_gtypes_sort_cb);
	return firmware_gtypes;
}

/**
 * fu_context_add_quirk_key:
 * @self: a #FuContext
 * @key: A quirk string, e.g. `DfuVersion`
 *
 * Adds a possible quirk key. If added by a plugin it should be namespaced
 * using the plugin name, where possible.
 *
 * Plugins can use this method only in fu_plugin_init()
 *
 * Since: 1.6.0
 **/
void
fu_context_add_quirk_key (FuContext *self, const gchar *key)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_CONTEXT (self));
	g_return_if_fail (key != NULL);
	if (priv->quirks == NULL)
		return;
	fu_quirks_add_possible_key (priv->quirks, key);
}

/**
 * fu_context_lookup_quirk_by_id:
 * @self: A #FuContext
 * @guid: GUID to lookup
 * @key: An ID to match the entry, e.g. "Summary"
 *
 * Looks up an entry in the hardware database using a string value.
 *
 * Returns: (transfer none): values from the database, or %NULL if not found
 *
 * Since: 1.6.0
 **/
const gchar *
fu_context_lookup_quirk_by_id (FuContext *self, const gchar *guid, const gchar *key)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), NULL);

	/* exact ID */
	return fu_quirks_lookup_by_id (priv->quirks, guid, key);
}

/**
 * fu_context_lookup_quirk_by_id_iter:
 * @self: A #FuContext
 * @guid: GUID to lookup
 * @iter_cb: (scope async): A #FuContextLookupIter
 * @user_data: user data passed to @iter_cb
 *
 * Looks up all entries in the hardware database using a GUID value.
 *
 * Returns: %TRUE if the ID was found, and @iter was called
 *
 * Since: 1.6.0
 **/
gboolean
fu_context_lookup_quirk_by_id_iter (FuContext *self, const gchar *guid,
				    FuContextLookupIter iter_cb, gpointer user_data)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), FALSE);
	g_return_val_if_fail (guid != NULL, FALSE);
	g_return_val_if_fail (iter_cb != NULL, FALSE);
	return fu_quirks_lookup_by_id_iter (priv->quirks, guid, (FuQuirksIter) iter_cb, user_data);
}

/**
 * fu_context_security_changed:
 * @self: A #FuContext
 *
 * Informs the daemon that the HSI state may have changed.
 *
 * Since: 1.6.0
 **/
void
fu_context_security_changed (FuContext *self)
{
	g_return_if_fail (FU_IS_CONTEXT (self));
	g_signal_emit (self, signals[SIGNAL_SECURITY_CHANGED], 0);
}

/**
 * fu_context_load_hwinfo:
 * @self: A #FuContext
 * @error: (nullable): A #GError, or %NULL
 *
 * Loads all hardware information parts of the context.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.0
 **/
gboolean
fu_context_load_hwinfo (FuContext *self, GError **error)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GError) error_smbios = NULL;
	g_autoptr(GError) error_hwids = NULL;

	g_return_val_if_fail (FU_IS_CONTEXT (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!fu_smbios_setup (priv->smbios, &error_smbios))
		g_warning ("Failed to load SMBIOS: %s", error_smbios->message);
	if (!fu_hwids_setup (priv->hwids, priv->smbios, &error_hwids))
		g_warning ("Failed to load HWIDs: %s", error_hwids->message);

	/* always */
	return TRUE;
}

/**
 * fu_context_load_quirks:
 * @self: A #FuContext
 * @error: (nullable): A #GError, or %NULL
 *
 * Loads all quirks into the context.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.0
 **/
gboolean
fu_context_load_quirks (FuContext *self, FuQuirksLoadFlags flags, GError **error)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (FU_IS_CONTEXT (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* rebuild silo if required */
	if (!fu_quirks_load (priv->quirks, flags, &error_local))
		g_warning ("Failed to load quirks: %s", error_local->message);

	/* always */
	return TRUE;
}

/**
 * fu_context_get_battery_state:
 * @self: A #FuContext
 *
 * Gets if the system is on battery power, e.g. UPS or laptop battery.
 *
 * Returns: a #FuBatteryState, e.g. %FU_BATTERY_STATE_DISCHARGING
 *
 * Since: 1.6.0
 **/
FuBatteryState
fu_context_get_battery_state (FuContext *self)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), FALSE);
	return priv->battery_state;
}

/**
 * fu_context_set_battery_state:
 * @self: A #FuContext
 * @battery_state: a #FuBatteryState, e.g. %FU_BATTERY_STATE_DISCHARGING
 *
 * Sets if the system is on battery power, e.g. UPS or laptop battery.
 *
 * Since: 1.6.0
 **/
void
fu_context_set_battery_state (FuContext *self, FuBatteryState battery_state)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_CONTEXT (self));
	if (priv->battery_state == battery_state)
		return;
	priv->battery_state = battery_state;
	g_debug ("battery state now %s",
		 fu_battery_state_to_string (battery_state));
	g_object_notify (G_OBJECT (self), "battery-state");
}

/**
 * fu_context_get_battery_level:
 * @self: A #FuContext
 *
 * Gets the system battery level in percent.
 *
 * Returns: percentage value, or %FU_BATTERY_VALUE_INVALID for unknown
 *
 * Since: 1.6.0
 **/
guint
fu_context_get_battery_level (FuContext *self)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), G_MAXUINT);
	return priv->battery_level;
}

/**
 * fu_context_set_battery_level:
 * @self: A #FuContext
 * @battery_level: value
 *
 * Sets the system battery level in percent.
 *
 * Since: 1.6.0
 **/
void
fu_context_set_battery_level (FuContext *self, guint battery_level)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_CONTEXT (self));
	g_return_if_fail (battery_level <= FU_BATTERY_VALUE_INVALID);
	if (priv->battery_level == battery_level)
		return;
	priv->battery_level = battery_level;
	g_debug ("battery level now %u", battery_level);
	g_object_notify (G_OBJECT (self), "battery-level");
}

/**
 * fu_context_get_battery_threshold:
 * @self: A #FuContext
 *
 * Gets the system battery threshold in percent.
 *
 * Returns: percentage value, or %FU_BATTERY_VALUE_INVALID for unknown
 *
 * Since: 1.6.0
 **/
guint
fu_context_get_battery_threshold (FuContext *self)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_CONTEXT (self), G_MAXUINT);
	return priv->battery_threshold;
}

/**
 * fu_context_set_battery_threshold:
 * @self: A #FuContext
 * @battery_threshold: value
 *
 * Sets the system battery threshold in percent.
 *
 * Since: 1.6.0
 **/
void
fu_context_set_battery_threshold (FuContext *self, guint battery_threshold)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_CONTEXT (self));
	g_return_if_fail (battery_threshold <= FU_BATTERY_VALUE_INVALID);
	if (priv->battery_threshold == battery_threshold)
		return;
	priv->battery_threshold = battery_threshold;
	g_debug ("battery threshold now %u", battery_threshold);
	g_object_notify (G_OBJECT (self), "battery-threshold");
}

static void
fu_context_get_property (GObject *object, guint prop_id,
			 GValue *value, GParamSpec *pspec)
{
	FuContext *self = FU_CONTEXT (object);
	FuContextPrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_BATTERY_STATE:
		g_value_set_uint (value, priv->battery_state);
		break;
	case PROP_BATTERY_LEVEL:
		g_value_set_uint (value, priv->battery_level);
		break;
	case PROP_BATTERY_THRESHOLD:
		g_value_set_uint (value, priv->battery_threshold);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_context_set_property (GObject *object, guint prop_id,
			 const GValue *value, GParamSpec *pspec)
{
	FuContext *self = FU_CONTEXT (object);
	switch (prop_id) {
	case PROP_BATTERY_STATE:
		fu_context_set_battery_state (self, g_value_get_uint (value));
		break;
	case PROP_BATTERY_LEVEL:
		fu_context_set_battery_level (self, g_value_get_uint (value));
		break;
	case PROP_BATTERY_THRESHOLD:
		fu_context_set_battery_threshold (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_context_finalize (GObject *object)
{
	FuContext *self = FU_CONTEXT (object);
	FuContextPrivate *priv = GET_PRIVATE (self);

	g_object_unref (priv->hwids);
	g_object_unref (priv->quirks);
	g_object_unref (priv->smbios);
	g_hash_table_unref (priv->firmware_gtypes);
	g_ptr_array_unref (priv->udev_subsystems);

	G_OBJECT_CLASS (fu_context_parent_class)->finalize (object);
}

static void
fu_context_class_init (FuContextClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->get_property = fu_context_get_property;
	object_class->set_property = fu_context_set_property;

	pspec = g_param_spec_uint ("battery-state", NULL, NULL,
				   FU_BATTERY_STATE_UNKNOWN,
				   FU_BATTERY_STATE_LAST,
				   FU_BATTERY_STATE_UNKNOWN,
				   G_PARAM_READWRITE |
				   G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_BATTERY_STATE, pspec);

	pspec = g_param_spec_uint ("battery-level", NULL, NULL,
				   0,
				   FU_BATTERY_VALUE_INVALID,
				   FU_BATTERY_VALUE_INVALID,
				   G_PARAM_READWRITE |
				   G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_BATTERY_LEVEL, pspec);

	pspec = g_param_spec_uint ("battery-threshold", NULL, NULL,
				   0,
				   FU_BATTERY_VALUE_INVALID,
				   FU_BATTERY_VALUE_INVALID,
				   G_PARAM_READWRITE |
				   G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_BATTERY_THRESHOLD, pspec);

	signals[SIGNAL_SECURITY_CHANGED] =
		g_signal_new ("security-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuContextClass, security_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	object_class->finalize = fu_context_finalize;
}

static void
fu_context_init (FuContext *self)
{
	FuContextPrivate *priv = GET_PRIVATE (self);
	priv->battery_level = FU_BATTERY_VALUE_INVALID;
	priv->battery_threshold = FU_BATTERY_VALUE_INVALID;
	priv->smbios = fu_smbios_new ();
	priv->hwids = fu_hwids_new ();
	priv->udev_subsystems = g_ptr_array_new_with_free_func (g_free);
	priv->firmware_gtypes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	priv->quirks = fu_quirks_new ();
}

/**
 * fu_context_new:
 *
 * Creates a new #FuContext
 *
 * Returns: the object
 *
 * Since: 1.6.0
 **/
FuContext *
fu_context_new (void)
{
	return FU_CONTEXT (g_object_new (FU_TYPE_CONTEXT, NULL));
}
