/*
 * Copyright 2024 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later???
 */

#define G_LOG_DOMAIN "GParcelable"

#include "config.h"

#include <glib-unix.h>
#include <glib.h>
#if HAS_BINDER_NDK
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/persistable_bundle.h>
#endif /* HAS_BINDER_NDK */

#include "gparcelable.h"

/* Should this be defined in the header? Can we avoid exposing AParcel etc.? */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AStatus, AStatus_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AParcel, AParcel_delete)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(APersistableBundle, APersistableBundle_delete)

typedef struct GPArrayData {
	GVariant *value;
	GError **error;
} GPArrayData;

static binder_status_t
gp_parcel_variant_array_element_writer(AParcel *parcel, const void *array_data, size_t index)
{
	// GVariantIter *array_iter = (GVariantIter *)array_data;
	GPArrayData *user_data = (GPArrayData *)array_data;
	GError **error = user_data->error;
	// TODO: Are we safe to ignore index or should we use g_variant_get_child?
	// GVariant *value = g_variant_iter_next_value(user_data->array_iter);
	GVariant *child = g_variant_get_child_value(user_data->value, index);
	binder_status_t nstatus = STATUS_OK;

	// TODO: do some values require a nullable flag?
	// AParcel_writeInt32(parcel, 1);//parcel

	g_debug("gp_parcel_variant_array_element_writer %ld %s",
		index,
		g_variant_get_type_string(child));
	nstatus = gp_parcel_write_variant(parcel, child, error);

	if (error && *error)
		nstatus = (*error)->code;

	g_variant_unref(child);

	return nstatus;
}

static const char *
gp_parcel_string_array_element_getter(const void *array_data, gsize index, gint32 *string_length)
{
	GVariant *string_array = (GVariant *)array_data;
	const gchar *string;

	// TODO: maybe string array "ams"?
	/* Use &s to get a reference to the string which will be copied using AParcel_writeString
	 * and not freed */
	g_variant_get_child(string_array, index, "&s", &string);
	*string_length = strlen(string);

	return string;
}

static bool
gp_parcel_boolean_array_element_getter(const void *array_data, gsize index)
{
	GVariant *array_value = (GVariant *)array_data;
	gboolean value;

	g_variant_get_child(array_value, index, "b", &value);

	return value;
}

// TODO: Should APersistableBundle take a APersistableBundle as an argument to write to?
APersistableBundle *
gp_vardict_to_persistable_bundle(GVariant *vardict, GError **error)
{
	g_autoptr(APersistableBundle) bundle = APersistableBundle_new();
	const gchar *key = NULL;
	GVariant *value = NULL;
	GVariantIter iter;
	g_variant_iter_init(&iter, vardict);

	while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
		const GVariantType *const type = g_variant_get_type(value);
		const GVariantType *element_type = NULL;
		GVariantClass vclass = g_variant_classify(value);
		gconstpointer array_data = NULL;
		gsize fixed_array_size = 0;
		g_autoptr(APersistableBundle) child_bundle = NULL;
		g_debug("gp_vardict_to_persistable_bundle adding %s %s",
			key,
			g_variant_get_type_string(value));
		switch (vclass) {
		/**
		 * Unlike AParcel APersistableBundle's arrays are not nullable (Negative length
		 *arrays are invalid) Therefore we can ignore G_VARIANT_CLASS_MAYBE
		 **/
		case G_VARIANT_CLASS_BOOLEAN:
			APersistableBundle_putBoolean(bundle, key, g_variant_get_boolean(value));
			break;
		case G_VARIANT_CLASS_BYTE:
			APersistableBundle_putInt(bundle, key, g_variant_get_byte(value));
			break;
		case G_VARIANT_CLASS_INT16:
			APersistableBundle_putInt(bundle, key, g_variant_get_int16(value));
			break;
		case G_VARIANT_CLASS_UINT16:
			APersistableBundle_putInt(bundle, key, g_variant_get_uint16(value));
			break;
		case G_VARIANT_CLASS_INT32:
			APersistableBundle_putInt(bundle, key, g_variant_get_int32(value));
			break;
		case G_VARIANT_CLASS_UINT32:
			APersistableBundle_putInt(bundle, key, g_variant_get_uint32(value));
			break;
		case G_VARIANT_CLASS_INT64:
			APersistableBundle_putLong(bundle, key, g_variant_get_int64(value));
			break;
		case G_VARIANT_CLASS_UINT64:
			APersistableBundle_putLong(bundle, key, g_variant_get_uint64(value));
			break;
		case G_VARIANT_CLASS_DOUBLE:
			APersistableBundle_putDouble(bundle, key, g_variant_get_double(value));
			break;
		case G_VARIANT_CLASS_STRING:
			APersistableBundle_putString(bundle,
						     key,
						     g_variant_get_string(value, NULL));
			break;
		// TODO: In which case would it be case G_VARIANT_CLASS_VARIANT:
		case G_VARIANT_CLASS_ARRAY:
			element_type = g_variant_type_element(type);
			// TODO: This is effectively how g_variant_classify works, but is it okay to
			// do outside the library?
			switch (g_variant_type_peek_string(element_type)[0]) {
			case G_VARIANT_CLASS_BOOLEAN:
				array_data = g_variant_get_fixed_array(value,
								       &fixed_array_size,
								       sizeof(guchar));
				APersistableBundle_putBooleanVector(bundle,
								    key,
								    array_data,
								    fixed_array_size);
				break;
			case G_VARIANT_CLASS_INT32:
			case G_VARIANT_CLASS_UINT32:
				array_data = g_variant_get_fixed_array(value,
								       &fixed_array_size,
								       sizeof(guint32));
				APersistableBundle_putIntVector(bundle,
								key,
								array_data,
								fixed_array_size);
				break;
			case G_VARIANT_CLASS_INT64:
			case G_VARIANT_CLASS_UINT64:
				array_data = g_variant_get_fixed_array(value,
								       &fixed_array_size,
								       sizeof(guint64));
				APersistableBundle_putLongVector(bundle,
								 key,
								 array_data,
								 fixed_array_size);
				break;
			case G_VARIANT_CLASS_DOUBLE:
				array_data = g_variant_get_fixed_array(value,
								       &fixed_array_size,
								       sizeof(gdouble));
				APersistableBundle_putDoubleVector(bundle,
								   key,
								   array_data,
								   fixed_array_size);
				break;
			case G_VARIANT_CLASS_STRING:
				array_data = (gpointer)g_variant_get_strv(value, &fixed_array_size);
				APersistableBundle_putStringVector(bundle,
								   key,
								   (const gchar **)array_data,
								   fixed_array_size);
				break;
			case G_VARIANT_CLASS_DICT_ENTRY:
				/* Not an array, a vardict */
				/* putPersistableBundle deep copies the child bundle */
				child_bundle = gp_vardict_to_persistable_bundle(value, error);
				APersistableBundle_putPersistableBundle(bundle, key, child_bundle);
				break;
			default:
				g_set_error(error,
					    GP_ERROR,
					    STATUS_BAD_VALUE,
					    "Couldn't add %s of type %s to PersistableBundle",
					    key,
					    g_variant_get_type_string(value));
			}
			break;
		default:
			g_set_error(error,
				    GP_ERROR,
				    STATUS_BAD_VALUE,
				    "Couldn't add %s of type %s to PersistableBundle",
				    key,
				    g_variant_get_type_string(value));
		}
		g_variant_unref(value);
	}

	g_debug("gp_vardict_to_persistable_bundle done %d", APersistableBundle_size(bundle));

	return g_steal_pointer(&bundle);
}

