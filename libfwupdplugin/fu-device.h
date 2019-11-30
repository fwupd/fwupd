/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <fwupd.h>

#include "fu-firmware.h"
#include "fu-quirks.h"
#include "fu-common-version.h"

#define FU_TYPE_DEVICE (fu_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuDevice, fu_device, FU, DEVICE, FwupdDevice)

struct _FuDeviceClass
{
	FwupdDeviceClass	 parent_class;
	void			 (*to_string)		(FuDevice	*self,
							 guint		 indent,
							 GString	*str);
	gboolean		 (*write_firmware)	(FuDevice	*self,
							 FuFirmware	*firmware,
							 FwupdInstallFlags flags,
							 GError		**error);
	FuFirmware		*(*read_firmware)	(FuDevice	*self,
							 GError		**error);
	gboolean		 (*detach)		(FuDevice	*self,
							 GError		**error);
	gboolean		 (*attach)		(FuDevice	*self,
							 GError		**error);
	gboolean		 (*open)		(FuDevice	*self,
							 GError		**error);
	gboolean		 (*close)		(FuDevice	*self,
							 GError		**error);
	gboolean		 (*probe)		(FuDevice	*self,
							 GError		**error);
	gboolean		 (*rescan)		(FuDevice	*self,
							 GError		**error);
	FuFirmware		*(*prepare_firmware)	(FuDevice	*self,
							 GBytes		*fw,
							 FwupdInstallFlags flags,
							 GError		**error);
	gboolean		 (*set_quirk_kv)	(FuDevice	*self,
							 const gchar	*key,
							 const gchar	*value,
							 GError		**error);
	gboolean		 (*setup)		(FuDevice	*self,
							 GError		**error);
	void			 (*incorporate)		(FuDevice	*self,
							 FuDevice	*donor);
	gboolean		 (*poll)		(FuDevice	*self,
							 GError		**error);
	gboolean		 (*activate)		(FuDevice	*self,
							 GError		**error);
	gboolean		 (*reload)		(FuDevice	*self,
							 GError		**error);
	gboolean		 (*prepare)		(FuDevice	*self,
							 FwupdInstallFlags flags,
							 GError		**error);
	gboolean		 (*cleanup)		(FuDevice	*self,
							 FwupdInstallFlags flags,
							 GError		**error);
	/*< private >*/
	gpointer	padding[16];
};

/**
 * FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE:
 *
 * The default removal delay for device re-enumeration taking into account a
 * chain of slow USB hubs. This should be used when the device is able to
 * reset itself between bootloader->runtime->bootloader.
 */
#define FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE		10000

/**
 * FU_DEVICE_REMOVE_DELAY_USER_REPLUG:
 *
 * The default removal delay for device re-plug taking into account humans
 * being slow and clumsy. This should be used when the user has to do something,
 * e.g. unplug, press a magic button and then replug.
 */
#define FU_DEVICE_REMOVE_DELAY_USER_REPLUG		40000

FuDevice	*fu_device_new				(void);

