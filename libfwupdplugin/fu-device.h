/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
					GInputStream *stream,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*set_quirk_kv)(FuDevice *self,
				 const gchar *key,
				 const gchar *value,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT;
	gboolean (*setup)(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
	void (*incorporate)(FuDevice *self, FuDevice *donor);
	void (*replace)(FuDevice *self, FuDevice *donor);
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
	void (*invalidate)(FuDevice *self);
	gchar *(*convert_version)(FuDevice *self, guint64 version_raw);
	void (*register_flags)(FuDevice *self);
#endif
};

/**
 * FuDeviceInstanceFlags:
 * @FU_DEVICE_INSTANCE_FLAG_NONE:		No flags set
 * @FU_DEVICE_INSTANCE_FLAG_VISIBLE:		Show to the user
 * @FU_DEVICE_INSTANCE_FLAG_QUIRKS:		Match against quirk files
 * @FU_DEVICE_INSTANCE_FLAG_GENERIC:		Generic GUID added by a baseclass
 *
 * The flags to use when interacting with a device instance
 **/
typedef enum {
	FU_DEVICE_INSTANCE_FLAG_NONE = 0,
	FU_DEVICE_INSTANCE_FLAG_VISIBLE = 1 << 0,
	FU_DEVICE_INSTANCE_FLAG_QUIRKS = 1 << 1,
	FU_DEVICE_INSTANCE_FLAG_GENERIC = 1 << 2,
	/*< private >*/
	FU_DEVICE_INSTANCE_FLAG_UNKNOWN = G_MAXUINT64,
} FuDeviceInstanceFlags;

/**
 * FuDeviceIncorporateFlags:
 *
 * The flags to use when incorporating a device instance.
 **/
typedef enum {
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_BASECLASS:
	 *
	 * Set baseclass properties.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_BASECLASS = 1ull << 0,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_SUPERCLASS:
	 *
	 * Set superclass properties, implemented using `->incorporate()`.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_SUPERCLASS = 1ull << 1,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_VENDOR:
	 *
	 * Set vendor.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_VENDOR = 1ull << 2,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS:
	 *
	 * Set vendor IDs.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS = 1ull << 3,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID:
	 *
	 * Set the physical ID.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID = 1ull << 4,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_LOGICAL_ID:
	 *
	 * Set the logical ID.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_LOGICAL_ID = 1ull << 5,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_BACKEND_ID:
	 *
	 * Set the backend ID.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_BACKEND_ID = 1ull << 6,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_REMOVE_DELAY:
	 *
	 * Set the remove delay.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_REMOVE_DELAY = 1ull << 7,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_ACQUIESCE_DELAY:
	 *
	 * Set the acquiesce delay.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_ACQUIESCE_DELAY = 1ull << 8,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_ICONS:
	 *
	 * Set the icons.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_ICONS = 1ull << 9,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_UPDATE_ERROR:
	 *
	 * Set the update error.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_UPDATE_ERROR = 1ull << 10,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_UPDATE_STATE:
	 *
	 * Set the update state.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_UPDATE_STATE = 1ull << 11,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_VID:
	 *
	 * Set the vendor ID.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_VID = 1ull << 12,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_PID:
	 *
	 * Set the product ID.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_PID = 1ull << 13,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_UPDATE_MESSAGE:
	 *
	 * Set the update message.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_UPDATE_MESSAGE = 1ull << 14,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_UPDATE_IMAGE:
	 *
	 * Set the update image.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_UPDATE_IMAGE = 1ull << 15,
	/**
	 * FU_DEVICE_INCORPORATE_FLAG_EVENTS:
	 *
	 * Add the device events.
	 *
	 * Since: 2.0.0
	 **/
	FU_DEVICE_INCORPORATE_FLAG_EVENTS = 1ull << 16,
	/*< private >*/
	FU_DEVICE_INCORPORATE_FLAG_ALL = G_MAXUINT64,
} FuDeviceIncorporateFlags;

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
#define fu_device_has_flag(d, v)	   fwupd_device_has_flag(FWUPD_DEVICE(d), v)
#define fu_device_has_request_flag(d, v)   fwupd_device_has_request_flag(FWUPD_DEVICE(d), v)
#define fu_device_add_request_flag(d, v)   fwupd_device_add_request_flag(FWUPD_DEVICE(d), v)
#define fu_device_has_instance_id(d, v)	   fwupd_device_has_instance_id(FWUPD_DEVICE(d), v)
#define fu_device_has_vendor_id(d, v)	   fwupd_device_has_vendor_id(FWUPD_DEVICE(d), v)
#define fu_device_has_protocol(d, v)	   fwupd_device_has_protocol(FWUPD_DEVICE(d), v)
#define fu_device_has_checksum(d, v)	   fwupd_device_has_checksum(FWUPD_DEVICE(d), v)
#define fu_device_add_checksum(d, v)	   fwupd_device_add_checksum(FWUPD_DEVICE(d), v)
#define fu_device_add_release(d, v)	   fwupd_device_add_release(FWUPD_DEVICE(d), v)
#define fu_device_add_icon(d, v)	   fwupd_device_add_icon(FWUPD_DEVICE(d), v)
#define fu_device_has_icon(d, v)	   fwupd_device_has_icon(FWUPD_DEVICE(d), v)
#define fu_device_add_issue(d, v)	   fwupd_device_add_issue(FWUPD_DEVICE(d), v)
#define fu_device_set_description(d, v)	   fwupd_device_set_description(FWUPD_DEVICE(d), v)
#define fu_device_set_flags(d, v)	   fwupd_device_set_flags(FWUPD_DEVICE(d), v)
#define fu_device_set_plugin(d, v)	   fwupd_device_set_plugin(FWUPD_DEVICE(d), v)
#define fu_device_set_serial(d, v)	   fwupd_device_set_serial(FWUPD_DEVICE(d), v)
#define fu_device_set_summary(d, v)	   fwupd_device_set_summary(FWUPD_DEVICE(d), v)
#define fu_device_set_branch(d, v)	   fwupd_device_set_branch(FWUPD_DEVICE(d), v)
#define fu_device_set_update_error(d, v)   fwupd_device_set_update_error(FWUPD_DEVICE(d), v)
#define fu_device_add_vendor_id(d, v)	   fwupd_device_add_vendor_id(FWUPD_DEVICE(d), v)
#define fu_device_add_protocol(d, v)	   fwupd_device_add_protocol(FWUPD_DEVICE(d), v)
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
 * FU_DEVICE_PRIVATE_FLAG_NO_AUTO_INSTANCE_IDS:
 *
 * Do not add instance IDs from the device baseclass.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_PRIVATE_FLAG_NO_AUTO_INSTANCE_IDS "no-auto-instance-ids"
/**
 * FU_DEVICE_PRIVATE_FLAG_ENSURE_SEMVER:
 *
 * Ensure the version is a valid semantic version, e.g. numbers separated with dots.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_PRIVATE_FLAG_ENSURE_SEMVER "ensure-semver"
/**
 * FU_DEVICE_PRIVATE_FLAG_ONLY_SUPPORTED:
 *
 * Only devices supported in the metadata will be opened
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_PRIVATE_FLAG_ONLY_SUPPORTED "only-supported"
/**
 * FU_DEVICE_PRIVATE_FLAG_MD_SET_NAME:
 *
 * Set the device name from the metadata `name` if available.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_PRIVATE_FLAG_MD_SET_NAME "md-set-name"
/**
 * FU_DEVICE_PRIVATE_FLAG_MD_SET_NAME_CATEGORY:
 *
 * Set the device name from the metadata `category` if available.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_PRIVATE_FLAG_MD_SET_NAME_CATEGORY "md-set-name-category"
/**
 * FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT:
 *
 * Set the device version format from the metadata or history database if available.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_PRIVATE_FLAG_MD_SET_VERFMT "md-set-verfmt"
/**
 * FU_DEVICE_PRIVATE_FLAG_MD_SET_ICON:
 *
 * Set the device icon from the metadata if available.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_PRIVATE_FLAG_MD_SET_ICON "md-set-icon"
/**
 * FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN:
 *
 * Retry the device open up to 5 times if it fails.
 *
 * Since: 1.5.5
 */
#define FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN "retry-open"
/**
 * FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID:
 *
 * Match GUIDs on device replug where the physical and logical IDs will be different.
 *
 * Since: 1.5.8
 */
#define FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID "replug-match-guid"
/**
 * FU_DEVICE_PRIVATE_FLAG_INHERIT_ACTIVATION:
 *
 * Inherit activation status from the history database on startup.
 *
 * Since: 1.5.9
 */
#define FU_DEVICE_PRIVATE_FLAG_INHERIT_ACTIVATION "inherit-activation"
/**
 * FU_DEVICE_PRIVATE_FLAG_IS_OPEN:
 *
 * The device opened successfully and ready to use.
 *
 * Since: 1.6.1
 */
#define FU_DEVICE_PRIVATE_FLAG_IS_OPEN "is-open"
/**
 * FU_DEVICE_PRIVATE_FLAG_NO_SERIAL_NUMBER:
 *
 * Do not attempt to read the device serial number.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_PRIVATE_FLAG_NO_SERIAL_NUMBER "no-serial-number"
/**
 * FU_DEVICE_PRIVATE_FLAG_AUTO_PARENT_CHILDREN:
 *
 * Automatically assign the parent for children of this device.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_PRIVATE_FLAG_AUTO_PARENT_CHILDREN "auto-parent-children"
/**
 * FU_DEVICE_PRIVATE_FLAG_ATTACH_EXTRA_RESET:
 *
 * Device needs resetting twice for attach after the firmware update.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_PRIVATE_FLAG_ATTACH_EXTRA_RESET "attach-extra-reset"
/**
 * FU_DEVICE_PRIVATE_FLAG_INHIBIT_CHILDREN:
 *
 * Children of the device are inhibited by the parent.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_PRIVATE_FLAG_INHIBIT_CHILDREN "inhibit-children"
/**
 * FU_DEVICE_PRIVATE_FLAG_NO_AUTO_REMOVE_CHILDREN:
 *
 * Do not auto-remove children in the device list.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_PRIVATE_FLAG_NO_AUTO_REMOVE_CHILDREN "no-auto-remove-children"
/**
 * FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN:
 *
 * Use parent to open and close the device.
 *
 * Since: 1.6.2
 */
#define FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN "use-parent-for-open"
/**
 * FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_BATTERY:
 *
 * Use parent for the battery level and threshold.
 *
 * Since: 1.6.3
 */
#define FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_BATTERY "use-parent-for-battery"
/**
 * FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FALLBACK:
 *
 * Use parent for the battery level and threshold.
 *
 * Since: 1.6.4
 */
#define FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FALLBACK "use-proxy-fallback"
/**
 * FU_DEVICE_PRIVATE_FLAG_NO_AUTO_REMOVE:
 *
 * The device is not auto removed.
 *
 * Since 1.7.3
 */
#define FU_DEVICE_PRIVATE_FLAG_NO_AUTO_REMOVE "no-auto-remove"
/**
 * FU_DEVICE_PRIVATE_FLAG_MD_SET_VENDOR:
 *
 * Set the device vendor from the metadata `developer_name` if available.
 *
 * Since: 1.7.4
 */
#define FU_DEVICE_PRIVATE_FLAG_MD_SET_VENDOR "md-set-vendor"
/**
 * FU_DEVICE_PRIVATE_FLAG_NO_LID_CLOSED:
 *
 * Do not allow updating when the laptop lid is closed.
 *
 * Since: 1.7.4
 */
#define FU_DEVICE_PRIVATE_FLAG_NO_LID_CLOSED "no-lid-closed"
/**
 * FU_DEVICE_PRIVATE_FLAG_NO_PROBE:
 *
 * Do not probe this device.
 *
 * Since: 1.7.6
 */
#define FU_DEVICE_PRIVATE_FLAG_NO_PROBE "no-probe"
/**
 * FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED:
 *
 * Set the signed/unsigned payload from the metadata if available.
 *
 * Since: 1.7.6
 */
#define FU_DEVICE_PRIVATE_FLAG_MD_SET_SIGNED "md-set-signed"
/**
 * FU_DEVICE_PRIVATE_FLAG_AUTO_PAUSE_POLLING:
 *
 * Pause polling when reading or writing to the device
 *
 * Since: 1.8.1
 */
#define FU_DEVICE_PRIVATE_FLAG_AUTO_PAUSE_POLLING "auto-pause-polling"
/**
 * FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG:
 *
 * Only use the device removal delay when explicitly waiting for a replug, rather than
 * every time the device is removed.
 *
 * Since: 1.8.1
 */
#define FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG "only-wait-for-replug"
/**
 * FU_DEVICE_PRIVATE_FLAG_IGNORE_SYSTEM_POWER:
 *
 * Allow updating firmware when the system power is otherwise too low.
 * This is only really useful when updating the system battery firmware.
 *
 * Since: 1.8.11
 */
#define FU_DEVICE_PRIVATE_FLAG_IGNORE_SYSTEM_POWER "ignore-system-power"
/**
 * FU_DEVICE_PRIVATE_FLAG_SAVE_INTO_BACKUP_REMOTE:
 *
 * Save the cabinet archive to persistent storage remote before starting the update process.
 *
 * This is useful when the network device is being updated, and different blobs inside the
 * archive could be required in different scenarios. For instance, if the user installs a
 * firmware update for a specific network device and then changes the SIM -- it might be
 * they need the archive again and have no internet access.
 *
 * Since: 1.8.13
 */
#define FU_DEVICE_PRIVATE_FLAG_SAVE_INTO_BACKUP_REMOTE "save-into-backup-remote"
/**
 * FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS:
 *
 * Set the device flags from the metadata if available.
 *
 * NOTE: These flags should only affect device update, and should never be used to affect
 * enumeration.
 *
 * Since: 1.9.1
 */
#define FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS "md-set-flags"
/**
 * FU_DEVICE_PRIVATE_FLAG_MD_SET_VERSION:
 *
 * Set the device version from the metadata if available.
 *
 * Since: 1.9.1
 */
#define FU_DEVICE_PRIVATE_FLAG_MD_SET_VERSION "md-set-version"
/**
 * FU_DEVICE_PRIVATE_FLAG_MD_ONLY_CHECKSUM:
 *
 * Only use the metadata *checksum* to set device attributes.
 *
 * Since: 1.9.1
 */
#define FU_DEVICE_PRIVATE_FLAG_MD_ONLY_CHECKSUM "md-only-checksum"
/**
 * FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV:
 *
 * Add the `_REV` instance ID suffix.
 *
 * Since: 1.9.3
 */
#define FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV "add-instance-id-rev"
/**
 * FU_DEVICE_PRIVATE_FLAG_UNCONNECTED:
 *
 * The device is not connected and is probably awaiting replug.
 *
 * Since: 1.9.4
 */
#define FU_DEVICE_PRIVATE_FLAG_UNCONNECTED "unconnected"
/**
 * FU_DEVICE_PRIVATE_FLAG_DISPLAY_REQUIRED:
 *
 * The device requires a display to be plugged in.
 *
 * Since: 1.9.6
 */
#define FU_DEVICE_PRIVATE_FLAG_DISPLAY_REQUIRED "display-required"
/**
 * FU_DEVICE_PRIVATE_FLAG_UPDATE_PENDING:
 *
 * The device has an update that is waiting to be applied.
 *
 * Since: 1.9.7
 */
#define FU_DEVICE_PRIVATE_FLAG_UPDATE_PENDING "update-pending"
/**
 * FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_GUIDS:
 *
 * Do not add generic GUIDs from outside the plugin.
 *
 * Since: 1.9.8
 */
#define FU_DEVICE_PRIVATE_FLAG_NO_GENERIC_GUIDS "no-generic-guids"
/**
 * FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES:
 *
 * The device uses a generic instance ID and firmware requires a parent, child, sibling or
 * CHID requirement.
 *
 * Since: 1.9.8
 */
#define FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES "enforce-requires"
/**
 * FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE:
 *
 * The device represents the main system host firmware.
 *
 * Since: 1.9.10
 */
#define FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE "host-firmware"
/**
 * FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE_CHILD:
 *
 * The device should be a child of the main system host firmware device.
 *
 * Since: 1.9.10
 */
#define FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE_CHILD "host-firmware-child"
/**
 * FU_DEVICE_PRIVATE_FLAG_HOST_CPU:
 *
 * The device represents the main CPU device.
 *
 * Since: 1.9.10
 */
#define FU_DEVICE_PRIVATE_FLAG_HOST_CPU "host-cpu"
/**
 * FU_DEVICE_PRIVATE_FLAG_HOST_CPU_CHILD:
 *
 * The device should be a child of the main CPU device.
 *
 * Since: 1.9.10
 */
#define FU_DEVICE_PRIVATE_FLAG_HOST_CPU_CHILD "host-cpu-child"
/**
 * FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER:
 *
 * Do not automatically set the device order, e.g. updating the child before the parent.
 *
 * Since: 1.9.13
 */
#define FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER "explicit-order"
/**
 * FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY:
 *
 * Reference-count the proxy -- which is useful when using `ProxyGType`.
 *
 * Since: 1.9.15
 */
#define FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY "refcounted-proxy"
/**
 * FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN:
 *
 * Use proxy to open and close the device.
 *
 * Since: 1.9.16
 */
#define FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN "use-proxy-for-open"

/**
 * FU_DEVICE_PRIVATE_FLAG_INSTALL_PARENT_FIRST:
 *
 * The composite device requires installation of composite firmware on the parent before
 * the child.
 * Normally the child is installed before the parent.
 *
 * Since: 2.0.0
 */
#define FU_DEVICE_PRIVATE_FLAG_INSTALL_PARENT_FIRST "install-parent-first"

/**
 * FU_DEVICE_PRIVATE_FLAG_REGISTERED:
 *
 * The device has been registered with other plugins.
 *
 * Since: 2.0.0
 */
#define FU_DEVICE_PRIVATE_FLAG_REGISTERED "registered"

/**
 * FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS:
 *
 * The device will add counterpart GUIDs from an alternate mode like bootloader.
 * This flag is typically specified in a quirk.
 *
 * Since: 2.0.0
 */
#define FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS "add-counterpart-guids"

/**
 * FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION:
 *
 * The device will always display use the runtime version rather than the bootloader
 * version.
 *
 * Since: 2.0.0
 */
#define FU_DEVICE_PRIVATE_FLAG_USE_RUNTIME_VERSION "use-runtime-version"

/**
 * FU_DEVICE_PRIVATE_FLAG_SKIPS_RESTART:
 *
 * The device relies upon activation or power cycle to load firmware.
 *
 * Since: 2.0.0
 */
#define FU_DEVICE_PRIVATE_FLAG_SKIPS_RESTART "skips-restart"

/**
 * FU_DEVICE_PRIVATE_FLAG_IS_FAKE:
 *
 * The device is fake, and constructed from a fake sysfs test directory.
 *
 * Since: 2.0.0
 */
#define FU_DEVICE_PRIVATE_FLAG_IS_FAKE "is-fake"

/* accessors */
gchar *
fu_device_to_string(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_add_string(FuDevice *self, guint idt, GString *str) G_GNUC_NON_NULL(1, 3);
const gchar *
fu_device_get_equivalent_id(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_equivalent_id(FuDevice *self, const gchar *equivalent_id) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_guid(FuDevice *self, const gchar *guid) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_guid_full(FuDevice *self, const gchar *guid, FuDeviceInstanceFlags flags)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_device_has_guid(FuDevice *self, const gchar *guid) G_GNUC_NON_NULL(1);
void
fu_device_add_instance_id(FuDevice *self, const gchar *instance_id) G_GNUC_NON_NULL(1);
void
fu_device_add_instance_id_full(FuDevice *self,
			       const gchar *instance_id,
			       FuDeviceInstanceFlags flags) G_GNUC_NON_NULL(1, 2);
FuDevice *
fu_device_get_root(FuDevice *self) G_GNUC_NON_NULL(1);
FuDevice *
fu_device_get_parent(FuDevice *self) G_GNUC_NON_NULL(1);
FuDevice *
fu_device_get_backend_parent(FuDevice *self, GError **error) G_GNUC_NON_NULL(1);
FuDevice *
fu_device_get_backend_parent_with_subsystem(FuDevice *self, const gchar *subsystem, GError **error)
    G_GNUC_NON_NULL(1);
GPtrArray *
fu_device_get_children(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_add_child(FuDevice *self, FuDevice *child) G_GNUC_NON_NULL(1, 2);
void
fu_device_remove_child(FuDevice *self, FuDevice *child) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_parent_guid(FuDevice *self, const gchar *guid) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_parent_physical_id(FuDevice *self, const gchar *physical_id) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_parent_backend_id(FuDevice *self, const gchar *backend_id) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_counterpart_guid(FuDevice *self, const gchar *guid) G_GNUC_NON_NULL(1, 2);
FuDevice *
fu_device_get_proxy(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_proxy(FuDevice *self, FuDevice *proxy) G_GNUC_NON_NULL(1);
FuDevice *
fu_device_get_proxy_with_fallback(FuDevice *self) G_GNUC_NON_NULL(1);
const gchar *
fu_device_get_metadata(FuDevice *self, const gchar *key) G_GNUC_NON_NULL(1, 2);
gboolean
fu_device_get_metadata_boolean(FuDevice *self, const gchar *key) G_GNUC_NON_NULL(1, 2);
guint
fu_device_get_metadata_integer(FuDevice *self, const gchar *key) G_GNUC_NON_NULL(1, 2);
void
fu_device_remove_metadata(FuDevice *self, const gchar *key) G_GNUC_NON_NULL(1, 2);
void
fu_device_set_metadata(FuDevice *self, const gchar *key, const gchar *value) G_GNUC_NON_NULL(1, 2);
void
fu_device_set_metadata_boolean(FuDevice *self, const gchar *key, gboolean value)
    G_GNUC_NON_NULL(1, 2);
void
fu_device_set_metadata_integer(FuDevice *self, const gchar *key, guint value) G_GNUC_NON_NULL(1, 2);
void
fu_device_set_id(FuDevice *self, const gchar *id) G_GNUC_NON_NULL(1);
void
fu_device_set_version_format(FuDevice *self, FwupdVersionFormat fmt) G_GNUC_NON_NULL(1);
void
fu_device_set_version(FuDevice *self, const gchar *version) G_GNUC_NON_NULL(1);
void
fu_device_set_version_lowest(FuDevice *self, const gchar *version) G_GNUC_NON_NULL(1);
void
fu_device_set_version_bootloader(FuDevice *self, const gchar *version) G_GNUC_NON_NULL(1);
void
fu_device_set_version_raw(FuDevice *self, guint64 version_raw) G_GNUC_NON_NULL(1);
void
fu_device_inhibit(FuDevice *self, const gchar *inhibit_id, const gchar *reason)
    G_GNUC_NON_NULL(1, 2);
void
fu_device_uninhibit(FuDevice *self, const gchar *inhibit_id) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_problem(FuDevice *self, FwupdDeviceProblem problem) G_GNUC_NON_NULL(1);
void
fu_device_remove_problem(FuDevice *self, FwupdDeviceProblem problem) G_GNUC_NON_NULL(1);
gboolean
fu_device_has_problem(FuDevice *self, FwupdDeviceProblem problem) G_GNUC_NON_NULL(1);
gboolean
fu_device_has_inhibit(FuDevice *self, const gchar *inhibit_id) G_GNUC_NON_NULL(1, 2);
const gchar *
fu_device_get_physical_id(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_physical_id(FuDevice *self, const gchar *physical_id) G_GNUC_NON_NULL(1);
const gchar *
fu_device_get_logical_id(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_logical_id(FuDevice *self, const gchar *logical_id) G_GNUC_NON_NULL(1);
const gchar *
fu_device_get_backend_id(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_backend_id(FuDevice *self, const gchar *backend_id) G_GNUC_NON_NULL(1);
const gchar *
fu_device_get_proxy_guid(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_proxy_guid(FuDevice *self, const gchar *proxy_guid) G_GNUC_NON_NULL(1);
guint
fu_device_get_priority(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_priority(FuDevice *self, guint priority) G_GNUC_NON_NULL(1);
void
fu_device_add_flag(FuDevice *self, FwupdDeviceFlags flag) G_GNUC_NON_NULL(1);
void
fu_device_remove_flag(FuDevice *self, FwupdDeviceFlags flag) G_GNUC_NON_NULL(1);
void
fu_device_set_name(FuDevice *self, const gchar *value) G_GNUC_NON_NULL(1);
void
fu_device_set_vendor(FuDevice *self, const gchar *vendor) G_GNUC_NON_NULL(1);
guint
fu_device_get_remove_delay(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_remove_delay(FuDevice *self, guint remove_delay) G_GNUC_NON_NULL(1);
guint
fu_device_get_acquiesce_delay(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_acquiesce_delay(FuDevice *self, guint acquiesce_delay) G_GNUC_NON_NULL(1);
void
fu_device_set_firmware_size(FuDevice *self, guint64 size) G_GNUC_NON_NULL(1);
void
fu_device_set_firmware_size_min(FuDevice *self, guint64 size_min) G_GNUC_NON_NULL(1);
void
fu_device_set_firmware_size_max(FuDevice *self, guint64 size_max) G_GNUC_NON_NULL(1);
guint64
fu_device_get_firmware_size_min(FuDevice *self) G_GNUC_NON_NULL(1);
guint64
fu_device_get_firmware_size_max(FuDevice *self) G_GNUC_NON_NULL(1);
guint
fu_device_get_battery_level(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_battery_level(FuDevice *self, guint battery_level) G_GNUC_NON_NULL(1);
guint
fu_device_get_battery_threshold(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_battery_threshold(FuDevice *self, guint battery_threshold) G_GNUC_NON_NULL(1);
const gchar *
fu_device_get_update_message(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_update_message(FuDevice *self, const gchar *update_message) G_GNUC_NON_NULL(1);
const gchar *
fu_device_get_update_image(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_update_image(FuDevice *self, const gchar *update_image) G_GNUC_NON_NULL(1);

gint64
fu_device_get_created_usec(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_created_usec(FuDevice *self, gint64 created_usec) G_GNUC_NON_NULL(1);
gint64
fu_device_get_modified_usec(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_modified_usec(FuDevice *self, gint64 modified_usec) G_GNUC_NON_NULL(1);

guint16
fu_device_get_vid(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_vid(FuDevice *self, guint16 vid);
guint16
fu_device_get_pid(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_pid(FuDevice *self, guint16 pid);

void
fu_device_set_update_state(FuDevice *self, FwupdUpdateState update_state) G_GNUC_NON_NULL(1);
void
fu_device_set_context(FuDevice *self, FuContext *ctx) G_GNUC_NON_NULL(1);
FuContext *
fu_device_get_context(FuDevice *self) G_GNUC_NON_NULL(1);
GType
fu_device_get_specialized_gtype(FuDevice *self) G_GNUC_NON_NULL(1);
GType
fu_device_get_proxy_gtype(FuDevice *self) G_GNUC_NON_NULL(1);
GType
fu_device_get_firmware_gtype(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_firmware_gtype(FuDevice *self, GType firmware_gtype) G_GNUC_NON_NULL(1);
gboolean
fu_device_get_results(FuDevice *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_device_write_firmware(FuDevice *self,
			 GInputStream *stream,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
FuFirmware *
fu_device_prepare_firmware(FuDevice *self,
			   GInputStream *stream,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2, 3);
FuFirmware *
fu_device_read_firmware(FuDevice *self,
			FuProgress *progress,
			GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
GBytes *
fu_device_dump_firmware(FuDevice *self,
			FuProgress *progress,
			GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_device_attach(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_device_detach(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_device_attach_full(FuDevice *self,
		      FuProgress *progress,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_device_detach_full(FuDevice *self,
		      FuProgress *progress,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_device_reload(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_device_prepare(FuDevice *self, FuProgress *progress, FwupdInstallFlags flags, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_device_cleanup(FuDevice *self, FuProgress *progress, FwupdInstallFlags flags, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
void
fu_device_incorporate(FuDevice *self, FuDevice *donor, FuDeviceIncorporateFlags flag)
    G_GNUC_NON_NULL(1, 2);
void
fu_device_incorporate_flag(FuDevice *self, FuDevice *donor, FwupdDeviceFlags flag)
    G_GNUC_NON_NULL(1);
gboolean
fu_device_open(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_device_close(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_device_probe(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_device_setup(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_device_rescan(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_device_activate(FuDevice *self, FuProgress *progress, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
void
fu_device_probe_invalidate(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_probe_complete(FuDevice *self) G_GNUC_NON_NULL(1);
gboolean
fu_device_poll(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
void
fu_device_set_poll_interval(FuDevice *self, guint interval) G_GNUC_NON_NULL(1);
void
fu_device_retry_set_delay(FuDevice *self, guint delay) G_GNUC_NON_NULL(1);
void
fu_device_retry_add_recovery(FuDevice *self, GQuark domain, gint code, FuDeviceRetryFunc func)
    G_GNUC_NON_NULL(1);
gboolean
fu_device_retry(FuDevice *self,
		FuDeviceRetryFunc func,
		guint count,
		gpointer user_data,
		GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_device_retry_full(FuDevice *self,
		     FuDeviceRetryFunc func,
		     guint count,
		     guint delay,
		     gpointer user_data,
		     GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
void
fu_device_sleep(FuDevice *self, guint delay_ms) G_GNUC_NON_NULL(1);
void
fu_device_sleep_full(FuDevice *self, guint delay_ms, FuProgress *progress) G_GNUC_NON_NULL(1);
gboolean
fu_device_bind_driver(FuDevice *self, const gchar *subsystem, const gchar *driver, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_device_unbind_driver(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
GHashTable *
fu_device_report_metadata_pre(FuDevice *self) G_GNUC_NON_NULL(1);
GHashTable *
fu_device_report_metadata_post(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_add_security_attrs(FuDevice *self, FuSecurityAttrs *attrs) G_GNUC_NON_NULL(1, 2);
void
fu_device_register_private_flag(FuDevice *self, const gchar *flag) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_private_flag(FuDevice *self, const gchar *flag) G_GNUC_NON_NULL(1, 2);
void
fu_device_remove_private_flag(FuDevice *self, const gchar *flag) G_GNUC_NON_NULL(1, 2);
gboolean
fu_device_has_private_flag(FuDevice *self, const gchar *flag) G_GNUC_NON_NULL(1, 2);
gboolean
fu_device_emit_request(FuDevice *self, FwupdRequest *request, FuProgress *progress, GError **error)
    G_GNUC_NON_NULL(1, 2);
FwupdSecurityAttr *
fu_device_security_attr_new(FuDevice *self, const gchar *appstream_id) G_GNUC_NON_NULL(1, 2);

const gchar *
fu_device_get_instance_str(FuDevice *self, const gchar *key) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_instance_str(FuDevice *self, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
void
fu_device_add_instance_strsafe(FuDevice *self, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
void
fu_device_add_instance_strup(FuDevice *self, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
void
fu_device_add_instance_u4(FuDevice *self, const gchar *key, guint8 value) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_instance_u8(FuDevice *self, const gchar *key, guint8 value) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_instance_u16(FuDevice *self, const gchar *key, guint16 value) G_GNUC_NON_NULL(1, 2);
void
fu_device_add_instance_u32(FuDevice *self, const gchar *key, guint32 value) G_GNUC_NON_NULL(1, 2);
gboolean
fu_device_build_instance_id(FuDevice *self, GError **error, const gchar *subsystem, ...)
    G_GNUC_NULL_TERMINATED G_GNUC_NON_NULL(1, 3);
gboolean
fu_device_build_instance_id_full(FuDevice *self,
				 FuDeviceInstanceFlags flags,
				 GError **error,
				 const gchar *subsystem,
				 ...) G_GNUC_NULL_TERMINATED G_GNUC_NON_NULL(1, 4);
void
fu_device_build_vendor_id(FuDevice *self, const gchar *prefix, const gchar *value);
void
fu_device_build_vendor_id_u16(FuDevice *self, const gchar *prefix, guint16 value);
FuDeviceLocker *
fu_device_poll_locker_new(FuDevice *self, GError **error) G_GNUC_NON_NULL(1);