static binder_status_t
gp_parcel_write_null(AParcel *parcel, const GVariantType *type, GError **error)
{
	binder_status_t status = STATUS_OK;
	GVariantClass class = g_variant_type_peek_string(type)[0];
	const GVariantType *element_type = NULL;
	g_info("writing null value of type %s", g_variant_type_peek_string(type));
	// TODO: What values are nullable in GVariant? What about in parcels? If GVariant primitives
	// can be nullable, we should write default values to the parcel???
	//     Or we should throw an error. That is to say, GVariants with maybe primitives cannot
	//     be directly mapped to AIDL parcels
	/* Annotations (nullable) can't be attached to primitive types. */
	/* typed (primitive) arrays use the same array function as AParcel_writeParcelableArray so
	 * this should be fine */
	switch (class) {
	case G_VARIANT_CLASS_ARRAY:
		element_type = g_variant_type_element(type);
		// TODO: parcel array entries are nullable can we discard maybes
		if (g_variant_type_is_maybe(element_type)) {
			element_type = g_variant_type_element(element_type);
		}
		switch (g_variant_type_peek_string(element_type)[0]) {
		case G_VARIANT_CLASS_DICT_ENTRY:
		case G_VARIANT_CLASS_ARRAY:
			status =
			    AParcel_writeParcelableArray(parcel,
							 NULL,
							 -1,
							 gp_parcel_variant_array_element_writer);
			break;
		default:
			g_set_error(error,
				    GP_ERROR,
				    STATUS_BAD_VALUE,
				    "Cannot write null for type %s to Parcel",
				    g_variant_type_peek_string(type));
		}
		break;
	case G_VARIANT_CLASS_STRING:
		status = AParcel_writeString(parcel, NULL, -1);
		break;
	default:
		g_set_error(error,
			    GP_ERROR,
			    STATUS_BAD_VALUE,
			    "Cannot write null for type %s to Parcel",
			    g_variant_type_peek_string(type));
	}

	return status;
}

