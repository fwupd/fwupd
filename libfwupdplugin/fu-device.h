/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupd.h>

#include "fu-context.h"
#include "fu-device-locker.h"
#include "fu-firmware.h"
#include "fu-progress.h"
#include "fu-security-attrs.h"
#include "fu-version-common.h"

#define FU_TYPE_DEVICE (fu_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuDevice, fu_device, FU, DEVICE, FwupdDevice)

struct _FuDeviceClass {
	FwupdDeviceClass parent_class;
#ifndef __GI_SCANNER__
	void (*to_string)(FuDevice *self, guint indent, GString *str);
	gboolean (*write_firmware)(FuDevice *self,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error) G_GNUC_WARN_UNUSED_RESULT;
	FuFirmware *(*read_firmware)(FuDevice *self,
				     FuProgress *progress,
				     GError **error)G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*detach)(FuDevice *self,
			   FuProgress *progress,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*attach)(FuDevice *self,
			   FuProgress *progress,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*open)(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*close)(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*probe)(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*rescan)(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	FuFirmware *(*prepare_firmware)(FuDevice *self,
					GBytes *fw,
					FwupdInstallFlags flags,
					GError **error)G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*set_quirk_kv)(FuDevice *self,
				 const gchar *key,
				 const gchar *value,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*setup)(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	void (*incorporate)(FuDevice *self, FuDevice *donor);
	void (*probe_complete)(FuDevice *self);
	gboolean (*poll)(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*activate)(FuDevice *self,
			     FuProgress *progress,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*reload)(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*prepare)(FuDevice *self,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*cleanup)(FuDevice *self,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT;
	void (*report_metadata_pre)(FuDevice *self, GHashTable *metadata);
	void (*report_metadata_post)(FuDevice *self, GHashTable *metadata);
	gboolean (*bind_driver)(FuDevice *self,
				const gchar *subsystem,
				const gchar *driver,
				GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*unbind_driver)(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	GBytes *(*dump_firmware)(FuDevice *self,
				 FuProgress *progress,
				 GError **error)G_GNUC_WARN_UNUSED_RESULT;
	void (*add_security_attrs)(FuDevice *self, FuSecurityAttrs *attrs);
	gboolean (*ready)(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	void (*child_added)(FuDevice *self, /* signal */
			    FuDevice *child);
	void (*child_removed)(FuDevice *self, /* signal */
			      FuDevice *child);
	void (*request)(FuDevice *self, /* signal */
			FwupdRequest *request);
	gboolean (*get_results)(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	void (*set_progress)(FuDevice *self, FuProgress *progress);
#endif
};

/**
 * FuDeviceInstanceFlags:
 * @FU_DEVICE_INSTANCE_FLAG_NONE:		No flags set
 * @FU_DEVICE_INSTANCE_FLAG_VISIBLE:		Show to the user
 * @FU_DEVICE_INSTANCE_FLAG_QUIRKS:		Match against quirk files
 *
 * The flags to use when interacting with a device instance
 **/
typedef enum {
	FU_DEVICE_INSTANCE_FLAG_NONE = 0,
	FU_DEVICE_INSTANCE_FLAG_VISIBLE = 1 << 0,
	FU_DEVICE_INSTANCE_FLAG_QUIRKS = 1 << 1,
	/*< private >*/
	FU_DEVICE_INSTANCE_FLAG_LAST
} FuDeviceInstanceFlags;

/**
 * FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE:
 *
 * The default removal delay for device re-enumeration taking into account a
 * chain of slow USB hubs. This should be used when the device is able to
 * reset itself between bootloader->runtime->bootloader.
 */
#define FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE 10000 /* ms */

/**
 * FU_DEVICE_REMOVE_DELAY_USER_REPLUG:
 *
 * The default removal delay for device re-plug taking into account humans
 * being slow and clumsy. This should be used when the user has to do something,
 * e.g. unplug, press a magic button and then replug.
 */
#define FU_DEVICE_REMOVE_DELAY_USER_REPLUG 40000 /* ms */

/**
 * FuDeviceRetryFunc:
 * @self: a #FuDevice
 * @user_data: (closure): user data
 * @error: (nullable): optional return location for an error
 *
 * The device retry iteration callback.
 *
 * Returns: %TRUE on success
 */
typedef gboolean (*FuDeviceRetryFunc)(FuDevice *self,
				      gpointer user_data,
				      GError **error) G_GNUC_WARN_UNUSED_RESULT;

FuDevice *
fu_device_new(FuContext *ctx);

/* helpful casting macros */
#define fu_device_has_flag(d, v)	   fwupd_device_has_flag(FWUPD_DEVICE(d), v)
#define fu_device_has_instance_id(d, v)	   fwupd_device_has_instance_id(FWUPD_DEVICE(d), v)
#define fu_device_has_vendor_id(d, v)	   fwupd_device_has_vendor_id(FWUPD_DEVICE(d), v)
#define fu_device_has_protocol(d, v)	   fwupd_device_has_protocol(FWUPD_DEVICE(d), v)
#define fu_device_has_checksum(d, v)	   fwupd_device_has_checksum(FWUPD_DEVICE(d), v)
#define fu_device_add_checksum(d, v)	   fwupd_device_add_checksum(FWUPD_DEVICE(d), v)
#define fu_device_add_release(d, v)	   fwupd_device_add_release(FWUPD_DEVICE(d), v)
#define fu_device_add_icon(d, v)	   fwupd_device_add_icon(FWUPD_DEVICE(d), v)
#define fu_device_has_icon(d, v)	   fwupd_device_has_icon(FWUPD_DEVICE(d), v)
#define fu_device_add_issue(d, v)	   fwupd_device_add_issue(FWUPD_DEVICE(d), v)
#define fu_device_set_created(d, v)	   fwupd_device_set_created(FWUPD_DEVICE(d), v)
#define fu_device_set_description(d, v)	   fwupd_device_set_description(FWUPD_DEVICE(d), v)
#define fu_device_set_flags(d, v)	   fwupd_device_set_flags(FWUPD_DEVICE(d), v)
#define fu_device_set_modified(d, v)	   fwupd_device_set_modified(FWUPD_DEVICE(d), v)
#define fu_device_set_plugin(d, v)	   fwupd_device_set_plugin(FWUPD_DEVICE(d), v)
#define fu_device_set_serial(d, v)	   fwupd_device_set_serial(FWUPD_DEVICE(d), v)
#define fu_device_set_summary(d, v)	   fwupd_device_set_summary(FWUPD_DEVICE(d), v)
#define fu_device_set_branch(d, v)	   fwupd_device_set_branch(FWUPD_DEVICE(d), v)
#define fu_device_set_update_message(d, v) fwupd_device_set_update_message(FWUPD_DEVICE(d), v)
#define fu_device_set_update_image(d, v)   fwupd_device_set_update_image(FWUPD_DEVICE(d), v)
#define fu_device_set_update_error(d, v)   fwupd_device_set_update_error(FWUPD_DEVICE(d), v)
#define fu_device_add_vendor_id(d, v)	   fwupd_device_add_vendor_id(FWUPD_DEVICE(d), v)
#define fu_device_add_protocol(d, v)	   fwupd_device_add_protocol(FWUPD_DEVICE(d), v)
#define fu_device_set_version_raw(d, v)	   fwupd_device_set_version_raw(FWUPD_DEVICE(d), v)
#define fu_device_set_version_lowest_raw(d, v)                                                     \
	fwupd_device_set_version_lowest_raw(FWUPD_DEVICE(d), v)
#define fu_device_set_version_bootloader_raw(d, v)                                                 \
	fwupd_device_set_version_bootloader_raw(FWUPD_DEVICE(d), v)
#define fu_device_set_version_build_date(d, v)                                                     \
	fwupd_device_set_version_build_date(FWUPD_DEVICE(d), v)
#define fu_device_set_flashes_left(d, v)     fwupd_device_set_flashes_left(FWUPD_DEVICE(d), v)
#define fu_device_set_install_duration(d, v) fwupd_device_set_install_duration(FWUPD_DEVICE(d), v)
#define fu_device_get_checksums(d)	     fwupd_device_get_checksums(FWUPD_DEVICE(d))
#define fu_device_get_flags(d)		     fwupd_device_get_flags(FWUPD_DEVICE(d))
#define fu_device_get_created(d)	     fwupd_device_get_created(FWUPD_DEVICE(d))
#define fu_device_get_modified(d)	     fwupd_device_get_modified(FWUPD_DEVICE(d))
#define fu_device_get_guids(d)		     fwupd_device_get_guids(FWUPD_DEVICE(d))
#define fu_device_get_guid_default(d)	     fwupd_device_get_guid_default(FWUPD_DEVICE(d))
#define fu_device_get_instance_ids(d)	     fwupd_device_get_instance_ids(FWUPD_DEVICE(d))
#define fu_device_get_icons(d)		     fwupd_device_get_icons(FWUPD_DEVICE(d))
#define fu_device_get_issues(d)		     fwupd_device_get_issues(FWUPD_DEVICE(d))
#define fu_device_get_name(d)		     fwupd_device_get_name(FWUPD_DEVICE(d))
#define fu_device_get_serial(d)		     fwupd_device_get_serial(FWUPD_DEVICE(d))
#define fu_device_get_summary(d)	     fwupd_device_get_summary(FWUPD_DEVICE(d))
#define fu_device_get_branch(d)		     fwupd_device_get_branch(FWUPD_DEVICE(d))
#define fu_device_get_id(d)		     fwupd_device_get_id(FWUPD_DEVICE(d))
#define fu_device_get_composite_id(d)	     fwupd_device_get_composite_id(FWUPD_DEVICE(d))
#define fu_device_get_plugin(d)		     fwupd_device_get_plugin(FWUPD_DEVICE(d))
#define fu_device_get_update_error(d)	     fwupd_device_get_update_error(FWUPD_DEVICE(d))
#define fu_device_get_update_state(d)	     fwupd_device_get_update_state(FWUPD_DEVICE(d))
#define fu_device_get_update_message(d)	     fwupd_device_get_update_message(FWUPD_DEVICE(d))
#define fu_device_get_update_image(d)	     fwupd_device_get_update_image(FWUPD_DEVICE(d))
#define fu_device_get_vendor(d)		     fwupd_device_get_vendor(FWUPD_DEVICE(d))
#define fu_device_get_version(d)	     fwupd_device_get_version(FWUPD_DEVICE(d))
#define fu_device_get_version_lowest(d)	     fwupd_device_get_version_lowest(FWUPD_DEVICE(d))
#define fu_device_get_version_bootloader(d)  fwupd_device_get_version_bootloader(FWUPD_DEVICE(d))
#define fu_device_get_version_format(d)	     fwupd_device_get_version_format(FWUPD_DEVICE(d))
#define fu_device_get_version_raw(d)	     fwupd_device_get_version_raw(FWUPD_DEVICE(d))
#define fu_device_get_version_lowest_raw(d)  fwupd_device_get_version_lowest_raw(FWUPD_DEVICE(d))
#define fu_device_get_version_bootloader_raw(d)                                                    \
	fwupd_device_get_version_bootloader_raw(FWUPD_DEVICE(d))
#define fu_device_get_version_build_date(d) fwupd_device_get_version_build_date(FWUPD_DEVICE(d))
#define fu_device_get_vendor_ids(d)	    fwupd_device_get_vendor_ids(FWUPD_DEVICE(d))
#define fu_device_get_protocols(d)	    fwupd_device_get_protocols(FWUPD_DEVICE(d))
#define fu_device_get_flashes_left(d)	    fwupd_device_get_flashes_left(FWUPD_DEVICE(d))
#define fu_device_get_install_duration(d)   fwupd_device_get_install_duration(FWUPD_DEVICE(d))
#define fu_device_get_release_default(d)    fwupd_device_get_release_default(FWUPD_DEVICE(d))
#define fu_device_get_status(d)		    fwupd_device_get_status(FWUPD_DEVICE(d))
#define fu_device_set_status(d, v)	    fwupd_device_set_status(FWUPD_DEVICE(d), v)
#define fu_device_get_percentage(d)	    fwupd_device_get_percentage(FWUPD_DEVICE(d))
#define fu_device_set_percentage(d, v)	    fwupd_device_set_percentage(FWUPD_DEVICE(d), v)

/**
 * FuDeviceInternalFlags:
 *
 * The device internal flags.
 **/
typedef guint64 FuDeviceInternalFlags;

/**
 * FU_DEVICE_INTERNAL_FLAG_NONE:
 *
 * No flags set.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_INTERNAL_FLAG_NONE (0)

/**
 * FU_DEVICE_INTERNAL_FLAG_UNKNOWN:
 *
 * Unknown flag value.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_INTERNAL_FLAG_UNKNOWN G_MAXUINT64

/**
 * FU_DEVICE_INTERNAL_FLAG_NO_AUTO_INSTANCE_IDS:
 *
 * Do not add instance IDs from the device baseclass.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_INTERNAL_FLAG_NO_AUTO_INSTANCE_IDS (1ull << 0)

/**
 * FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER:
 *
 * Ensure the version is a valid semantic version, e.g. numbers separated with dots.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER (1ull << 1)

/**
 * FU_DEVICE_INTERNAL_FLAG_ONLY_SUPPORTED:
 *
 * Only devices supported in the metadata will be opened
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_INTERNAL_FLAG_ONLY_SUPPORTED (1ull << 2)

/**
 * FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME:
 *
 * Set the device name from the metadata `name` if available.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME (1ull << 3)

/**
 * FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME_CATEGORY:
 *
 * Set the device name from the metadata `category` if available.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME_CATEGORY (1ull << 4)

/**
 * FU_DEVICE_INTERNAL_FLAG_MD_SET_VERFMT:
 *
 * Set the device version format from the metadata if available.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_INTERNAL_FLAG_MD_SET_VERFMT (1ull << 5)

/**
 * FU_DEVICE_INTERNAL_FLAG_MD_SET_ICON:
 *
 * Set the device icon from the metadata if available.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_INTERNAL_FLAG_MD_SET_ICON (1ull << 6)

/**
 * FU_DEVICE_INTERNAL_FLAG_RETRY_OPEN:
 *
 * Retry the device open up to 5 times if it fails.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_INTERNAL_FLAG_RETRY_OPEN (1ull << 7)

/**
 * FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID:
 *
 * Match GUIDs on device replug where the physical and logical IDs will be different.
 *
 * Since: 1.5.8
 */
#define FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID (1ull << 8)

/**
 * FU_DEVICE_INTERNAL_FLAG_INHERIT_ACTIVATION:
 *
 * Inherit activation status from the history database on startup.
 *
 * Since: 1.5.9
 */
#define FU_DEVICE_INTERNAL_FLAG_INHERIT_ACTIVATION (1ull << 9)

/**
 * FU_DEVICE_INTERNAL_FLAG_IS_OPEN:
 *
 * The device opened successfully and ready to use.
 *
 * Since: 1.6.1
 */
#define FU_DEVICE_INTERNAL_FLAG_IS_OPEN (1ull << 10)

/**
 * FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER:
 *
 * Do not attempt to read the device serial number.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_INTERNAL_FLAG_NO_SERIAL_NUMBER (1ull << 11)

/**
 * FU_DEVICE_INTERNAL_FLAG_AUTO_PARENT_CHILDREN:
 *
 * Automatically assign the parent for children of this device.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_INTERNAL_FLAG_AUTO_PARENT_CHILDREN (1ull << 12)

/**
 * FU_DEVICE_INTERNAL_FLAG_ATTACH_EXTRA_RESET:
 *
 * Device needs resetting twice for attach after the firmware update.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_INTERNAL_FLAG_ATTACH_EXTRA_RESET (1ull << 13)

/**
 * FU_DEVICE_INTERNAL_FLAG_INHIBIT_CHILDREN:
 *
 * Children of the device are inhibited by the parent.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_INTERNAL_FLAG_INHIBIT_CHILDREN (1ull << 14)

/**
 * FU_DEVICE_INTERNAL_FLAG_NO_AUTO_REMOVE_CHILDREN:
 *
 * Do not auto-remove children in the device list.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_INTERNAL_FLAG_NO_AUTO_REMOVE_CHILDREN (1ull << 15)

/**
 * FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN:
 *
 * Use parent to open and close the device.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN (1ull << 16)

/**
 * FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_BATTERY:
 *
 * Use parent for the battery level and threshold.
 *
 * Since: 1.6.3
 */
#define FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_BATTERY (1ull << 17)

/**
 * FU_DEVICE_INTERNAL_FLAG_USE_PROXY_FALLBACK:
 *
 * Use parent for the battery level and threshold.
 *
 * Since: 1.6.4
 */
#define FU_DEVICE_INTERNAL_FLAG_USE_PROXY_FALLBACK (1ull << 18)

/**
 * FU_DEVICE_INTERNAL_FLAG_NO_AUTO_REMOVE:
 *
 * The device is not auto removed.
 *
 * Since 1.7.3
 */
#define FU_DEVICE_INTERNAL_FLAG_NO_AUTO_REMOVE (1llu << 19)

/**
 * FU_DEVICE_INTERNAL_FLAG_MD_SET_VENDOR:
 *
 * Set the device vendor from the metadata `developer_name` if available.
 *
 * Since: 1.7.4
 */
#define FU_DEVICE_INTERNAL_FLAG_MD_SET_VENDOR (1ull << 20)

/**
 * FU_DEVICE_INTERNAL_FLAG_NO_LID_CLOSED:
 *
 * Do not allow updating when the laptop lid is closed.
 *
 * Since: 1.7.4
 */
#define FU_DEVICE_INTERNAL_FLAG_NO_LID_CLOSED (1ull << 21)

/**
 * FU_DEVICE_INTERNAL_FLAG_NO_PROBE:
 *
 * Do not probe this device.
 *
 * Since: 1.7.6
 */
#define FU_DEVICE_INTERNAL_FLAG_NO_PROBE (1ull << 22)

/**
 * FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED:
 *
 * Set the signed/unsigned payload from the metadata if available.
 *
 * Since: 1.7.6
 */
#define FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED (1ull << 23)

/**
 * FU_DEVICE_INTERNAL_AUTO_PAUSE_POLLING:
 *
 * Pause polling when reading or writing to the device
 *
 * Since: 1.8.1
 */
#define FU_DEVICE_INTERNAL_AUTO_PAUSE_POLLING (1ull << 24)

/**
 * FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG:
 *
 * Only use the device removal delay when explicitly waiting for a replug, rather than every time
 * the device is removed.
 *
 * Since: 1.8.1
 */
#define FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG (1ull << 25)

/**
 * FU_DEVICE_INTERNAL_FLAG_IGNORE_SYSTEM_POWER:
 *
 * Allow updating firmware when the system power is otherwise too low.
 * This is only really useful when updating the system battery firmware.
 *
 * Since: 1.8.11
 */
#define FU_DEVICE_INTERNAL_FLAG_IGNORE_SYSTEM_POWER (1ull << 26)

/**
 * FU_DEVICE_INTERNAL_FLAG_NO_PROBE_COMPLETE:
 *
 * Do not deallocate resources typically only required during `->probe`.
 *
 * Note: the daemon will not actually free or unref device resources, but the plugin should
 * still use this flag. After a a few releases and a lot of testing we'll actually flip the switch.
 *
 * Since: 1.8.12
 */
#define FU_DEVICE_INTERNAL_FLAG_NO_PROBE_COMPLETE (1ull << 27)

/* accessors */
gchar *
fu_device_to_string(FuDevice *self);
void
fu_device_add_string(FuDevice *self, guint idt, GString *str);
const gchar *
fu_device_get_alternate_id(FuDevice *self);
void
fu_device_set_alternate_id(FuDevice *self, const gchar *alternate_id);
const gchar *
fu_device_get_equivalent_id(FuDevice *self);
void
fu_device_set_equivalent_id(FuDevice *self, const gchar *equivalent_id);
void
fu_device_add_guid(FuDevice *self, const gchar *guid);
void
fu_device_add_guid_full(FuDevice *self, const gchar *guid, FuDeviceInstanceFlags flags);
gboolean
fu_device_has_guid(FuDevice *self, const gchar *guid);
void
fu_device_add_instance_id(FuDevice *self, const gchar *instance_id);
void
fu_device_add_instance_id_full(FuDevice *self,
			       const gchar *instance_id,
			       FuDeviceInstanceFlags flags);
FuDevice *
fu_device_get_alternate(FuDevice *self);
FuDevice *
fu_device_get_root(FuDevice *self);
FuDevice *
fu_device_get_parent(FuDevice *self);
GPtrArray *
fu_device_get_children(FuDevice *self);
void
fu_device_add_child(FuDevice *self, FuDevice *child);
void
fu_device_remove_child(FuDevice *self, FuDevice *child);
void
fu_device_add_parent_guid(FuDevice *self, const gchar *guid);
void
fu_device_add_parent_physical_id(FuDevice *self, const gchar *physical_id);
void
fu_device_add_counterpart_guid(FuDevice *self, const gchar *guid);
FuDevice *
fu_device_get_proxy(FuDevice *self);
void
fu_device_set_proxy(FuDevice *self, FuDevice *proxy);
FuDevice *
fu_device_get_proxy_with_fallback(FuDevice *self);
const gchar *
fu_device_get_metadata(FuDevice *self, const gchar *key);
gboolean
fu_device_get_metadata_boolean(FuDevice *self, const gchar *key);
guint
fu_device_get_metadata_integer(FuDevice *self, const gchar *key);
void
fu_device_remove_metadata(FuDevice *self, const gchar *key);
void
fu_device_set_metadata(FuDevice *self, const gchar *key, const gchar *value);
void
fu_device_set_metadata_boolean(FuDevice *self, const gchar *key, gboolean value);
void
fu_device_set_metadata_integer(FuDevice *self, const gchar *key, guint value);
void
fu_device_set_id(FuDevice *self, const gchar *id);
void
fu_device_set_version_format(FuDevice *self, FwupdVersionFormat fmt);
void
fu_device_set_version(FuDevice *self, const gchar *version);
void
fu_device_set_version_lowest(FuDevice *self, const gchar *version);
void
fu_device_set_version_bootloader(FuDevice *self, const gchar *version);
void
fu_device_set_version_from_uint16(FuDevice *self, guint16 version_raw);
void
fu_device_set_version_from_uint32(FuDevice *self, guint32 version_raw);
void
fu_device_set_version_from_uint64(FuDevice *self, guint64 version_raw);
void
fu_device_inhibit(FuDevice *self, const gchar *inhibit_id, const gchar *reason);
void
fu_device_uninhibit(FuDevice *self, const gchar *inhibit_id);
void
fu_device_add_problem(FuDevice *self, FwupdDeviceProblem problem);
void
fu_device_remove_problem(FuDevice *self, FwupdDeviceProblem problem);
gboolean
fu_device_has_problem(FuDevice *self, FwupdDeviceProblem problem);
gboolean
fu_device_has_inhibit(FuDevice *self, const gchar *inhibit_id);
const gchar *
fu_device_get_physical_id(FuDevice *self);
void
fu_device_set_physical_id(FuDevice *self, const gchar *physical_id);
const gchar *
fu_device_get_logical_id(FuDevice *self);
void
fu_device_set_logical_id(FuDevice *self, const gchar *logical_id);
const gchar *
fu_device_get_backend_id(FuDevice *self);
void
fu_device_set_backend_id(FuDevice *self, const gchar *backend_id);
const gchar *
fu_device_get_proxy_guid(FuDevice *self);
void
fu_device_set_proxy_guid(FuDevice *self, const gchar *proxy_guid);
guint
fu_device_get_priority(FuDevice *self);
void
fu_device_set_priority(FuDevice *self, guint priority);
void
fu_device_add_flag(FuDevice *self, FwupdDeviceFlags flag);
void
fu_device_remove_flag(FuDevice *self, FwupdDeviceFlags flag);
const gchar *
fu_device_get_custom_flags(FuDevice *self);
void
fu_device_set_custom_flags(FuDevice *self, const gchar *custom_flags);
void
fu_device_set_name(FuDevice *self, const gchar *value);
void
fu_device_set_vendor(FuDevice *self, const gchar *vendor);
guint
fu_device_get_remove_delay(FuDevice *self);
void
fu_device_set_remove_delay(FuDevice *self, guint remove_delay);
guint
fu_device_get_acquiesce_delay(FuDevice *self);
void
fu_device_set_acquiesce_delay(FuDevice *self, guint acquiesce_delay);
void
fu_device_set_firmware_size(FuDevice *self, guint64 size);
void
fu_device_set_firmware_size_min(FuDevice *self, guint64 size_min);
void
fu_device_set_firmware_size_max(FuDevice *self, guint64 size_max);
guint64
fu_device_get_firmware_size_min(FuDevice *self);
guint64
fu_device_get_firmware_size_max(FuDevice *self);
guint
fu_device_get_battery_level(FuDevice *self);
void
fu_device_set_battery_level(FuDevice *self, guint battery_level);
guint
fu_device_get_battery_threshold(FuDevice *self);
void
fu_device_set_battery_threshold(FuDevice *self, guint battery_threshold);
void
fu_device_set_update_state(FuDevice *self, FwupdUpdateState update_state);
void
fu_device_set_context(FuDevice *self, FuContext *ctx);
FuContext *
fu_device_get_context(FuDevice *self);
GType
fu_device_get_specialized_gtype(FuDevice *self);
GType
fu_device_get_firmware_gtype(FuDevice *self);
void
fu_device_set_firmware_gtype(FuDevice *self, GType firmware_gtype);
void
fu_device_add_internal_flag(FuDevice *self, FuDeviceInternalFlags flag);
void
fu_device_remove_internal_flag(FuDevice *self, FuDeviceInternalFlags flag);
gboolean
fu_device_has_internal_flag(FuDevice *self, FuDeviceInternalFlags flag);
gboolean
fu_device_get_results(FuDevice *self, GError **error);
gboolean
fu_device_write_firmware(FuDevice *self,
			 GBytes *fw,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT;
FuFirmware *
fu_device_prepare_firmware(FuDevice *self, GBytes *fw, FwupdInstallFlags flags, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
FuFirmware *
fu_device_read_firmware(FuDevice *self,
			FuProgress *progress,
			GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_device_dump_firmware(FuDevice *self,
			FuProgress *progress,
			GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_attach(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_detach(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_attach_full(FuDevice *self,
		      FuProgress *progress,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_detach_full(FuDevice *self,
		      FuProgress *progress,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_reload(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_prepare(FuDevice *self, FuProgress *progress, FwupdInstallFlags flags, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_cleanup(FuDevice *self, FuProgress *progress, FwupdInstallFlags flags, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
void
fu_device_incorporate(FuDevice *self, FuDevice *donor);
void
fu_device_incorporate_flag(FuDevice *self, FuDevice *donor, FwupdDeviceFlags flag);
gboolean
fu_device_open(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_close(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_probe(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_setup(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_rescan(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_activate(FuDevice *self, FuProgress *progress, GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_device_probe_invalidate(FuDevice *self);
void
fu_device_probe_complete(FuDevice *self);
gboolean
fu_device_poll(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_device_set_poll_interval(FuDevice *self, guint interval);
void
fu_device_retry_set_delay(FuDevice *self, guint delay);
void
fu_device_retry_add_recovery(FuDevice *self, GQuark domain, gint code, FuDeviceRetryFunc func);
gboolean
fu_device_retry(FuDevice *self,
		FuDeviceRetryFunc func,
		guint count,
		gpointer user_data,
		GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_retry_full(FuDevice *self,
		     FuDeviceRetryFunc func,
		     guint count,
		     guint delay,
		     gpointer user_data,
		     GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_device_sleep(FuDevice *self, guint delay_ms);
void
fu_device_sleep_full(FuDevice *self, guint delay_ms, FuProgress *progress);
gboolean
fu_device_bind_driver(FuDevice *self, const gchar *subsystem, const gchar *driver, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_device_unbind_driver(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
GHashTable *
fu_device_report_metadata_pre(FuDevice *self);
GHashTable *
fu_device_report_metadata_post(FuDevice *self);
void
fu_device_add_security_attrs(FuDevice *self, FuSecurityAttrs *attrs);
void
fu_device_register_private_flag(FuDevice *self, guint64 value, const gchar *value_str);
void
fu_device_add_private_flag(FuDevice *self, guint64 flag);
void
fu_device_remove_private_flag(FuDevice *self, guint64 flag);
gboolean
fu_device_has_private_flag(FuDevice *self, guint64 flag);
void
fu_device_emit_request(FuDevice *self, FwupdRequest *request);
FwupdSecurityAttr *
fu_device_security_attr_new(FuDevice *self, const gchar *appstream_id);

void
fu_device_add_instance_str(FuDevice *self, const gchar *key, const gchar *value);
void
fu_device_add_instance_strsafe(FuDevice *self, const gchar *key, const gchar *value);
void
fu_device_add_instance_strup(FuDevice *self, const gchar *key, const gchar *value);
void
fu_device_add_instance_u4(FuDevice *self, const gchar *key, guint8 value);
void
fu_device_add_instance_u8(FuDevice *self, const gchar *key, guint8 value);
void
fu_device_add_instance_u16(FuDevice *self, const gchar *key, guint16 value);
void
fu_device_add_instance_u32(FuDevice *self, const gchar *key, guint32 value);
gboolean
fu_device_build_instance_id(FuDevice *self, GError **error, const gchar *subsystem, ...)
    G_GNUC_NULL_TERMINATED;
gboolean
fu_device_build_instance_id_quirk(FuDevice *self, GError **error, const gchar *subsystem, ...)
    G_GNUC_NULL_TERMINATED;
FuDeviceLocker *
fu_device_poll_locker_new(FuDevice *self, GError **error);
