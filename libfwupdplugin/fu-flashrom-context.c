/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFlashromContext"

#include "config.h"
#include "fu-common.h"
#include "fu-flashrom-context.h"

#ifdef HAVE_LIBFLASHROM
#include <libflashrom.h>
#endif
#include <libfwupd/fwupd.h>

struct romentry {
	uint32_t start;
	uint32_t end;
	bool included;
	char *name;
};

/* This struct is opaque in libflashrom.h, but it is impossible to clear the
 * included flag on an entry via the public APIs- not even releasing a layout
 * and creating a new one will do it, because it (as of v1.2) will return a
 * pointer to a global layout every time, unless reading layout from IFD.
 * Even loading a new layout into an old one appends regions rather than
 * replacing, so layout must be cleared by zeroing num_entries ourselves. */
struct flashrom_layout {
	struct romentry *entries;
	size_t num_entries;
};

struct _FuFlashromContext {
	GObject parent_instance;
	FuFlashromOpener *opener;
	GMutexLocker *lock;
#ifdef HAVE_LIBFLASHROM
	struct flashrom_programmer *programmer;
	struct flashrom_flashctx *flashctx;
	struct flashrom_layout *layout;
#endif
};

G_DEFINE_TYPE (FuFlashromContext, fu_flashrom_context, G_TYPE_OBJECT)

struct fmap_area {
	guint32 offset;
	guint32 size;
	guint8 name[32];
	guint16 flags;
} __attribute__((packed));

struct fmap {
	guint8 signature[8];
	guint8 ver_major;
	guint8 ver_minor;
	guint64 base;
	guint32 size;
	guint8 name[32];
	guint16 n_areas;
	struct fmap_area areas[];
} __attribute__((packed));

#ifdef HAVE_LIBFLASHROM
static int
log_callback (enum flashrom_log_level level, const char *fmt, va_list args)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
	g_autofree gchar *message = g_strdup_vprintf (fmt, args);
#pragma clang diagnostic pop
	g_autofree gchar *str = fu_common_strstrip (message);

	if (g_strcmp0 (str, "OK.") == 0 || g_strcmp0 (str, ".") == 0)
		return 0;

	switch (level) {
		case FLASHROM_MSG_ERROR:
		case FLASHROM_MSG_WARN:
			g_warning ("%s", str);
			break;
		case FLASHROM_MSG_INFO:
			g_debug ("%s", str);
			break;
		case FLASHROM_MSG_DEBUG:
		case FLASHROM_MSG_DEBUG2:
			if (g_getenv ("FWUPD_FLASHROM_VERBOSE") != NULL)
				g_debug ("%s", str);
			break;
		default:
			break;
	}
	return 0;
}

static gboolean
context_layout_from_ifd (FuFlashromContext *context,
			 GError **error)
{
	int rc = flashrom_layout_read_from_ifd (&context->layout,
						context->flashctx,
						NULL, 0);

	if (rc == 6) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
				     "descriptor parsing is not available");
		return FALSE;
	} else if (rc == 3) {
		g_set_error_literal (error, FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "flash descriptor could not be parsed");
		return FALSE;
	} else if (rc == 2) {
		g_set_error_literal (error, FWUPD_ERROR,
				     FWUPD_ERROR_READ,
				     "failed to read flash descriptor");
		return FALSE;
	} else if (rc != 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_READ,
			     "unspecified error reading flash descriptor: %d",
			     rc);
		return FALSE;
	}

	return TRUE;
}

static gboolean
context_layout_from_static (FuFlashromContext *self,
			    GPtrArray *regions,
			    GError **error)
{
	gsize fmap_size = sizeof (struct fmap)
		+ (regions->len * sizeof (struct fmap_area));
	g_autofree struct fmap *fmap = g_malloc0 (fmap_size);
	int rc;

	/* construct header */
	*fmap = (struct fmap){
		.signature = "__FMAP__",
		.ver_major = 1,
		.ver_minor = 1,
		.base = 0,
		/* total firmware size is unimportant for setting layout, but
		 * use fmap_size as a plausible value */
		.size = fmap_size,
		.n_areas = regions->len,
	};

	/* fill in areas */
	for (gsize i = 0; i < regions->len; i++) {
		struct fu_flashrom_opener_layout_region *region =
			g_ptr_array_index (regions, i);
		fmap->areas[i] = (struct fmap_area){
			.offset = region->offset,
			.size = region->size,
		};
		strncpy ((char *)&fmap->areas[i].name, region->name,
			 sizeof (fmap->areas[i].name) - 1);
	}

	/* parse fmap into flashrom layout */
	rc = flashrom_layout_read_fmap_from_buffer (&self->layout, self->flashctx,
						    (uint8_t *)fmap, fmap_size);
	if (rc != 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "failed to parse flashmap: error code %d", rc);
		return FALSE;
	}
	return TRUE;
}

/**
 * context_reset_layout:
 *
 * reset the context's layout to match the pristine layout set on the opener.
 */