/* helpful casting macros */
#define fu_device_remove_flag(d,v)		fwupd_device_remove_flag(FWUPD_DEVICE(d),v)
#define fu_device_has_flag(d,v)			fwupd_device_has_flag(FWUPD_DEVICE(d),v)
#define fu_device_has_instance_id(d,v)		fwupd_device_has_instance_id(FWUPD_DEVICE(d),v)
#define fu_device_add_checksum(d,v)		fwupd_device_add_checksum(FWUPD_DEVICE(d),v)
#define fu_device_add_release(d,v)		fwupd_device_add_release(FWUPD_DEVICE(d),v)
#define fu_device_add_icon(d,v)			fwupd_device_add_icon(FWUPD_DEVICE(d),v)
#define fu_device_set_created(d,v)		fwupd_device_set_created(FWUPD_DEVICE(d),v)
#define fu_device_set_description(d,v)		fwupd_device_set_description(FWUPD_DEVICE(d),v)
#define fu_device_set_flags(d,v)		fwupd_device_set_flags(FWUPD_DEVICE(d),v)
#define fu_device_set_modified(d,v)		fwupd_device_set_modified(FWUPD_DEVICE(d),v)
#define fu_device_set_plugin(d,v)		fwupd_device_set_plugin(FWUPD_DEVICE(d),v)
#define fu_device_set_serial(d,v)		fwupd_device_set_serial(FWUPD_DEVICE(d),v)
#define fu_device_set_summary(d,v)		fwupd_device_set_summary(FWUPD_DEVICE(d),v)
#define fu_device_set_update_error(d,v)		fwupd_device_set_update_error(FWUPD_DEVICE(d),v)
#define fu_device_set_update_state(d,v)		fwupd_device_set_update_state(FWUPD_DEVICE(d),v)
#define fu_device_set_vendor(d,v)		fwupd_device_set_vendor(FWUPD_DEVICE(d),v)
#define fu_device_set_vendor_id(d,v)		fwupd_device_set_vendor_id(FWUPD_DEVICE(d),v)
#define fu_device_set_version_lowest(d,v)	fwupd_device_set_version_lowest(FWUPD_DEVICE(d),v)
#define fu_device_set_version_bootloader(d,v)	fwupd_device_set_version_bootloader(FWUPD_DEVICE(d),v)
#define fu_device_set_version_format(d,v)	fwupd_device_set_version_format(FWUPD_DEVICE(d),v)
#define fu_device_set_version_raw(d,v)		fwupd_device_set_version_raw(FWUPD_DEVICE(d),v)
#define fu_device_set_flashes_left(d,v)		fwupd_device_set_flashes_left(FWUPD_DEVICE(d),v)
#define fu_device_set_install_duration(d,v)	fwupd_device_set_install_duration(FWUPD_DEVICE(d),v)
#define fu_device_get_checksums(d)		fwupd_device_get_checksums(FWUPD_DEVICE(d))
#define fu_device_get_flags(d)			fwupd_device_get_flags(FWUPD_DEVICE(d))
#define fu_device_get_created(d)		fwupd_device_get_created(FWUPD_DEVICE(d))
#define fu_device_get_modified(d)		fwupd_device_get_modified(FWUPD_DEVICE(d))
#define fu_device_get_guids(d)			fwupd_device_get_guids(FWUPD_DEVICE(d))
#define fu_device_get_guid_default(d)		fwupd_device_get_guid_default(FWUPD_DEVICE(d))
#define fu_device_get_instance_ids(d)		fwupd_device_get_instance_ids(FWUPD_DEVICE(d))
#define fu_device_get_icons(d)			fwupd_device_get_icons(FWUPD_DEVICE(d))
#define fu_device_get_name(d)			fwupd_device_get_name(FWUPD_DEVICE(d))
#define fu_device_get_serial(d)			fwupd_device_get_serial(FWUPD_DEVICE(d))
#define fu_device_get_summary(d)		fwupd_device_get_summary(FWUPD_DEVICE(d))
#define fu_device_get_id(d)			fwupd_device_get_id(FWUPD_DEVICE(d))
#define fu_device_get_plugin(d)			fwupd_device_get_plugin(FWUPD_DEVICE(d))
#define fu_device_get_update_error(d)		fwupd_device_get_update_error(FWUPD_DEVICE(d))
#define fu_device_get_update_state(d)		fwupd_device_get_update_state(FWUPD_DEVICE(d))
#define fu_device_get_vendor(d)			fwupd_device_get_vendor(FWUPD_DEVICE(d))
#define fu_device_get_version(d)		fwupd_device_get_version(FWUPD_DEVICE(d))
#define fu_device_get_version_lowest(d)		fwupd_device_get_version_lowest(FWUPD_DEVICE(d))
#define fu_device_get_version_bootloader(d)	fwupd_device_get_version_bootloader(FWUPD_DEVICE(d))
#define fu_device_get_version_format(d)		fwupd_device_get_version_format(FWUPD_DEVICE(d))
#define fu_device_get_version_raw(d)		fwupd_device_get_version_raw(FWUPD_DEVICE(d))
#define fu_device_get_vendor_id(d)		fwupd_device_get_vendor_id(FWUPD_DEVICE(d))
#define fu_device_get_flashes_left(d)		fwupd_device_get_flashes_left(FWUPD_DEVICE(d))
#define fu_device_get_install_duration(d)	fwupd_device_get_install_duration(FWUPD_DEVICE(d))

/* accessors */
gchar		*fu_device_to_string			(FuDevice	*self);
const gchar	*fu_device_get_alternate_id		(FuDevice	*self);
void		 fu_device_set_alternate_id		(FuDevice	*self,
							 const gchar	*alternate_id);
const gchar	*fu_device_get_equivalent_id		(FuDevice	*self);
void		 fu_device_set_equivalent_id		(FuDevice	*self,
							 const gchar	*equivalent_id);
void		 fu_device_add_guid			(FuDevice	*self,
							 const gchar	*guid);
gboolean	 fu_device_has_guid			(FuDevice	*self,
							 const gchar	*guid);
void		 fu_device_add_instance_id		(FuDevice	*self,
							 const gchar	*instance_id);
FuDevice	*fu_device_get_alternate		(FuDevice	*self);
FuDevice	*fu_device_get_parent			(FuDevice	*self);
GPtrArray	*fu_device_get_children			(FuDevice	*self);
void		 fu_device_add_child			(FuDevice	*self,
							 FuDevice	*child);
void		 fu_device_add_parent_guid		(FuDevice	*self,
							 const gchar	*guid);
void		 fu_device_add_counterpart_guid		(FuDevice	*self,
							 const gchar	*guid);
const gchar	*fu_device_get_metadata			(FuDevice	*self,
							 const gchar	*key);
gboolean	 fu_device_get_metadata_boolean		(FuDevice	*self,
							 const gchar	*key);
guint		 fu_device_get_metadata_integer		(FuDevice	*self,
							 const gchar	*key);