binder_status_t
gp_parcel_write_variant(AParcel *parcel, GVariant *value, GError **error)
{
	GVariantIter iter;
	GVariantClass vclass = g_variant_classify(value);
	const GVariantType *const type = g_variant_get_type(value);
	const GVariantType *element_type = NULL;
	g_autoptr(APersistableBundle) bundle = NULL;
	GVariant *child = NULL;
	binder_status_t status = STATUS_OK;
	gconstpointer array_data = NULL;
	gsize fixed_array_size = 0;

	g_debug("gp_parcel_write_variant %s", g_variant_get_type_string(value));
	switch (vclass) {
	case G_VARIANT_CLASS_MAYBE:
		child = g_variant_get_maybe(value);
		if (child) {
			g_debug("maybe value is %s", g_variant_get_type_string(child));
			if (g_variant_type_equal(g_variant_get_type(child),
						 G_VARIANT_TYPE_STRING)) {
				g_debug(" - value is %s", g_variant_get_string(child, NULL));
			}
			g_debug("gp_parcel_write_variant process maybe value %s",
				g_variant_get_type_string(child));
			status = gp_parcel_write_variant(parcel, child, error);
			g_variant_unref(child);
		} else {
			g_debug("maybe value is none");
			status = gp_parcel_write_null(parcel, g_variant_type_element(type), error);
		}
		break;
	case G_VARIANT_CLASS_BOOLEAN:
		status = AParcel_writeBool(parcel, g_variant_get_boolean(value));
		break;
	case G_VARIANT_CLASS_BYTE:
		status = AParcel_writeByte(parcel, g_variant_get_byte(value));
		break;
	case G_VARIANT_CLASS_INT32:
		status = AParcel_writeInt32(parcel, g_variant_get_int32(value));
		break;
	case G_VARIANT_CLASS_UINT32:
		status = AParcel_writeUint32(parcel, g_variant_get_uint32(value));
		break;
	case G_VARIANT_CLASS_INT64:
		status = AParcel_writeInt64(parcel, g_variant_get_int64(value));
		break;
	case G_VARIANT_CLASS_UINT64:
		status = AParcel_writeUint64(parcel, g_variant_get_int64(value));
		break;
	case G_VARIANT_CLASS_DOUBLE:
		status = AParcel_writeDouble(parcel, g_variant_get_double(value));
		break;
	case G_VARIANT_CLASS_HANDLE:
		status = AParcel_writeParcelFileDescriptor(parcel, g_variant_get_handle(value));
		break;
	case G_VARIANT_CLASS_STRING:
		array_data = g_variant_get_string(value, &fixed_array_size);
		g_debug("  adding string \"%s\" to parcel", (gchar *)array_data);
		status = AParcel_writeString(parcel, array_data, fixed_array_size);
		break;

	/**
	 * Array covers the following data types we can handle:
	 *  + vardict `a{sv}`
	 *  + typed arrays `ax` where x is a basic type
	 *  + Array of parcelables `ax` where x is one of vardict, array, ???
	 **/
	case G_VARIANT_CLASS_ARRAY:
		element_type = g_variant_type_element(type);
		switch (g_variant_type_peek_string(element_type)[0]) {
		case G_VARIANT_CLASS_DICT_ENTRY:
			/* Write a PersistableBundle */
			/* Convert dict to persistable bundle */
			bundle = gp_vardict_to_persistable_bundle(value, error);
			if (error && *error)
				break;
			/* Write nullable flag */
			status = AParcel_writeInt32(parcel, !!bundle);
			if (status != STATUS_OK)
				break;
			/* Write persistable bundle */
			status = APersistableBundle_writeToParcel(bundle, parcel);
			break;
		case G_VARIANT_CLASS_BOOLEAN:
			status = AParcel_writeBoolArray(parcel,
							value,
							g_variant_n_children(value),
							gp_parcel_boolean_array_element_getter);
			break;
		case G_VARIANT_CLASS_BYTE:
			array_data =
			    g_variant_get_fixed_array(value, &fixed_array_size, sizeof(guint8));
			status = AParcel_writeByteArray(parcel, array_data, fixed_array_size);
			break;
		case G_VARIANT_CLASS_INT32:
			array_data =
			    g_variant_get_fixed_array(value, &fixed_array_size, sizeof(gint32));
			status = AParcel_writeInt32Array(parcel, array_data, fixed_array_size);
			break;
		case G_VARIANT_CLASS_UINT32:
			array_data =
			    g_variant_get_fixed_array(value, &fixed_array_size, sizeof(guint32));
			status = AParcel_writeUint32Array(parcel, array_data, fixed_array_size);
			break;
		case G_VARIANT_CLASS_INT64:
			array_data =
			    g_variant_get_fixed_array(value, &fixed_array_size, sizeof(gint64));
			status = AParcel_writeInt64Array(parcel, array_data, fixed_array_size);
			break;
		case G_VARIANT_CLASS_UINT64:
			array_data =
			    g_variant_get_fixed_array(value, &fixed_array_size, sizeof(guint64));
			status = AParcel_writeUint64Array(parcel, array_data, fixed_array_size);
			break;
		case G_VARIANT_CLASS_DOUBLE:
			/* parcels support float and double, variants only double */
			array_data =
			    g_variant_get_fixed_array(value, &fixed_array_size, sizeof(gdouble));
			status = AParcel_writeDoubleArray(parcel, array_data, fixed_array_size);
			break;
		case G_VARIANT_CLASS_STRING:
			// TODO: AParcel_stringArrayElementGetter
			status = AParcel_writeStringArray(parcel,
							  value,
							  g_variant_n_children(value),
							  gp_parcel_string_array_element_getter);
			break;
		default: {
			// TODO: Do I need to specify types that can be attempted this way?
			// g_variant_iter_init(&iter, value);
			// struct {
			//    GVariantIter *iter;
			//    GError **error;
			//} user_data = {&iter, error};
			// status = AParcel_writeParcelableArray(parcel, &user_data,
			//    g_variant_iter_n_children(&iter),
			//    gp_parcel_variant_array_element_writer);
			GPArrayData parcelable_array_data = {value, error};
			status =
			    AParcel_writeParcelableArray(parcel,
							 &parcelable_array_data,
							 g_variant_n_children(value),
							 gp_parcel_variant_array_element_writer);
		} break;
		}
		break;
	case G_VARIANT_CLASS_TUPLE:
		// TODO: These would probably be extremely useful for passing arguments
		// iter tuple children calling parcel_write_variant, I think? Parcels are tuples?
		g_variant_iter_init(&iter, value);
		while ((child = g_variant_iter_next_value(&iter))) {
			g_debug("gp_parcel_write_variant process tuple value %s, %s",
				g_variant_get_type_string(child),
				g_variant_print(child, TRUE));
			status = gp_parcel_write_variant(parcel, child, error);
			g_debug("gp_parcel_write_variant finish process tuple value");
			g_variant_unref(child);
			if (error && *error) {
				break;
			}
		}
		break;
	default:
		g_warning("Cannot encode type \"%s\" as Parcel", g_variant_get_type_string(value));
		g_info(" parcel %s", g_variant_print(value, true));
	}

	// TODO: make sure there are no branches that don't make sense
	// TODO: How do we test this?
	// status = STATUS_NO_MEMORY;
	if (error && *error) {
		// TODO: setting status is unnecessary if we're using error
		status = (*error)->code;
	} else if (status != STATUS_OK) {
		g_set_error(error,
			    GP_ERROR,
			    status,
			    "Failed to write %s to parcel (%d)",
			    g_variant_get_type_string(value),
			    status);
	}

	g_debug("gp_parcel_write_variant done ");
	return status;
}

