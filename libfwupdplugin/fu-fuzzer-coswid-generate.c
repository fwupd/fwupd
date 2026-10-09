#include "config.h"

#include "fu-coswid-common.h"
#include "fu-coswid-firmware.h"
#include "fu-fuzzer.h"

static FuCborItem *
fu_fuzzer_coswid_one_or_many(FuCborItem *item, gboolean many, GError **error)
{
	g_autoptr(FuCborItem) array = NULL;
	if (!many)
		return fu_cbor_item_ref(item);
	array = fu_cbor_item_new_array();
	if (!fu_cbor_item_array_append(array, item, error))
		return NULL;
	return g_steal_pointer(&array);
}

static GByteArray *
fu_fuzzer_coswid_build(gboolean many, gboolean directory, gboolean uuid, GError **error)
{
	const guint8 digest[32] = {0};
	const guint8 identifier[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	g_autoptr(FuCborItem) root = fu_cbor_item_new_map();
	g_autoptr(FuCborItem) entity = fu_cbor_item_new_map();
	g_autoptr(FuCborItem) entity_value = NULL;
	g_autoptr(FuCborItem) role = fu_cbor_item_new_integer(FU_COSWID_ENTITY_ROLE_TAG_CREATOR);
	g_autoptr(FuCborItem) role_value = NULL;
	g_autoptr(FuCborItem) link = fu_cbor_item_new_map();
	g_autoptr(FuCborItem) link_value = NULL;
	g_autoptr(FuCborItem) payload = fu_cbor_item_new_map();
	g_autoptr(FuCborItem) payload_value = NULL;
	g_autoptr(FuCborItem) file = fu_cbor_item_new_map();
	g_autoptr(FuCborItem) file_value = NULL;
	g_autoptr(FuCborItem) hash = fu_cbor_item_new_array();
	g_autoptr(FuCborItem) hash_value = NULL;
	g_autoptr(FuCborItem) algorithm = fu_cbor_item_new_integer(FU_COSWID_HASH_ALG_SHA256);
	g_autoptr(GBytes) digest_bytes = g_bytes_new_static(digest, sizeof(digest));
	g_autoptr(FuCborItem) digest_value = fu_cbor_item_new_bytes(digest_bytes);

	if (uuid)
		fu_coswid_write_tag_bytestring(root, FU_COSWID_TAG_TAG_ID, identifier, sizeof(identifier));
	else
		fu_coswid_write_tag_string(root, FU_COSWID_TAG_TAG_ID, "fwupd-corpus");
	fu_coswid_write_tag_string(root, FU_COSWID_TAG_SOFTWARE_NAME, "firmware");
	fu_coswid_write_tag_string(root, FU_COSWID_TAG_SOFTWARE_VERSION, "1.0.0");
	fu_coswid_write_tag_integer(root, FU_COSWID_TAG_VERSION_SCHEME, FU_COSWID_VERSION_SCHEME_SEMVER);
	fu_coswid_write_tag_string(root, FU_COSWID_TAG_LANG, "en-US");
	fu_coswid_write_tag_bool(root, FU_COSWID_TAG_CORPUS, TRUE);

	fu_coswid_write_tag_string(entity, FU_COSWID_TAG_ENTITY_NAME, "fwupd");
	fu_coswid_write_tag_string(entity, FU_COSWID_TAG_REG_ID, "fwupd.org");
	role_value = fu_fuzzer_coswid_one_or_many(role, many, error);
	if (role_value == NULL)
		return NULL;
	fu_coswid_write_tag_item(entity, FU_COSWID_TAG_ROLE, role_value);
	entity_value = fu_fuzzer_coswid_one_or_many(entity, many, error);
	if (entity_value == NULL)
		return NULL;
	fu_coswid_write_tag_item(root, FU_COSWID_TAG_ENTITY, entity_value);

	fu_coswid_write_tag_string(link, FU_COSWID_TAG_HREF, "https://fwupd.org");
	fu_coswid_write_tag_integer(link, FU_COSWID_TAG_REL, FU_COSWID_LINK_REL_LICENSE);
	link_value = fu_fuzzer_coswid_one_or_many(link, many, error);
	if (link_value == NULL)
		return NULL;
	fu_coswid_write_tag_item(root, FU_COSWID_TAG_LINK, link_value);

	if (!fu_cbor_item_array_append(hash, algorithm, error) ||
	    !fu_cbor_item_array_append(hash, digest_value, error))
		return NULL;
	hash_value = fu_fuzzer_coswid_one_or_many(hash, many, error);
	if (hash_value == NULL)
		return NULL;
	fu_coswid_write_tag_string(file, FU_COSWID_TAG_FS_NAME, "firmware.bin");
	fu_coswid_write_tag_integer(file, FU_COSWID_TAG_SIZE, 32);
	fu_coswid_write_tag_item(file, FU_COSWID_TAG_HASH, hash_value);
	file_value = fu_fuzzer_coswid_one_or_many(file, many, error);
	if (file_value == NULL)
		return NULL;
	if (directory) {
		g_autoptr(FuCborItem) directory_item = fu_cbor_item_new_map();
		g_autoptr(FuCborItem) elements = fu_cbor_item_new_map();
		fu_coswid_write_tag_item(elements, FU_COSWID_TAG_FILE, file_value);
		fu_coswid_write_tag_item(directory_item, FU_COSWID_TAG_PATH_ELEMENTS, elements);
		fu_coswid_write_tag_item(payload, FU_COSWID_TAG_DIRECTORY, directory_item);
	} else {
		fu_coswid_write_tag_item(payload, FU_COSWID_TAG_FILE, file_value);
	}
	payload_value = fu_fuzzer_coswid_one_or_many(payload, many, error);
	if (payload_value == NULL)
		return NULL;
	fu_coswid_write_tag_item(root, FU_COSWID_TAG_PAYLOAD, payload_value);
	return fu_cbor_item_write(root, error);
}

int
main(int argc, char **argv)
{
	gboolean many;
	gboolean directory;
	gboolean uuid;
	g_autoptr(GByteArray) blob = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GError) error = NULL;

	if (argc != 3 ||
	    (g_strcmp0(argv[1], "single") != 0 && g_strcmp0(argv[1], "arrays") != 0 &&
	     g_strcmp0(argv[1], "directory") != 0 && g_strcmp0(argv[1], "uuid") != 0)) {
		g_printerr("Expected VARIANT OUTPUT, where VARIANT is single, arrays, directory or uuid\n");
		return EXIT_FAILURE;
	}
	many = g_strcmp0(argv[1], "arrays") == 0;
	directory = g_strcmp0(argv[1], "directory") == 0;
	uuid = g_strcmp0(argv[1], "uuid") == 0;
	blob = fu_fuzzer_coswid_build(many, directory, uuid, &error);
	if (blob == NULL) {
		g_printerr("Failed to build corpus: %s\n", error->message);
		return EXIT_FAILURE;
	}
	firmware = fu_coswid_firmware_new();
	if (!fu_fuzzer_test_input(FU_FUZZER(firmware), blob, &error)) {
		g_printerr("Failed to validate corpus: %s\n", error->message);
		return EXIT_FAILURE;
	}
	if (!g_file_set_contents(argv[2], (const gchar *)blob->data, blob->len, &error)) {
		g_printerr("Failed to save corpus: %s\n", error->message);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}