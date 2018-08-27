/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_DEVICE_H
#define __FU_DEVICE_H

#include <glib-object.h>
#include <fwupd.h>

#include "fu-quirks.h"

G_BEGIN_DECLS

#define FU_TYPE_DEVICE (fu_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuDevice, fu_device, FU, DEVICE, FwupdDevice)

struct _FuDeviceClass
{
	FwupdDeviceClass	 parent_class;
	void			 (*to_string)		(FuDevice	*device,
							 GString	*str);
	gboolean		 (*write_firmware)	(FuDevice	*device,
							 GBytes		*fw,
							 GError		**error);
	GBytes			*(*read_firmware)	(FuDevice	*device,
							 GError		**error);
	gboolean		 (*detach)		(FuDevice	*device,
							 GError		**error);
	gboolean		 (*attach)		(FuDevice	*device,
							 GError		**error);
	gboolean		 (*open)		(FuDevice	*device,
							 GError		**error);
	gboolean		 (*close)		(FuDevice	*device,
							 GError		**error);
	gboolean		 (*probe)		(FuDevice	*device,
							 GError		**error);
	GBytes			*(*prepare_firmware)	(FuDevice	*device,
							 GBytes		*fw,
							 GError		**error);
	/*< private >*/
	gpointer	padding[24];
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
#define FU_DEVICE_REMOVE_DELAY_USER_REPLUG		20000

FuDevice	*fu_device_new				(void);

/* helpful casting macros */
#define fu_device_add_flag(d,v)			fwupd_device_add_flag(FWUPD_DEVICE(d),v)
#define fu_device_remove_flag(d,v)		fwupd_device_remove_flag(FWUPD_DEVICE(d),v)
#define fu_device_has_flag(d,v)			fwupd_device_has_flag(FWUPD_DEVICE(d),v)
#define fu_device_add_checksum(d,v)		fwupd_device_add_checksum(FWUPD_DEVICE(d),v)
#define fu_device_add_release(d,v)		fwupd_device_add_release(FWUPD_DEVICE(d),v)
#define fu_device_add_icon(d,v)			fwupd_device_add_icon(FWUPD_DEVICE(d),v)
#define fu_device_set_created(d,v)		fwupd_device_set_created(FWUPD_DEVICE(d),v)
#define fu_device_set_description(d,v)		fwupd_device_set_description(FWUPD_DEVICE(d),v)
#define fu_device_set_flags(d,v)		fwupd_device_set_flags(FWUPD_DEVICE(d),v)
#define fu_device_has_guid(d,v)			fwupd_device_has_guid(FWUPD_DEVICE(d),v)
#define fu_device_set_modified(d,v)		fwupd_device_set_modified(FWUPD_DEVICE(d),v)
#define fu_device_set_plugin(d,v)		fwupd_device_set_plugin(FWUPD_DEVICE(d),v)
#define fu_device_set_summary(d,v)		fwupd_device_set_summary(FWUPD_DEVICE(d),v)
#define fu_device_set_update_error(d,v)		fwupd_device_set_update_error(FWUPD_DEVICE(d),v)
#define fu_device_set_update_state(d,v)		fwupd_device_set_update_state(FWUPD_DEVICE(d),v)
#define fu_device_set_vendor(d,v)		fwupd_device_set_vendor(FWUPD_DEVICE(d),v)
#define fu_device_set_vendor_id(d,v)		fwupd_device_set_vendor_id(FWUPD_DEVICE(d),v)
#define fu_device_set_version(d,v)		fwupd_device_set_version(FWUPD_DEVICE(d),v)
#define fu_device_set_version_lowest(d,v)	fwupd_device_set_version_lowest(FWUPD_DEVICE(d),v)
#define fu_device_set_version_bootloader(d,v)	fwupd_device_set_version_bootloader(FWUPD_DEVICE(d),v)
#define fu_device_set_flashes_left(d,v)		fwupd_device_set_flashes_left(FWUPD_DEVICE(d),v)
#define fu_device_get_checksums(d)		fwupd_device_get_checksums(FWUPD_DEVICE(d))
#define fu_device_get_flags(d)			fwupd_device_get_flags(FWUPD_DEVICE(d))
#define fu_device_get_created(d)		fwupd_device_get_created(FWUPD_DEVICE(d))
#define fu_device_get_modified(d)		fwupd_device_get_modified(FWUPD_DEVICE(d))
#define fu_device_get_guids(d)			fwupd_device_get_guids(FWUPD_DEVICE(d))
#define fu_device_get_guid_default(d)		fwupd_device_get_guid_default(FWUPD_DEVICE(d))
#define fu_device_get_icons(d)			fwupd_device_get_icons(FWUPD_DEVICE(d))
#define fu_device_get_name(d)			fwupd_device_get_name(FWUPD_DEVICE(d))
#define fu_device_get_summary(d)		fwupd_device_get_summary(FWUPD_DEVICE(d))
#define fu_device_get_id(d)			fwupd_device_get_id(FWUPD_DEVICE(d))
#define fu_device_get_plugin(d)			fwupd_device_get_plugin(FWUPD_DEVICE(d))
#define fu_device_get_update_error(d)		fwupd_device_get_update_error(FWUPD_DEVICE(d))
#define fu_device_get_update_state(d)		fwupd_device_get_update_state(FWUPD_DEVICE(d))
#define fu_device_get_vendor(d)			fwupd_device_get_vendor(FWUPD_DEVICE(d))
#define fu_device_get_version(d)		fwupd_device_get_version(FWUPD_DEVICE(d))
#define fu_device_get_version_lowest(d)		fwupd_device_get_version_lowest(FWUPD_DEVICE(d))
#define fu_device_get_version_bootloader(d)	fwupd_device_get_version_bootloader(FWUPD_DEVICE(d))
#define fu_device_get_vendor_id(d)		fwupd_device_get_vendor_id(FWUPD_DEVICE(d))
#define fu_device_get_flashes_left(d)		fwupd_device_get_flashes_left(FWUPD_DEVICE(d))

/* accessors */
gchar		*fu_device_to_string			(FuDevice	*device);
const gchar	*fu_device_get_alternate_id		(FuDevice	*device);
void		 fu_device_set_alternate_id		(FuDevice	*device,
							 const gchar	*alternate_id);
const gchar	*fu_device_get_equivalent_id		(FuDevice	*device);
void		 fu_device_set_equivalent_id		(FuDevice	*device,
							 const gchar	*equivalent_id);
void		 fu_device_add_guid			(FuDevice	*device,
							 const gchar	*guid);
gchar		*fu_device_get_guids_as_str		(FuDevice	*device);
FuDevice	*fu_device_get_alternate		(FuDevice	*device);
FuDevice	*fu_device_get_parent			(FuDevice	*device);
GPtrArray	*fu_device_get_children			(FuDevice	*device);
void		 fu_device_add_child			(FuDevice	*device,
							 FuDevice	*child);
void		 fu_device_add_parent_guid		(FuDevice	*device,
							 const gchar	*guid);
const gchar	*fu_device_get_metadata			(FuDevice	*device,
							 const gchar	*key);
gboolean	 fu_device_get_metadata_boolean		(FuDevice	*device,
							 const gchar	*key);
guint		 fu_device_get_metadata_integer		(FuDevice	*device,
							 const gchar	*key);
void		 fu_device_set_metadata			(FuDevice	*device,
							 const gchar	*key,
							 const gchar	*value);
void		 fu_device_set_metadata_boolean		(FuDevice	*device,
							 const gchar	*key,
							 gboolean	 value);
void		 fu_device_set_metadata_integer		(FuDevice	*device,
							 const gchar	*key,
							 guint		 value);
void		 fu_device_set_id			(FuDevice	*device,
							 const gchar	*id);
const gchar	*fu_device_get_platform_id		(FuDevice	*device);
void		 fu_device_set_platform_id		(FuDevice	*device,
							 const gchar	*platform_id);
const gchar	*fu_device_get_serial			(FuDevice	*device);
void		 fu_device_set_serial			(FuDevice	*device,
							 const gchar	*serial);
const gchar	*fu_device_get_custom_flags		(FuDevice	*device);
gboolean	 fu_device_has_custom_flag		(FuDevice	*device,
							 const gchar	*hint);
void		 fu_device_set_custom_flags		(FuDevice	*device,
							 const gchar	*custom_flags);
void		 fu_device_set_name			(FuDevice	*device,
							 const gchar	*value);
guint		 fu_device_get_remove_delay		(FuDevice	*device);
void		 fu_device_set_remove_delay		(FuDevice	*device,
							 guint		 remove_delay);
FwupdStatus	 fu_device_get_status			(FuDevice	*device);
void		 fu_device_set_status			(FuDevice	*device,
							 FwupdStatus	 status);
void		 fu_device_set_firmware_size_min	(FuDevice	*device,
							 guint64	 size_min);
void		 fu_device_set_firmware_size_max	(FuDevice	*device,
							 guint64	 size_max);
guint		 fu_device_get_progress			(FuDevice	*device);
void		 fu_device_set_progress			(FuDevice	*device,
							 guint		 progress);
void		 fu_device_set_progress_full		(FuDevice	*device,
							 gsize		 progress_done,
							 gsize		 progress_total);
void		 fu_device_set_quirks			(FuDevice	*device,
							 FuQuirks	*quirks);
FuQuirks	*fu_device_get_quirks			(FuDevice	*device);
FwupdRelease	*fu_device_get_release_default		(FuDevice	*device);
gboolean	 fu_device_write_firmware		(FuDevice	*device,
							 GBytes		*fw,
							 GError		**error);
GBytes		*fu_device_prepare_firmware		(FuDevice	*device,
							 GBytes		*fw,
							 GError		**error);
GBytes		*fu_device_read_firmware		(FuDevice	*device,
							 GError		**error);
gboolean	 fu_device_attach			(FuDevice	*device,
							 GError		**error);
gboolean	 fu_device_detach			(FuDevice	*device,
							 GError		**error);
void		 fu_device_incorporate			(FuDevice	*self,
							 FuDevice	*donor);
gboolean	 fu_device_open				(FuDevice	*device,
							 GError		**error);
gboolean	 fu_device_close			(FuDevice	*device,
							 GError		**error);
gboolean	 fu_device_probe			(FuDevice	*device,
							 GError		**error);
void		 fu_device_probe_invalidate		(FuDevice	*device);

G_END_DECLS

#endif /* __FU_DEVICE_H */