static gboolean
gp_parcel_to_variant_inner(GVariantBuilder *builder,
			   const AParcel *parcel,
			   const GVariantType *type,
			   gboolean is_root,
			   gboolean is_in_tuple,
			   GError **error);

// TODO: AParcel to GVariant
// binder_status_t
// gp_parcel_read

// TODO: APersistableBundle_stringAllocator that allocates g_variant_dict (context) entries?
static char *
bundle_string_allocator(gint32 size_bytes, void *context)
{
	return g_malloc0(size_bytes);
}

typedef struct {
	GVariantBuilder *builder;
	const GVariantType *type;
	GError **error;
	gint32 length;
	gboolean to_open;
	gboolean is_null;
	gboolean is_maybe;
	gboolean is_in_tuple;
	gboolean is_root;
} BuilderArray;

static bool
parcelable_array_allocator_builder(gpointer user_data, gint32 length)
{
	BuilderArray *ptr_array = user_data;
	g_debug("builder allocate %p to %d", user_data, length);

	// length -1 designates a null array
	ptr_array->length = length;
	ptr_array->is_null = length < 0;
	ptr_array->is_maybe = g_variant_type_is_maybe(ptr_array->type);

	// TODO: If we are currently in a tuple we must acknowledge the null maybe
	//   However if the builder is a bare maybe array we must not
	//   Is this a bug in GVariantBuilder?

	// We are in a maybe
	if (ptr_array->is_maybe) {
		// the parcelable is null don't write anything
		if (ptr_array->is_null) {
			g_debug("array builder maybe is null %s",
				g_variant_type_peek_string(ptr_array->type));
			// unless we are in a container that requires us to write
			if (ptr_array->is_in_tuple) {
				g_variant_builder_add_value(
				    ptr_array->builder,
				    g_variant_new_maybe(g_variant_type_element(ptr_array->type),
							NULL));
			}
			return TRUE;
		}
		// update the type to the real array type
		// do we need to open the maybe?
		g_debug("array builder unwrapping maybe type %s",
			g_variant_type_peek_string(ptr_array->type));
		if (ptr_array->is_in_tuple) {
			g_variant_builder_open(ptr_array->builder, ptr_array->type);
		}
		ptr_array->type = g_variant_type_element(ptr_array->type);
		ptr_array->is_root = FALSE;
	}

	if (length > 0) {
		g_debug("array builder open array %s, %s root, %s in tuple",
			g_variant_type_peek_string(ptr_array->type),
			ptr_array->is_root ? "is" : "isn't",
			ptr_array->is_in_tuple ? "is" : "isn't");
		// open array (close in when the final element is written)
		if (!ptr_array->is_root)
			g_variant_builder_open(ptr_array->builder, ptr_array->type);
	} else {
		g_debug("array builder add empty array %s",
			g_variant_type_peek_string(ptr_array->type));
		// write an empty array
		if (ptr_array->is_maybe) {
			g_variant_builder_open(ptr_array->builder, ptr_array->type);
			g_variant_builder_close(ptr_array->builder);
			if (ptr_array->is_in_tuple) {
				g_variant_builder_close(ptr_array->builder);
			}
		}
	}

	return TRUE;
}