void		 fu_device_remove_metadata		(FuDevice	*self,
							 const gchar	*key);
void		 fu_device_set_metadata			(FuDevice	*self,
							 const gchar	*key,
							 const gchar	*value);
void		 fu_device_set_metadata_boolean		(FuDevice	*self,
							 const gchar	*key,
							 gboolean	 value);
void		 fu_device_set_metadata_integer		(FuDevice	*self,
							 const gchar	*key,
							 guint		 value);
void		 fu_device_set_id			(FuDevice	*self,
							 const gchar	*id);
void		 fu_device_set_version			(FuDevice	*self,
							 const gchar	*version,
							 FwupdVersionFormat fmt);
const gchar	*fu_device_get_physical_id		(FuDevice	*self);
void		 fu_device_set_physical_id		(FuDevice	*self,
							 const gchar	*physical_id);
const gchar	*fu_device_get_logical_id		(FuDevice	*self);
void		 fu_device_set_logical_id		(FuDevice	*self,
							 const gchar	*logical_id);
const gchar	*fu_device_get_protocol			(FuDevice	*self);
void		 fu_device_set_protocol			(FuDevice	*self,
							 const gchar	*protocol);
void		 fu_device_add_flag			(FuDevice	*self,
							 FwupdDeviceFlags flag);
const gchar	*fu_device_get_custom_flags		(FuDevice	*self);
gboolean	 fu_device_has_custom_flag		(FuDevice	*self,
							 const gchar	*hint);
void		 fu_device_set_custom_flags		(FuDevice	*self,
							 const gchar	*custom_flags);
void		 fu_device_set_name			(FuDevice	*self,
							 const gchar	*value);
guint		 fu_device_get_remove_delay		(FuDevice	*self);
void		 fu_device_set_remove_delay		(FuDevice	*self,
							 guint		 remove_delay);
FwupdStatus	 fu_device_get_status			(FuDevice	*self);
void		 fu_device_set_status			(FuDevice	*self,
							 FwupdStatus	 status);
void		 fu_device_set_firmware_size		(FuDevice	*self,
							 guint64	 size);
void		 fu_device_set_firmware_size_min	(FuDevice	*self,
							 guint64	 size_min);
void		 fu_device_set_firmware_size_max	(FuDevice	*self,
							 guint64	 size_max);
guint64		 fu_device_get_firmware_size_min	(FuDevice	*self);
guint64		 fu_device_get_firmware_size_max	(FuDevice	*self);
guint		 fu_device_get_progress			(FuDevice	*self);
void		 fu_device_set_progress			(FuDevice	*self,
							 guint		 progress);
void		 fu_device_set_progress_full		(FuDevice	*self,
							 gsize		 progress_done,
							 gsize		 progress_total);
void		 fu_device_set_quirks			(FuDevice	*self,
							 FuQuirks	*quirks);
FuQuirks	*fu_device_get_quirks			(FuDevice	*self);
FwupdRelease	*fu_device_get_release_default		(FuDevice	*self);
gboolean	 fu_device_write_firmware		(FuDevice	*self,
							 GBytes		*fw,
							 FwupdInstallFlags flags,
							 GError		**error);
FuFirmware	*fu_device_prepare_firmware		(FuDevice	*self,
							 GBytes		*fw,
							 FwupdInstallFlags flags,
							 GError		**error);
FuFirmware	*fu_device_read_firmware		(FuDevice	*self,
							 GError		**error);
gboolean	 fu_device_attach			(FuDevice	*self,
							 GError		**error);
gboolean	 fu_device_detach			(FuDevice	*self,
							 GError		**error);
gboolean	 fu_device_reload			(FuDevice	*self,
							 GError		**error);
gboolean	 fu_device_prepare			(FuDevice	*self,
							 FwupdInstallFlags flags,
							 GError		**error);
gboolean	 fu_device_cleanup			(FuDevice	*self,
							 FwupdInstallFlags flags,
							 GError		**error);
void		 fu_device_incorporate			(FuDevice	*self,
							 FuDevice	*donor);
void		 fu_device_incorporate_flag		(FuDevice	*self,
							 FuDevice	*donor,
							 FwupdDeviceFlags flag);
gboolean	 fu_device_open				(FuDevice	*self,
							 GError		**error);
gboolean	 fu_device_close			(FuDevice	*self,
							 GError		**error);
gboolean	 fu_device_probe			(FuDevice	*self,
							 GError		**error);
gboolean	 fu_device_setup			(FuDevice	*self,
							 GError		**error);
gboolean	 fu_device_rescan			(FuDevice	*self,
							 GError		**error);
gboolean	 fu_device_activate			(FuDevice	*self,
							 GError		**error);
void		 fu_device_probe_invalidate		(FuDevice	*self);
gboolean	 fu_device_poll				(FuDevice	*self,
							 GError		**error);
void		 fu_device_set_poll_interval		(FuDevice	*self,
							 guint		 interval);
