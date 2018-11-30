/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_ARCHIVE_H
#define __FU_ARCHIVE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define FU_TYPE_ARCHIVE (fu_archive_get_type ())

G_DECLARE_FINAL_TYPE (FuArchive, fu_archive, FU, ARCHIVE, GObject)

/**
 * FwupdError:
 * @FU_ARCHIVE_FLAG_NONE:		No flags set
 * @FU_ARCHIVE_FLAG_IGNORE_PATH:	Ignore any path component
 *
 * The flags to use when loading the archive.
 **/
typedef enum {
	FU_ARCHIVE_FLAG_NONE		= 0,
	FU_ARCHIVE_FLAG_IGNORE_PATH	= 1 << 0,
	/*< private >*/
	FU_ARCHIVE_FLAG_LAST
} FuArchiveFlags;

FuArchive	*fu_archive_new			(GBytes		*data,
						 FuArchiveFlags	 flags,
						 GError		**error);
GBytes		*fu_archive_lookup_by_fn	(FuArchive	*self,
						 const gchar	*fn,
						 GError		**error);

G_END_DECLS

#endif /* __FU_ARCHIVE_H */