static binder_status_t
read_parcelable_element_builder(const AParcel *parcel, gpointer user_data, gsize index)
{
	// TODO: Can I avoid the g_ptr_array and just call gp_parcel_to_variant
	BuilderArray *ptr_array = user_data;
	gint32 is_some = TRUE;
	const GVariantType *element_type = g_variant_type_element(ptr_array->type);
	gboolean is_maybe = g_variant_type_is_maybe(element_type);
	g_debug("builder setting element %ld to %p", index, parcel);

	// Pass generic maybe to next stage
	if (!is_maybe) {
		AParcel_readInt32(parcel, &is_some);
	}

	// Skip non-maybe non-null values
	if (is_some) {
		if (ptr_array->to_open) {
			gp_parcel_to_variant_inner(ptr_array->builder,
						   parcel,
						   element_type,
						   FALSE,
						   FALSE,
						   ptr_array->error);
		} else {
			gp_parcel_to_variant_inner(ptr_array->builder,
						   parcel,
						   ptr_array->type,
						   FALSE,
						   FALSE,
						   ptr_array->error);
		}
	}
	// if (is_maybe) {
	//	// Unwrap maybe type
	//	element_type = g_variant_type_element(element_type);
	// }
	//  TODO: Handle maybe element???
	// if (is_some) {
	//  TODO:
	// gp_parcel_to_variant(ptr_array->builder, parcel, element_type, ptr_array->error);
	// } else if (is_maybe) {
	//	g_variant_builder_add_value(ptr_array->builder, g_variant_new_maybe(element_type,
	// NULL));
	// }

	if (index == (gsize)ptr_array->length - 1) {
		g_debug("builder close array builder");
		if (!ptr_array->is_root)
			g_variant_builder_close(ptr_array->builder);

		if (ptr_array->is_maybe && ptr_array->is_in_tuple)
			g_variant_builder_close(ptr_array->builder);
	}
	return STATUS_OK;
}

static bool
parcel_nullable_string_allocator(void *string_data, gint32 length, gchar **buffer)
{
	gchar **string = (gchar **)string_data;

	g_debug("parcel_nullable_string_allocator %d", length);

	if (length == 0)
		return FALSE;

	if (length < 0) {
		*string = NULL;
	} else {
		*string = g_malloc0(length);
		*buffer = *string;
	}
	return TRUE;
}

// AParcel_stringArrayElementAllocator
static bool
parcel_string_array_element_allocator(gpointer array_data,
				      gsize index,
				      gint32 length,
				      gchar **buffer)
{
	GStrv *strvp = (GStrv *)array_data;
	GStrv strv = (GStrv)*strvp;

	g_debug("parcel_string_array_element_allocator %d", length);

	return parcel_nullable_string_allocator((void *)&(strv[index]), length, buffer);
}

// AParcel_stringArrayAllocator
static bool
parcel_string_array_allocator(gpointer array_data, gint32 length)
{
	GStrv *strv = (GStrv *)array_data;

	g_debug("parcel_string_array_allocator %d", length);

	if (length < 0)
		return false;

	*strv = g_new0(gchar *, length + 1);

	return true;
}

// TODO: Rewrite this to reduce depth of switch claueses checking type
/* The type should be the same as used to create the builder
 * For example:
 * ```
 * g_variant_builder_init(&builder, type);
 * gp_parcel_to_variant(&builder, parcel, type, &error);
 * ```
 * or
 * ```
 * g_variant_builder_open(&builder, type);
 * gp_parcel_to_variant(&builder, parcel, type, &error);
 * g_variant_builder_close(&builder);
 * ```
 */
gboolean
gp_parcel_to_variant(GVariantBuilder *builder,
		     const AParcel *parcel,
		     const GVariantType *type,
		     GError **error)
{
	return gp_parcel_to_variant_inner(builder, parcel, type, TRUE, FALSE, error);
}

