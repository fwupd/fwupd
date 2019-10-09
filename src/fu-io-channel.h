/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_TYPE_IO_CHANNEL (fu_io_channel_get_type ())

G_DECLARE_FINAL_TYPE (FuIOChannel, fu_io_channel, FU, IO_CHANNEL, GObject)

/**
 * FuIOChannelFlags:
 * @FU_IO_CHANNEL_FLAG_NONE:			No flags are set
 * @FU_IO_CHANNEL_FLAG_SINGLE_SHOT:		Only one read or write is expected
 * @FU_IO_CHANNEL_FLAG_FLUSH_INPUT:		Flush pending input before writing
 * @FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO:		Block waiting for the TTY
 *
 * The flags used when reading data from the TTY.
 **/
typedef enum {
	FU_IO_CHANNEL_FLAG_NONE			= 0,		/* Since: 1.2.2 */
	FU_IO_CHANNEL_FLAG_SINGLE_SHOT		= 1 << 0,	/* Since: 1.2.2 */
	FU_IO_CHANNEL_FLAG_FLUSH_INPUT		= 1 << 1,	/* Since: 1.2.2 */
	FU_IO_CHANNEL_FLAG_USE_BLOCKING_IO	= 1 << 2,	/* Since: 1.2.2 */
	/*< private >*/
	FU_IO_CHANNEL_FLAG_LAST
} FuIOChannelFlags;

FuIOChannel	*fu_io_channel_unix_new		(gint		 fd);
FuIOChannel	*fu_io_channel_new_file		(const gchar	*filename,
						 GError		**error);

gint		 fu_io_channel_unix_get_fd	(FuIOChannel	*self);
gboolean	 fu_io_channel_shutdown		(FuIOChannel	*self,
						 GError		**error);
gboolean	 fu_io_channel_write_raw	(FuIOChannel	*self,
						 const guint8	*data,
						 gsize		 datasz,
						 guint		 timeout_ms,
						 FuIOChannelFlags flags,
						 GError		**error);
gboolean	 fu_io_channel_read_raw		(FuIOChannel	*self,
						 guint8		*buf,
						 gsize		 bufsz,
						 gsize		*bytes_read,
						 guint		 timeout_ms,
						 FuIOChannelFlags flags,
						 GError		**error);
gboolean	 fu_io_channel_write_bytes	(FuIOChannel	*self,
						 GBytes		*bytes,
						 guint		 timeout_ms,
						 FuIOChannelFlags flags,
						 GError		**error);
gboolean	 fu_io_channel_write_byte_array	(FuIOChannel	*self,
						 GByteArray	*buf,
						 guint		 timeout_ms,
						 FuIOChannelFlags flags,
						 GError		**error);
GBytes		*fu_io_channel_read_bytes	(FuIOChannel	*self,
						 gssize		 max_size,
						 guint		 timeout_ms,
						 FuIOChannelFlags flags,
						 GError		**error);
GByteArray	*fu_io_channel_read_byte_array	(FuIOChannel	*self,
						 gssize		 max_size,
						 guint		 timeout_ms,
						 FuIOChannelFlags flags,
						 GError		**error);
