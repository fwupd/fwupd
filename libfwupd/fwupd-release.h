/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __FWUPD_RELEASE_H
#define __FWUPD_RELEASE_H

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
gchar		*fwupd_release_to_string		(FwupdRelease	*release);

const gchar	*fwupd_release_get_version		(FwupdRelease	*release);
void		 fwupd_release_set_version		(FwupdRelease	*release,
							 const gchar	*version);
const gchar	*fwupd_release_get_uri			(FwupdRelease	*release);
void		 fwupd_release_set_uri			(FwupdRelease	*release,
							 const gchar	*uri);
GPtrArray	*fwupd_release_get_checksums		(FwupdRelease	*release);
void		 fwupd_release_add_checksum		(FwupdRelease	*release,
							 const gchar	*checksum);

const gchar	*fwupd_release_get_filename		(FwupdRelease	*release);
void		 fwupd_release_set_filename		(FwupdRelease	*release,
							 const gchar	*filename);
const gchar	*fwupd_release_get_appstream_id		(FwupdRelease	*release);
void		 fwupd_release_set_appstream_id		(FwupdRelease	*release,
							 const gchar	*appstream_id);
const gchar	*fwupd_release_get_remote_id		(FwupdRelease	*release);
void		 fwupd_release_set_remote_id		(FwupdRelease	*release,
							 const gchar	*remote_id);
const gchar	*fwupd_release_get_vendor		(FwupdRelease	*release);
void		 fwupd_release_set_vendor		(FwupdRelease	*release,
							 const gchar	*vendor);
const gchar	*fwupd_release_get_name			(FwupdRelease	*release);
void		 fwupd_release_set_name			(FwupdRelease	*release,
							 const gchar	*name);
const gchar	*fwupd_release_get_summary		(FwupdRelease	*release);
void		 fwupd_release_set_summary		(FwupdRelease	*release,
							 const gchar	*summary);
const gchar	*fwupd_release_get_description		(FwupdRelease	*release);
void		 fwupd_release_set_description		(FwupdRelease	*release,
							 const gchar	*description);
const gchar	*fwupd_release_get_homepage		(FwupdRelease	*release);
void		 fwupd_release_set_homepage		(FwupdRelease	*release,
							 const gchar	*homepage);
guint64		 fwupd_release_get_size			(FwupdRelease	*release);
void		 fwupd_release_set_size			(FwupdRelease	*release,
							 guint64	 size);
const gchar	*fwupd_release_get_license		(FwupdRelease	*release);
void		 fwupd_release_set_license		(FwupdRelease	*release,
							 const gchar	*license);
FwupdTrustFlags	 fwupd_release_get_trust_flags		(FwupdRelease	*release);
void		 fwupd_release_set_trust_flags		(FwupdRelease	*release,
							 FwupdTrustFlags trust_flags);

G_END_DECLS

#endif /* __FWUPD_RELEASE_H */