static gboolean
gp_parcel_to_variant_inner(GVariantBuilder *builder,
			   const AParcel *parcel,
			   const GVariantType *type,
			   gboolean is_root,
			   gboolean is_in_tuple,
			   GError **error)
{
	// Builder types take the inner value
	//  where type is 'maa{sv}' and parcel contains null, don't write just return (we aren't in
	//  a container) where type is '(maii)' and parcel contains null and i, write null then i
	//  (we are in a container)
	g_autoptr(AStatus) status = NULL;
	binder_status_t nstatus = STATUS_OK;
	const GVariantType *element_type;
	const GVariantType *element_type_2;
	g_autofree BuilderArray *ba = NULL;
	APersistableBundle *bundle;
	gint32 is_some;

	g_debug("gp_parcel_to_variant %s", g_variant_type_peek_string(type));

	switch (g_variant_type_peek_string(type)[0]) {
	// element_type = g_variant_type_element(type);
	case G_VARIANT_CLASS_MAYBE:
		element_type = g_variant_type_element(type);
		g_debug("  child %s", g_variant_type_peek_string(element_type));
		// TODO: Handle maybe types
		switch (g_variant_type_peek_string(element_type)[0]) {
		case G_VARIANT_CLASS_ARRAY:
			element_type_2 = g_variant_type_element(element_type);
			switch (g_variant_type_peek_string(element_type_2)[0]) {
			case G_VARIANT_CLASS_DICT_ENTRY:
				g_debug("we are a maybe dict, not a maybe array");
				AParcel_readInt32(parcel, &is_some);
				if (is_some) {
					// Open maybe vardict to indicate not null
					g_variant_builder_open(builder, type);
					g_debug("maybe vardict type entry type is %s",
						g_variant_type_peek_string(element_type));
					// TODO: Extract persistable bundle from parcel
					bundle = APersistableBundle_new();
					nstatus =
					    APersistableBundle_readFromParcel(parcel, &bundle);
					if (nstatus != STATUS_OK) {
						status = AStatus_fromStatus(nstatus);
						g_warning(
						    "read persistable bundle from parcel is %s",
						    AStatus_getDescription(status));
					}
					g_variant_builder_open(builder, element_type);
					gp_persistable_bundle_to_vardict(builder, bundle, error);
					g_variant_builder_close(builder);
					g_variant_builder_close(builder);
				}

				break;
			default:
				// TODO: this is a maybe array, first we must open maybe (if not
				// null)
				// TODO: Array header is length or "-1" for null
				ba = g_malloc0(sizeof(BuilderArray));
				ba->error = error;
				ba->builder = builder;
				// TODO: Rather than `to_open = TRUE`
				// `g_variant_type_new_maybe(element_type)`?
				ba->type = type;
				ba->to_open = TRUE;
				ba->is_maybe = TRUE;
				// We're in a maybe
				ba->is_in_tuple = is_in_tuple;
				ba->is_root = TRUE;

				g_debug("build maybe parcelable array");
				nstatus =
				    AParcel_readParcelableArray(parcel,
								(void *)ba,
								parcelable_array_allocator_builder,
								read_parcelable_element_builder);
				if (nstatus != STATUS_OK) {
					status = AStatus_fromStatus(nstatus);
					g_warning("couldn't read parcelable array %s",
						  AStatus_getDescription(status));
				}
			}
			break;
		case G_VARIANT_CLASS_TUPLE:
			// Parcels don't have maybe
			// open tuple
			g_variant_builder_open(builder, element_type);
			if (!gp_parcel_to_variant_inner(builder,
							parcel,
							element_type,
							FALSE,
							TRUE,
							error)) {
				return FALSE;
			}
			g_variant_builder_close(builder);
			break;
		default:
			g_warning("Cannot decode type \"%s\" from Parcel",
				  g_variant_type_peek_string(type));
		}
		break;
	case G_VARIANT_CLASS_TUPLE:
		g_debug("This should be a tuple");
		// builder is already in the tuple, no need to open
		// g_warning("open %s", g_variant_type_peek_string(type));
		// g_variant_builder_open(builder, type);
		for (const GVariantType *itype = g_variant_type_first(type); itype;
		     itype = g_variant_type_next(itype)) {
			if (!gp_parcel_to_variant_inner(builder, parcel, itype, FALSE, TRUE, error))
				return FALSE;
		}
		// g_warning("close %s", g_variant_type_peek_string(type));
		// g_variant_builder_close(builder);
		break;
	case G_VARIANT_CLASS_STRING: {
		g_autofree gchar *string_data = NULL;
		g_debug("moving string from parcel to variant");
		nstatus = AParcel_readString(parcel,
					     (void *)&string_data,
					     parcel_nullable_string_allocator);
		g_debug("string is \"%s\"", string_data);
		g_variant_builder_add_value(
		    builder,
		    g_variant_new_take_string(g_steal_pointer(&string_data)));
		break;
	}
	// TODO: read (u8, i8, u16, i16, f32)
	case G_VARIANT_CLASS_BYTE: {
		guint8 value = 0;
		// Change of signedness here
		AParcel_readByte(parcel, &value);
		g_variant_builder_add_value(builder, g_variant_new_byte(value));
	} break;
	// case G_VARIANT_CLASS_UINT16: No AParcel_readUint16
	// case G_VARIANT_CLASS_INT16: No AParcel_readInt16
	case G_VARIANT_CLASS_UINT32: {
		guint32 value = 0;
		AParcel_readUint32(parcel, &value);
		g_variant_builder_add_value(builder, g_variant_new_uint32(value));
	} break;
	case G_VARIANT_CLASS_INT32: {
		gint32 value = 0;
		AParcel_readInt32(parcel, &value);
		g_variant_builder_add_value(builder, g_variant_new_int32(value));
	} break;
	case G_VARIANT_CLASS_UINT64: {
		guint64 value = 0;
		AParcel_readUint64(parcel, &value);
		g_variant_builder_add_value(builder, g_variant_new_uint64(value));
	} break;
	case G_VARIANT_CLASS_INT64: {
		gint64 value = 0;
		AParcel_readInt64(parcel, &value);
		g_variant_builder_add_value(builder, g_variant_new_int64(value));
	} break;
	case G_VARIANT_CLASS_DOUBLE: {
		gdouble value = 0;
		AParcel_readDouble(parcel, &value);
		g_variant_builder_add_value(builder, g_variant_new_double(value));
	} break;
	case G_VARIANT_CLASS_HANDLE: {
		gint fd = 0;
		AParcel_readParcelFileDescriptor(parcel, &fd);
		g_variant_builder_add_value(builder, g_variant_new_handle(fd));
		break;
	}
	case G_VARIANT_CLASS_ARRAY:
		element_type = g_variant_type_element(type);
		g_debug("  child %s", g_variant_type_peek_string(element_type));
		g_debug("arrayish type is %s class %c",
			g_variant_type_peek_string(type),
			g_variant_type_peek_string(element_type)[0]);

		switch (g_variant_type_peek_string(element_type)[0]) {
		case G_VARIANT_CLASS_DICT_ENTRY:
			g_debug("vardict type entry type is %s",
				g_variant_type_peek_string(element_type));
			// TODO: Extract persistable bundle from parcel
			// TODO: is_some?
			bundle = APersistableBundle_new();
			nstatus = APersistableBundle_readFromParcel(parcel, &bundle);
			if (nstatus != STATUS_OK) {
				status = AStatus_fromStatus(nstatus);
				g_warning("read persistable bundle from parcel is %s",
					  AStatus_getDescription(status));
			}
			g_variant_builder_open(builder, type);
			gp_persistable_bundle_to_vardict(builder, bundle, error);
			g_variant_builder_close(builder);
			break;
		case G_VARIANT_CLASS_STRING: {
			g_auto(GStrv) strv = NULL;

			AParcel_readStringArray(parcel,
						(void *)&strv,
						parcel_string_array_allocator,
						parcel_string_array_element_allocator);
			g_variant_builder_add_value(builder, g_variant_new_strv(strv, -1));
		} break;
		// TODO: Typed arrays (u8, char, bool, i32, u32, f32, i64, u64, f64)
		default:
			// TODO: Check we can use array type and default to error
			ba = g_malloc0(sizeof(BuilderArray));
			ba->error = error;
			ba->builder = builder;
			ba->type = type;
			ba->to_open = TRUE;
			ba->is_in_tuple = is_in_tuple;
			ba->is_root = TRUE;

			g_debug(" - - - - - process parcelable array %s %s",
				g_variant_type_peek_string(type),
				g_variant_type_peek_string(element_type));

			g_debug("build parcelable array");
			nstatus = AParcel_readParcelableArray(parcel,
							      (void *)ba,
							      parcelable_array_allocator_builder,
							      read_parcelable_element_builder);
			if (nstatus != STATUS_OK) {
				status = AStatus_fromStatus(nstatus);
				// g_warning("couldn't read parcelable array %s",
				// AStatus_getDescription(status));
				if (*error == NULL) {
					g_set_error(error,
						    GP_ERROR,
						    nstatus,
						    "couldn't read parcelable array %s",
						    AStatus_getDescription(status));
				}
				return FALSE;
			}

			// TODO: This gives a warning that the builder type doesn't match the maybe
			// type
			if (ba->is_null && g_variant_type_is_tuple(type)) {
				// If we are in a ~container~ _tuple_ we explicitly write a maybe
				// "nothing"
				g_debug(" writing null");
				g_variant_builder_add_value(
				    builder,
				    g_variant_new_maybe(element_type, NULL));
			}
			/*
			g_message("array type is %s", g_variant_type_peek_string(element_type));
			// TODO: Extract parcelable elements array???
			//g_variant_builder_open(builder, element_type);
			nstatus = AParcel_readParcelableArray(parcel,
					      (void *)&parcel_array,
					      parcelable_array_allocator,
					      read_parcelable_element);
			if (nstatus != STATUS_OK) {
				status = AStatus_fromStatus(nstatus);
				g_warning("couldn't read parcelable array %s",
			AStatus_getDescription(status));
			}

			for (guint i = 0; i < parcel_array->len; i++) {
				// TODO: If `am?` (array of maybes) keep null Parcels, if `a?` skip
			maybe values const AParcel *child_parcel = g_ptr_array_index(parcel_array,
			i); if (g_variant_type_is_maybe(element_type)) {
					// TODO: We expect a maybe, let recursion handle it
					gp_parcel_to_variant(builder, child_parcel, element_type,
			error); } else {
					// TODO: We don't expect a maybe, ignore null entries
					gint32 is_some;
					nstatus = AParcel_readInt32(child_parcel, &is_some);
					if (is_some) {
						gp_parcel_to_variant(builder, child_parcel,
			element_type, error);
					}
				}
			}*/
			// g_variant_builder_close(builder);
		}
		break;
	default:
		g_set_error(error,
			    GP_ERROR,
			    0,
			    "Cannot decode type \"%s\" from Parcel",
			    g_variant_type_peek_string(type));
		return FALSE;
	}

	g_debug("finish %s", g_variant_type_peek_string(type));

	return TRUE;
}

