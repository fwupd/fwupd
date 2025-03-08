/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

#include "fu-bios-settings.h"
#include "fu-common-struct.h"
#include "fu-common.h"
#include "fu-efi-hard-drive-device-path.h"
#include "fu-efivars.h"
#include "fu-firmware.h"
#include "fu-smbios-struct.h"

#define FU_TYPE_CONTEXT (fu_context_get_type())
G_DECLARE_DERIVABLE_TYPE(FuContext, fu_context, FU, CONTEXT, GObject)

struct _FuContextClass {
	GObjectClass parent_class;
	/* signals */
	void (*security_changed)(FuContext *self);
	void (*housekeeping)(FuContext *self);
};

/**
 * FuContextQuirkSource:
 *
 * The source of the quirk data, ordered by how good the data is.
 **/
typedef enum {
	/**
	 * FU_CONTEXT_QUIRK_SOURCE_DEVICE:
	 *
	 * From the device itself, perhaps a USB descriptor.
	 *
	 * Since: 2.0.2
	 **/
	FU_CONTEXT_QUIRK_SOURCE_DEVICE,
	/**
	 * FU_CONTEXT_QUIRK_SOURCE_FILE:
	 *
	 * From an internal `.quirk` file.
	 *
	 * Since: 2.0.2
	 **/
	FU_CONTEXT_QUIRK_SOURCE_FILE,
	/**
	 * FU_CONTEXT_QUIRK_SOURCE_DB:
	 *
	 * From the database, populated from `usb.ids` and `pci.ids`.
	 *
	 * Since: 2.0.2
	 **/
	FU_CONTEXT_QUIRK_SOURCE_DB,
	/**
	 * FU_CONTEXT_QUIRK_SOURCE_FALLBACK:
	 *
	 * A good fallback, perhaps from the PCI class information.
	 *
	 * Since: 2.0.2
	 **/
	FU_CONTEXT_QUIRK_SOURCE_FALLBACK,
} FuContextQuirkSource;

/**
 * FuContextLookupIter:
 * @self: a #FuContext
 * @key: a key
 * @value: a value
 * @source: a #FuContextQuirkSource, e.g. %FU_CONTEXT_QUIRK_SOURCE_DB
 * @user_data: user data
 *
 * The context lookup iteration callback.
 */
typedef void (*FuContextLookupIter)(FuContext *self,
				    const gchar *key,
				    const gchar *value,
				    FuContextQuirkSource source,
				    gpointer user_data);

/**
 * FuContextFlags:
 *
 * The context flags.
 **/
typedef enum {
	/**
	 * FU_CONTEXT_FLAG_NONE:
	 *
	 * No flags set.
	 *
	 * Since: 1.8.5
	 **/
	FU_CONTEXT_FLAG_NONE = 0,
	/**
	 * FU_CONTEXT_FLAG_SAVE_EVENTS:
	 *
	 * Save events so that they can be replayed to emulate devices.
	 *
	 * Since: 1.8.5
	 **/
	FU_CONTEXT_FLAG_SAVE_EVENTS = 1u << 0,
	/**
	 * FU_CONTEXT_FLAG_SYSTEM_INHIBIT:
	 *
	 * All devices are not updatable due to a system-wide inhibit.
	 *
	 * Since: 1.8.10
	 **/
	FU_CONTEXT_FLAG_SYSTEM_INHIBIT = 1u << 1,
	/**
	 * FU_CONTEXT_FLAG_LOADED_HWINFO:
	 *
	 * Hardware information has been loaded with a call to fu_context_load_hwinfo().
	 *
	 * Since: 1.9.10
	 **/
	FU_CONTEXT_FLAG_LOADED_HWINFO = 1u << 2,
	/**
	 * FU_CONTEXT_FLAG_INHIBIT_VOLUME_MOUNT:
	 *
	 * Do not allow mounting volumes, usually set in self tests.
	 *
	 * Since: 2.0.2
	 **/
	FU_CONTEXT_FLAG_INHIBIT_VOLUME_MOUNT = 1u << 3,
	/**
	 * FU_CONTEXT_FLAG_FDE_BITLOCKER:
	 *
	 * Bitlocker style full disk encryption is in use
	 *
	 * Since: 2.0.5
	 **/
	FU_CONTEXT_FLAG_FDE_BITLOCKER = 1u << 4,
	/**
	 * FU_CONTEXT_FLAG_FDE_SNAPD:
	 *
	 * Snapd style full disk encryption is in use
	 *
	 * Since: 2.0.5
	 **/
	FU_CONTEXT_FLAG_FDE_SNAPD = 1u << 5,

	/**
	 * FU_CONTEXT_FLAG_LOADED_UNKNOWN:
	 *
	 * Unknown flag value.
	 *
	 * Since: 2.0.0
	 **/
	FU_CONTEXT_FLAG_LOADED_UNKNOWN = G_MAXUINT64,
} FuContextFlags;

void
fu_context_add_flag(FuContext *context, FuContextFlags flag) G_GNUC_NON_NULL(1);
void
fu_context_remove_flag(FuContext *context, FuContextFlags flag) G_GNUC_NON_NULL(1);
gboolean
fu_context_has_flag(FuContext *context, FuContextFlags flag) G_GNUC_NON_NULL(1);

const gchar *
fu_context_get_smbios_string(FuContext *self,
			     guint8 type,
			     guint8 length,
			     guint8 offset,
			     GError **error) G_GNUC_NON_NULL(1);
