/*
 * Copyright 2024 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later???
 */

#include "config.h"

#include "gparcelable.h"

// #include "glibconfig.h"
#include <glib-unix.h>
#include <glib.h>
#if HAS_BINDER_NDK
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/persistable_bundle.h>
#endif /* HAS_BINDER_NDK */

#define G_LOG_DOMAIN "GParcelable"

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

	// TODO: do some values require a nullable flag?
	// AParcel_writeInt32(parcel, 1);//parcel

	g_debug("gp_parcel_variant_array_element_writer %ld %s",
		index,
		g_variant_get_type_string(child));
	binder_status_t status = gp_parcel_write_variant(parcel, child, error);

	if (error && *error)
		status = (*error)->code;

	g_variant_unref(child);

	return status;
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
	GVariantIter iter;
	g_variant_iter_init(&iter, vardict);
	const gchar *key;
	GVariant *value;

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
				array_data = g_variant_get_strv(value, &fixed_array_size);
				APersistableBundle_putStringVector(bundle,
								   key,
								   array_data,
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
		switch (g_variant_type_peek_string(element_type)[0]) {
		case G_VARIANT_CLASS_DICT_ENTRY:
		case G_VARIANT_CLASS_ARRAY:
			status =
			    AParcel_writeParcelableArray(parcel,
							 NULL,
							 -1,
							 gp_parcel_variant_array_element_writer);
			break;
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
			g_warning("gp_parcel_write_variant process maybe value %s",
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
	case G_VARIANT_CLASS_STRING:
		array_data = g_variant_get_string(value, &fixed_array_size);
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
		default:
			// TODO: Do I need to specify types that can be attempted this way?
			// g_variant_iter_init(&iter, value);
			// struct {
			//    GVariantIter *iter;
			//    GError **error;
			//} user_data = {&iter, error};
			// status = AParcel_writeParcelableArray(parcel, &user_data,
			//    g_variant_iter_n_children(&iter),
			//    gp_parcel_variant_array_element_writer);
			GPArrayData array_data = {value, error};
			status =
			    AParcel_writeParcelableArray(parcel,
							 &array_data,
							 g_variant_n_children(value),
							 gp_parcel_variant_array_element_writer);
			break;
		}
		break;
	case G_VARIANT_CLASS_TUPLE:
		// TODO: These would probably be extremely useful for passing arguments
		// iter tuple children calling parcel_write_variant, I think? Parcels are tuples?
		g_variant_iter_init(&iter, value);
		while ((child = g_variant_iter_next_value(&iter))) {
			g_warning("gp_parcel_write_variant process tuple value %s",
				  g_variant_get_type_string(child));
			status = gp_parcel_write_variant(parcel, child, error);
			g_warning("gp_parcel_write_variant finish process tuple value");
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

	return status;
}