// TODO: APersistableBundle to GVariant
//  * Map of string key to
//     + Map of string key to ^
//     + bool
//     + int long
//     + double
//     + string
//     + int
void
gp_persistable_bundle_to_vardict(GVariantBuilder *builder,
				 APersistableBundle *bundle,
				 GError **error)
{
	GStrv str_keys = NULL;
	gint32 keys_size;
	guint32 key_count;

	/* string */
	keys_size =
	    APersistableBundle_getStringKeys(bundle, NULL, 0, bundle_string_allocator, NULL);
	key_count = keys_size / sizeof(gchar *);

	str_keys = g_new0(gchar *, key_count);
	keys_size = APersistableBundle_getStringKeys(bundle,
						     str_keys,
						     keys_size,
						     bundle_string_allocator,
						     NULL);

	for (guint i = 0; i < key_count; i++) {
		g_autofree char *key = str_keys[i];
		g_autofree char *val = NULL;

		if (APersistableBundle_getString(bundle,
						 key,
						 &val,
						 bundle_string_allocator,
						 NULL)) {
			g_variant_builder_add(builder,
					      "{&sv}",
					      g_steal_pointer(&key),
					      g_variant_new_take_string(g_steal_pointer(&val)));
		}
	}

	g_free((void *)str_keys);

	/* bool */
	keys_size =
	    APersistableBundle_getBooleanKeys(bundle, NULL, 0, bundle_string_allocator, NULL);
	key_count = keys_size / sizeof(gchar *);

	str_keys = g_new0(gchar *, key_count);
	keys_size = APersistableBundle_getBooleanKeys(bundle,
						      str_keys,
						      keys_size,
						      bundle_string_allocator,
						      NULL);
	for (guint i = 0; i < key_count; i++) {
		g_autofree char *key = str_keys[i];
		bool val;

		if (APersistableBundle_getBoolean(bundle, key, &val)) {
			g_variant_builder_add(builder,
					      "{&sv}",
					      g_steal_pointer(&key),
					      g_variant_new_boolean(val));
		}
	}

	g_free((void *)str_keys);

	/* i32 */
	keys_size = APersistableBundle_getIntKeys(bundle, NULL, 0, bundle_string_allocator, NULL);
	key_count = keys_size / sizeof(gchar *);

	str_keys = g_new0(gchar *, key_count);
	keys_size = APersistableBundle_getIntKeys(bundle,
						  str_keys,
						  keys_size,
						  bundle_string_allocator,
						  NULL);
	for (guint i = 0; i < key_count; i++) {
		g_autofree char *key = str_keys[i];
		gint32 val;

		if (APersistableBundle_getInt(bundle, key, &val)) {
			g_variant_builder_add(builder,
					      "{&sv}",
					      g_steal_pointer(&key),
					      g_variant_new_int32(val));
		}
	}

	g_free((void *)str_keys);

	/* i64 */
	keys_size = APersistableBundle_getLongKeys(bundle, NULL, 0, bundle_string_allocator, NULL);
	key_count = keys_size / sizeof(gchar *);

	str_keys = g_new0(gchar *, key_count);
	keys_size = APersistableBundle_getLongKeys(bundle,
						   str_keys,
						   keys_size,
						   bundle_string_allocator,
						   NULL);
	for (guint i = 0; i < key_count; i++) {
		g_autofree char *key = str_keys[i];
		gint64 val;

		if (APersistableBundle_getLong(bundle, key, &val)) {
			g_variant_builder_add(builder,
					      "{&sv}",
					      g_steal_pointer(&key),
					      g_variant_new_int64(val));
		}
	}

	g_free((void *)str_keys);

	/* list of strings */
	keys_size =
	    APersistableBundle_getStringVectorKeys(bundle, NULL, 0, bundle_string_allocator, NULL);
	key_count = keys_size / sizeof(gchar *);

	str_keys = g_new0(gchar *, key_count);
	keys_size = APersistableBundle_getStringVectorKeys(bundle,
							   str_keys,
							   keys_size,
							   bundle_string_allocator,
							   NULL);
	for (guint i = 0; i < key_count; i++) {
		g_autofree char *key = str_keys[i];
		gint32 strv_size;
		gint32 strv_len;
		g_autofree GStrv strv = NULL;

		strv_size = APersistableBundle_getStringVector(bundle,
							       key,
							       NULL,
							       0,
							       bundle_string_allocator,
							       NULL);
		strv_len = strv_size / sizeof(gchar *);

		strv = g_new0(gchar *, strv_len + 1);

		if (APersistableBundle_getStringVector(bundle,
						       key,
						       strv,
						       strv_size,
						       bundle_string_allocator,
						       NULL)) {
			g_variant_builder_add(builder,
					      "{&sv}",
					      g_steal_pointer(&key),
					      g_variant_new_strv((const gchar **)strv, -1));
		}
	}

	g_free((void *)str_keys);

	/* vardict */
	keys_size = APersistableBundle_getPersistableBundleKeys(bundle,
								NULL,
								0,
								bundle_string_allocator,
								NULL);
	key_count = keys_size / sizeof(gchar *);

	str_keys = g_new0(gchar *, key_count);
	keys_size = APersistableBundle_getPersistableBundleKeys(bundle,
								str_keys,
								keys_size,
								bundle_string_allocator,
								NULL);
	for (guint i = 0; i < key_count; i++) {
		g_autofree char *key = str_keys[i];
		GVariantBuilder sub_bundle_builder;
		g_autoptr(APersistableBundle) sub_bundle = APersistableBundle_new();

		if (APersistableBundle_getPersistableBundle(bundle, key, &sub_bundle)) {
			g_variant_builder_init(&sub_bundle_builder, G_VARIANT_TYPE("a{sv}"));
			gp_persistable_bundle_to_vardict(&sub_bundle_builder, sub_bundle, error);
			g_variant_builder_add(builder,
					      "{&sv}",
					      g_steal_pointer(&key),
					      g_variant_builder_end(&sub_bundle_builder));
		}
	}

	// TODO: APersistableBundle_getXVectorKeys
}