static gboolean
context_reset_layout (FuFlashromContext *self,
		      GError **error)
{
	enum fu_flashrom_opener_layout_kind layout_kind;
	const union fu_flashrom_opener_layout_data *layout_data;

	if (self->layout != NULL) {
		/* empty the layout manually via a non-public API, since we
		 * cannot guarantee the library will do it on layout release */
		for (size_t i = 0; i < self->layout->num_entries; i++)
			self->layout->entries[i].included = false;

		flashrom_layout_release (self->layout);
		self->layout = NULL;
	}

	layout_kind = fu_flashrom_opener_get_layout(self->opener, &layout_data);

	switch (layout_kind) {
		case FLASHROM_OPENER_LAYOUT_KIND_UNSET:
			break;
		case FLASHROM_OPENER_LAYOUT_KIND_IFD:
			if (!context_layout_from_ifd (self, error))
				return FALSE;
			break;
		case FLASHROM_OPENER_LAYOUT_KIND_STATIC:
			if (!context_layout_from_static (self,
							 layout_data->static_regions,
							 error))
				return FALSE;
			break;
		default:
			g_warn_if_reached();
			break;
	}
	flashrom_layout_set (self->flashctx, self->layout);
	return TRUE;
}
#endif

/**
 * fu_flashrom_context_open:
 * @opener: the opener to take parameters from
 * @context: (out): handle to the flashrom programmer
 *
 * open the flashrom library
 *
 * Returns: TRUE on success, or FALSE with @error set
 */
gboolean
fu_flashrom_context_open (FuFlashromOpener *opener,
			  FuFlashromContext **context,
			  GError **error)
{
#ifndef HAVE_LIBFLASHROM
	g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
		             "libflashrom support is not enabled");
	return FALSE;
#else
	g_autoptr(FuFlashromContext) out = g_object_new (FU_TYPE_FLASHROM_CONTEXT, NULL);
	struct flashrom_programmer *programmer;
	struct flashrom_flashctx *flashctx;
	const gchar *programmer_name = fu_flashrom_opener_get_programmer (opener);
	/* flashrom_programmer_init mutates the args string provided to it, so
	 * we must make a copy */
	g_autofree gchar *programmer_args =
		g_strdup (fu_flashrom_opener_get_programmer_args (opener));
	int rc;

	out->opener = g_object_ref (opener);

	if ((rc = flashrom_init (1)) != 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "libflashrom initialization failed with code %d",
			     rc);
		return FALSE;
	}
	flashrom_set_log_callback (log_callback);

	g_return_val_if_fail (programmer_name != NULL, FALSE);
	/* NULL programmer_args is valid, implying none */
	if ((rc = flashrom_programmer_init (&programmer,
					    programmer_name,
					    programmer_args)) != 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "flashrom programmer initialization failed with code %d",
			     rc);
		return FALSE;
	}
	out->programmer = programmer;

	rc = flashrom_flash_probe(&flashctx, programmer, NULL);
	if (rc == 3) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_READ,
				     "multiple flash chips found, expected only one");
		return FALSE;
	} else if (rc == 2) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_READ,
				     "no flash chips found");
		return FALSE;
	} else if (rc != 0) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_READ,
				     "unspecified error while probing flash");
		return FALSE;
	}
	out->flashctx = flashctx;

	if (!context_reset_layout (out, error))
		return FALSE;

	*context = g_steal_pointer (&out);
	return TRUE;
#endif
}

static void
fu_flashrom_context_init (FuFlashromContext *self)
{
	/*
	 * This mutex guards libflashrom library initialization, ensuring at
	 * most one user of the library is active at any time because
	 * libflashrom is not reentrant and may not be safe to use concurrently.
	 */
	static GMutex flashrom_open;

	self->lock = g_mutex_locker_new (&flashrom_open);
	self->opener = NULL;
	self->programmer = NULL;
	self->flashctx = NULL;
	self->layout = NULL;
}

static void
fu_flashrom_context_finalize (GObject *object)
{
	FuFlashromContext *self = FU_FLASHROM_CONTEXT (object);

	if (self->opener != NULL)
		g_object_unref (self->opener);
#ifdef HAVE_LIBFLASHROM
	if (self->layout != NULL)
		flashrom_layout_release (self->layout);
	if (self->flashctx != NULL)
		flashrom_flash_release (self->flashctx);
	if (self->programmer != NULL)
		flashrom_programmer_shutdown (self->programmer);
	flashrom_shutdown();
#endif
	g_mutex_locker_free (self->lock);

	G_OBJECT_CLASS (fu_flashrom_context_parent_class)->finalize (object);
}

static void
fu_flashrom_context_class_init (FuFlashromContextClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = fu_flashrom_context_finalize;
}

/**
 * fu_flashrom_context_get_flash_size:
 *
 * Returns: size of the flash
 *
 * Since: 1.6.1
 */
