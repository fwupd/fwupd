/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fwupd-enums.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_RELEASE (fwupd_release_get_type ())
G_DECLARE_DERIVABLE_TYPE (FwupdRelease, fwupd_release, FWUPD, RELEASE, GObject)

struct _FwupdReleaseClass
{
	GObjectClass			 parent_class;
	/*< private >*/
	void (*_fwupd_reserved1)	(void);
	void (*_fwupd_reserved2)	(void);
	void (*_fwupd_reserved3)	(void);
	void (*_fwupd_reserved4)	(void);
	void (*_fwupd_reserved5)	(void);
	void (*_fwupd_reserved6)	(void);
	void (*_fwupd_reserved7)	(void);
};

FwupdRelease	*fwupd_release_new			(void);
gchar		*fwupd_release_to_string		(FwupdRelease	*self);

const gchar	*fwupd_release_get_version		(FwupdRelease	*self);
void		 fwupd_release_set_version		(FwupdRelease	*self,
							 const gchar	*version);
G_DEPRECATED_FOR(fwupd_release_get_locations)
const gchar	*fwupd_release_get_uri			(FwupdRelease	*self);
G_DEPRECATED_FOR(fwupd_release_add_location)
void		 fwupd_release_set_uri			(FwupdRelease	*self,
							 const gchar	*uri);
GPtrArray	*fwupd_release_get_locations		(FwupdRelease	*self);
void		 fwupd_release_add_location		(FwupdRelease	*self,
							 const gchar	*location);
GPtrArray	*fwupd_release_get_issues		(FwupdRelease	*self);
void		 fwupd_release_add_issue		(FwupdRelease	*self,
							 const gchar	*issue);
GPtrArray	*fwupd_release_get_categories		(FwupdRelease	*self);
void		 fwupd_release_add_category		(FwupdRelease	*self,
							 const gchar	*category);
gboolean	 fwupd_release_has_category		(FwupdRelease	*self,
							 const gchar	*category);
GPtrArray	*fwupd_release_get_checksums		(FwupdRelease	*self);
void		 fwupd_release_add_checksum		(FwupdRelease	*self,
							 const gchar	*checksum);
gboolean	 fwupd_release_has_checksum		(FwupdRelease	*self,
							 const gchar	*checksum);

GHashTable	*fwupd_release_get_metadata		(FwupdRelease	*self);
void		 fwupd_release_add_metadata		(FwupdRelease	*self,
							 GHashTable	*hash);
void		 fwupd_release_add_metadata_item	(FwupdRelease	*self,
							 const gchar	*key,
							 const gchar	*value);
const gchar	*fwupd_release_get_metadata_item	(FwupdRelease	*self,
							 const gchar	*key);

const gchar	*fwupd_release_get_filename		(FwupdRelease	*self);
void		 fwupd_release_set_filename		(FwupdRelease	*self,
							 const gchar	*filename);
const gchar	*fwupd_release_get_protocol		(FwupdRelease	*self);
void		 fwupd_release_set_protocol		(FwupdRelease	*self,
							 const gchar	*protocol);
const gchar	*fwupd_release_get_appstream_id		(FwupdRelease	*self);
void		 fwupd_release_set_appstream_id		(FwupdRelease	*self,
							 const gchar	*appstream_id);
const gchar	*fwupd_release_get_detach_caption	(FwupdRelease	*self);
void		 fwupd_release_set_detach_caption	(FwupdRelease	*self,
							 const gchar	*detach_caption);
const gchar	*fwupd_release_get_detach_image		(FwupdRelease	*self);
void		 fwupd_release_set_detach_image		(FwupdRelease	*self,
							 const gchar	*detach_image);
const gchar	*fwupd_release_get_remote_id		(FwupdRelease	*self);
void		 fwupd_release_set_remote_id		(FwupdRelease	*self,
							 const gchar	*remote_id);
const gchar	*fwupd_release_get_vendor		(FwupdRelease	*self);
void		 fwupd_release_set_vendor		(FwupdRelease	*self,
							 const gchar	*vendor);