guint
fu_context_get_smbios_integer(FuContext *self,
			      guint8 type,
			      guint8 length,
			      guint8 offset,
			      GError **error) G_GNUC_NON_NULL(1);
GPtrArray *
fu_context_get_smbios_data(FuContext *self, guint8 type, guint8 length, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_context_has_hwid_guid(FuContext *self, const gchar *guid) G_GNUC_NON_NULL(1);
GPtrArray *
fu_context_get_hwid_guids(FuContext *self) G_GNUC_NON_NULL(1);
gboolean
fu_context_has_hwid_flag(FuContext *self, const gchar *flag) G_GNUC_NON_NULL(1, 2);
const gchar *
fu_context_get_hwid_value(FuContext *self, const gchar *key) G_GNUC_NON_NULL(1, 2);
gchar *
fu_context_get_hwid_replace_value(FuContext *self,
				  const gchar *keys,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
void
fu_context_add_runtime_version(FuContext *self, const gchar *component_id, const gchar *version)
    G_GNUC_NON_NULL(1, 2, 3);
const gchar *
fu_context_get_runtime_version(FuContext *self, const gchar *component_id) G_GNUC_NON_NULL(1, 2);
void
fu_context_add_compile_version(FuContext *self, const gchar *component_id, const gchar *version)
    G_GNUC_NON_NULL(1, 2, 3);
const gchar *
fu_context_lookup_quirk_by_id(FuContext *self, const gchar *guid, const gchar *key)
    G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_context_lookup_quirk_by_id_iter(FuContext *self,
				   const gchar *guid,
				   const gchar *key,
				   FuContextLookupIter iter_cb,
				   gpointer user_data) G_GNUC_NON_NULL(1, 2);
void
fu_context_add_quirk_key(FuContext *self, const gchar *key) G_GNUC_NON_NULL(1, 2);
void
fu_context_security_changed(FuContext *self) G_GNUC_NON_NULL(1);

FuPowerState
fu_context_get_power_state(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_power_state(FuContext *self, FuPowerState power_state) G_GNUC_NON_NULL(1);
FuLidState
fu_context_get_lid_state(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_lid_state(FuContext *self, FuLidState lid_state) G_GNUC_NON_NULL(1);
FuDisplayState
fu_context_get_display_state(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_display_state(FuContext *self, FuDisplayState display_state) G_GNUC_NON_NULL(1);
guint
fu_context_get_battery_level(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_battery_level(FuContext *self, guint battery_level) G_GNUC_NON_NULL(1);
guint
fu_context_get_battery_threshold(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_battery_threshold(FuContext *self, guint battery_threshold) G_GNUC_NON_NULL(1);

FuBiosSettings *
fu_context_get_bios_settings(FuContext *self) G_GNUC_NON_NULL(1);
gboolean
fu_context_get_bios_setting_pending_reboot(FuContext *self) G_GNUC_NON_NULL(1);
FwupdBiosSetting *
fu_context_get_bios_setting(FuContext *self, const gchar *name) G_GNUC_NON_NULL(1, 2);

GPtrArray *
fu_context_get_esp_volumes(FuContext *self, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
FuVolume *
fu_context_get_default_esp(FuContext *self, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
FuVolume *
fu_context_get_esp_volume_by_hard_drive_device_path(FuContext *self,
						    FuEfiHardDriveDevicePath *dp,
						    GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);

FuFirmware *
fu_context_get_fdt(FuContext *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
FuSmbiosChassisKind
fu_context_get_chassis_kind(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_esp_location(FuContext *self, const gchar *location);
const gchar *
fu_context_get_esp_location(FuContext *self);
FuEfivars *
fu_context_get_efivars(FuContext *self) G_GNUC_NON_NULL(1);

/**
 * FuContextEspFileFlags:
 *
 * The flags to use when loading files in the ESP.
 **/
typedef enum {
	/**
	 * FU_CONTEXT_ESP_FILE_FLAG_NONE:
	 *
	 * No flags set.
	 *
	 * Since: 2.0.0
	 **/
	FU_CONTEXT_ESP_FILE_FLAG_NONE = 0,
	/**
	 * FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_FIRST_STAGE:
	 *
	 * Include 1st stage bootloaders like shim.
	 *
	 * Since: 2.0.0
	 **/
	FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_FIRST_STAGE = 1 << 0,
	/**
	 * FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_SECOND_STAGE:
	 *
	 * Include 2nd stage bootloaders like shim.
	 *
	 * Since: 2.0.0
	 **/
	FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_SECOND_STAGE = 1 << 1,
	/**
	 * FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_REVOCATIONS:
	 *
	 * Include revokcations, for example the `revocations.efi` file used by shim.
	 *
	 * Since: 2.0.0
	 **/
	FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_REVOCATIONS = 1 << 2,
	/**
	 * FU_CONTEXT_ESP_FILE_FLAG_UNKNOWN:
	 *
	 * Unknown flag value.
	 *
	 * Since: 2.0.0
	 **/
	FU_CONTEXT_ESP_FILE_FLAG_UNKNOWN = G_MAXUINT64,
} FuContextEspFileFlags;

GPtrArray *
fu_context_get_esp_files(FuContext *self, FuContextEspFileFlags flags, GError **error)
    G_GNUC_NON_NULL(1);