gsize
fu_flashrom_context_get_flash_size (FuFlashromContext *self)
{
#ifdef HAVE_LIBFLASHROM
	return (gsize) flashrom_flash_getsize (self->flashctx);
#else
	return 0;
#endif
}

/**
 * fu_flashrom_context_set_included_regions:
 * @first_region: name of the first region to include, or NULL
 * @...: additional region names to include, terminated with NULL
 *
 * Include only the listed flash regions (identified by their names) in any
 * following flash operations. Flash regions are configured from those detected
 * at the time the context was opened, via fu_flashrom_opener_set_layout_*.
 *
 * Returns: TRUE on success, FALSE otherwise with @error set
 *
 * Since: 1.6.1
 */
gboolean
fu_flashrom_context_set_included_regions (FuFlashromContext *self,
					  GError **error,
					  const gchar *first_region,
					  ...)
{
#ifndef HAVE_LIBFLASHROM
	g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "libflashrom support is not enabled");
	return FALSE;
#else
	va_list ap;

	context_reset_layout (self, error);

	va_start (ap, first_region);
	for (const gchar *region = first_region; region	!= NULL;
	     region = va_arg(ap, const gchar *)) {
		if (flashrom_layout_include_region (self->layout, region) != 0) {
			va_end (ap);
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "layout does not have a region \"%s\"",
				     region);
			return FALSE;
		}
	}

	va_end (ap);
	return TRUE;
#endif
}

/**
 * fu_flashrom_context_read_image:
 * @data: (out): data currently in the flash
 *
 * Read the contents of device flash (only within the selected regions).
 *
 * Returns: FALSE on error, otherwise TRUE
 *
 * Since: 1.6.1
 */
gboolean
fu_flashrom_context_read_image (FuFlashromContext *self,
				GBytes **data,
				GError **error)
{
#ifndef HAVE_LIBFLASHROM
	g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "libflashrom support is not enabled");
	return FALSE;
#else
	gsize buf_size = (gsize) flashrom_flash_getsize (self->flashctx);
	g_autofree void *buf = g_malloc (buf_size);
	int rc;

	rc = flashrom_image_read(self->flashctx, buf, buf_size);
	if (rc == 2) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "buffer of size %zu is too small for flash image",
			     (size_t) buf_size);
		return FALSE;
	} else if (rc != 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_READ,
			     "failed to read flash contents: error code %d",
			     rc);
		return FALSE;
	}

	*data = g_bytes_new_take (g_steal_pointer (&buf), buf_size);
	return TRUE;
#endif
}

/**
 * fu_flashrom_context_write_image:
 * @data: (transfer none): data to write to flash
 * @verify: if TRUE, also immediately verify the written data
 *
 * Write the given data (only within the selected regions) to flash.
 *
 * Returns: FALSE on error, otherwise TRUE
 *
 * Since: 1.6.1
 */
gboolean
fu_flashrom_context_write_image (FuFlashromContext *self,
				 GBytes *data,
				 gboolean verify,
				 GError **error)
{
#ifndef HAVE_LIBFLASHROM
	g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "libflashrom support is not enabled");
	return FALSE;
#else
	gsize buf_size;
	const void *buf = g_bytes_get_data (data, &buf_size);
	int rc;

	flashrom_flag_set (self->flashctx, FLASHROM_FLAG_VERIFY_AFTER_WRITE,
			   (bool) verify);
	rc = flashrom_image_write (self->flashctx, (void *) buf,
				   (size_t) buf_size, NULL);
	if (rc == 4) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "buffer of %zu bytes does not match flash size",
			     (size_t ) buf_size);
		return FALSE;
	} else if (rc != 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_WRITE,
			     "failed to write flash contents: error code %d",
			     rc);
		return FALSE;
	}

	return TRUE;
#endif
}

/**
 * fu_flashrom_context_verify_image:
 * @data: (transfer none): expected data
 *
 * Verify that the contents of flash match those of @data (only within the
 * selected regions).
 *
 * Returns: FALSE on error (including flash contents differing from @data),
 * TRUE otherwise.
 *
 * Since: 1.6.1
 */
gboolean
fu_flashrom_context_verify_image (FuFlashromContext *self,
				  GBytes *data,
				  GError **error)
{
#ifndef HAVE_LIBFLASHROM
	g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "libflashrom support is not enabled");
	return FALSE;
#else
	gsize data_size;
	const void *data_buf = g_bytes_get_data (data, &data_size);

	int rc = flashrom_image_verify (self->flashctx, data_buf,
					(size_t) data_size);
	if (rc == 3) {
		g_set_error_literal (error,
				     FWUPD_ERROR, FWUPD_ERROR_WRITE,
				     "flash contents did not match expected");
		return FALSE;
	} else if (rc == 2) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "verify buffer of %zu bytes does not match flash size",
			     data_size);
		return FALSE;
	} else if (rc != 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "unspecified flash verify error (code %d)", rc);
		return FALSE;
	}
	return TRUE;
#endif
}