const gchar	*fwupd_release_get_name			(FwupdRelease	*self);
void		 fwupd_release_set_name			(FwupdRelease	*self,
							 const gchar	*name);
const gchar	*fwupd_release_get_name_variant_suffix	(FwupdRelease	*self);
void		 fwupd_release_set_name_variant_suffix	(FwupdRelease	*self,
							 const gchar	*name_variant_suffix);
const gchar	*fwupd_release_get_summary		(FwupdRelease	*self);
void		 fwupd_release_set_summary		(FwupdRelease	*self,
							 const gchar	*summary);
const gchar	*fwupd_release_get_branch		(FwupdRelease	*self);
void		 fwupd_release_set_branch		(FwupdRelease	*self,
							 const gchar	*branch);
const gchar	*fwupd_release_get_description		(FwupdRelease	*self);
void		 fwupd_release_set_description		(FwupdRelease	*self,
							 const gchar	*description);
const gchar	*fwupd_release_get_homepage		(FwupdRelease	*self);
void		 fwupd_release_set_homepage		(FwupdRelease	*self,
							 const gchar	*homepage);
const gchar	*fwupd_release_get_details_url		(FwupdRelease	*self);
void		 fwupd_release_set_details_url		(FwupdRelease	*self,
							 const gchar	*details_url);
const gchar	*fwupd_release_get_source_url		(FwupdRelease	*self);
void		 fwupd_release_set_source_url		(FwupdRelease	*self,
							 const gchar	*source_url);
guint64		 fwupd_release_get_size			(FwupdRelease	*self);
void		 fwupd_release_set_size			(FwupdRelease	*self,
							 guint64	 size);
guint64		 fwupd_release_get_created		(FwupdRelease	*self);
void		 fwupd_release_set_created		(FwupdRelease	*self,
							 guint64	 created);
const gchar	*fwupd_release_get_license		(FwupdRelease	*self);
void		 fwupd_release_set_license		(FwupdRelease	*self,
							 const gchar	*license);
FwupdTrustFlags	 fwupd_release_get_trust_flags		(FwupdRelease	*self)
G_DEPRECATED_FOR(fwupd_release_get_flags);
void		 fwupd_release_set_trust_flags		(FwupdRelease	*self,
							 FwupdTrustFlags trust_flags)
G_DEPRECATED_FOR(fwupd_release_set_flags);
FwupdReleaseFlags fwupd_release_get_flags		(FwupdRelease	*self);
void		 fwupd_release_set_flags		(FwupdRelease	*self,
							 FwupdReleaseFlags flags);
void		 fwupd_release_add_flag			(FwupdRelease	*self,
							 FwupdReleaseFlags flag);
void		 fwupd_release_remove_flag		(FwupdRelease	*self,
							 FwupdReleaseFlags flag);
gboolean	 fwupd_release_has_flag			(FwupdRelease	*self,
							 FwupdReleaseFlags flag);
FwupdReleaseUrgency fwupd_release_get_urgency		(FwupdRelease	*self);
void		 fwupd_release_set_urgency		(FwupdRelease	*self,
							 FwupdReleaseUrgency urgency);
guint32		 fwupd_release_get_install_duration	(FwupdRelease	*self);
void		 fwupd_release_set_install_duration	(FwupdRelease	*self,
							 guint32	 duration);
const gchar	*fwupd_release_get_update_message	(FwupdRelease	*self);
void		 fwupd_release_set_update_message	(FwupdRelease	*self,
							 const gchar	*update_message);
const gchar	*fwupd_release_get_update_image		(FwupdRelease	*self);
void		 fwupd_release_set_update_image		(FwupdRelease	*self,
							 const gchar	*update_image);

FwupdRelease	*fwupd_release_from_variant		(GVariant	*value);
GPtrArray	*fwupd_release_array_from_variant	(GVariant	*value);

G_END_DECLS
