/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <string.h>

#include "fwupd-error.h"
#include "fwupd-enums.h"

#include "fu-device.h"

#include "fu-redfish-client.h"
#include "fu-redfish-common.h"

struct _FuRedfishClient
{
	GObject			 parent_instance;
	SoupSession		*session;
	gchar			*hostname;
	guint			 port;
	gchar			*username;
	gchar			*password;
	gchar			*update_uri_path;
	gchar			*push_uri_path;
	gboolean		 auth_created;
	gboolean		 use_https;
	gboolean		 cacheck;
	GPtrArray		*devices;
};

G_DEFINE_TYPE (FuRedfishClient, fu_redfish_client, G_TYPE_OBJECT)

static void
fu_redfish_client_set_auth (FuRedfishClient *self, SoupURI *uri,
			    SoupMessage *msg)
{
	if ((self->username != NULL && self->password != NULL) &&
	    self->auth_created == FALSE) {
		/*
		 * Some redfish implementations miss WWW-Authenticate
		 * header for a 401 response, and SoupAuthManager couldn't
		 * generate SoupAuth accordingly. Since DSP0266 makes
		 * Basic Authorization a requirement for redfish, it shall be
		 * safe to use Basic Auth for all redfish implementations.
		 */
		SoupAuthManager *manager = SOUP_AUTH_MANAGER (soup_session_get_feature (self->session, SOUP_TYPE_AUTH_MANAGER));
		g_autoptr(SoupAuth) auth = soup_auth_new (SOUP_TYPE_AUTH_BASIC,
							  msg, "Basic");
		soup_auth_authenticate (auth, self->username, self->password);
		soup_auth_manager_use_auth (manager, uri, auth);
		self->auth_created = TRUE;
	}
}

static GBytes *
fu_redfish_client_fetch_data (FuRedfishClient *self, const gchar *uri_path, GError **error)
{
	guint status_code;
	g_autoptr(SoupMessage) msg = NULL;
	g_autoptr(SoupURI) uri = NULL;

	/* create URI */
	uri = soup_uri_new (NULL);
	soup_uri_set_scheme (uri, self->use_https ? "https" : "http");
	soup_uri_set_path (uri, uri_path);
	soup_uri_set_host (uri, self->hostname);
	soup_uri_set_port (uri, self->port);
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, uri);
	if (msg == NULL) {
		g_autofree gchar *tmp = soup_uri_to_string (uri, FALSE);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to create message for URI %s", tmp);
		return NULL;
	}
	fu_redfish_client_set_auth (self, uri, msg);
	status_code = soup_session_send_message (self->session, msg);
	if (status_code != SOUP_STATUS_OK) {
		g_autofree gchar *tmp = soup_uri_to_string (uri, FALSE);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to download %s: %s",
			     tmp, soup_status_get_phrase (status_code));
		return NULL;
	}
	return g_bytes_new (msg->response_body->data, msg->response_body->length);
}

static gboolean
fu_redfish_client_coldplug_member (FuRedfishClient *self,
				   JsonObject *member,
				   GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	const gchar *guid = NULL;
	g_autofree gchar *id = NULL;

	if (json_object_has_member (member, "SoftwareId")) {
		guid = json_object_get_string_member (member, "SoftwareId");
	} else if (json_object_has_member (member, "Oem")) {
		JsonObject *oem = json_object_get_object_member (member, "Oem");
		if (oem != NULL && json_object_has_member (oem, "Hpe")) {
			JsonObject *hpe = json_object_get_object_member (oem, "Hpe");
			if (hpe != NULL && json_object_has_member (hpe, "DeviceClass"))
				guid = json_object_get_string_member (hpe, "DeviceClass");
		}
	}

	/* skip the devices without guid */
	if (guid == NULL)
		return TRUE;

	dev = fu_device_new ();

	id = g_strdup_printf ("Redfish-Inventory-%s",
			      json_object_get_string_member (member, "Id"));
	fu_device_set_id (dev, id);
	fu_device_set_protocol (dev, "org.dmtf.redfish");

	fu_device_add_guid (dev, guid);
	if (json_object_has_member (member, "Name"))
		fu_device_set_name (dev, json_object_get_string_member (member, "Name"));
	fu_device_set_summary (dev, "Redfish device");
	if (json_object_has_member (member, "Version")) {
		fu_device_set_version (dev, json_object_get_string_member (member, "Version"),
				       FWUPD_VERSION_FORMAT_UNKNOWN);
	}
	if (json_object_has_member (member, "LowestSupportedVersion"))
		fu_device_set_version_lowest (dev, json_object_get_string_member (member, "LowestSupportedVersion"));
	if (json_object_has_member (member, "Description"))
		fu_device_set_description (dev, json_object_get_string_member (member, "Description"));
	if (json_object_has_member (member, "Updateable")) {
		if (json_object_get_boolean_member (member, "Updateable"))
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	} else {
		/* assume the device is updatable */
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	}

	/* success */
	g_ptr_array_add (self->devices, g_steal_pointer (&dev));
	return TRUE;
}

static gboolean
fu_redfish_client_coldplug_collection (FuRedfishClient *self,
				       JsonObject *collection,
				       GError **error)
{
	JsonArray *members;
	JsonNode *node_root;
	JsonObject *member;

	members = json_object_get_array_member (collection, "Members");
	for (guint i = 0; i < json_array_get_length (members); i++) {
		g_autoptr(JsonParser) parser = json_parser_new ();
		g_autoptr(GBytes) blob = NULL;
		JsonObject *member_id;
		const gchar *member_uri;

		member_id = json_array_get_object_element (members, i);
		member_uri = json_object_get_string_member (member_id, "@odata.id");
		if (member_uri == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_FOUND,
					     "no @odata.id string");
			return FALSE;
		}

		/* try to connect */
		blob = fu_redfish_client_fetch_data (self, member_uri, error);
		if (blob == NULL)
			return FALSE;

		/* get the member object */
		if (!json_parser_load_from_data (parser,
						 g_bytes_get_data (blob, NULL),
						 (gssize) g_bytes_get_size (blob),
						 error)) {
			g_prefix_error (error, "failed to parse node: ");
			return FALSE;
		}
		node_root = json_parser_get_root (parser);
		if (node_root == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "no root node");
			return FALSE;
		}
		member = json_node_get_object (node_root);
		if (member == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "no member object");
			return FALSE;
		}

		/* Create the device for the member */
		if (!fu_redfish_client_coldplug_member (self, member, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_redfish_client_coldplug_inventory (FuRedfishClient *self,
				      JsonObject *inventory,
				      GError **error)
{
	g_autoptr(JsonParser) parser = json_parser_new ();
	g_autoptr(GBytes) blob = NULL;
	JsonNode *node_root;
	JsonObject *collection;
	const gchar *collection_uri;

	if (inventory == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no inventory object");
		return FALSE;
	}

	collection_uri = json_object_get_string_member (inventory, "@odata.id");
	if (collection_uri == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "no @odata.id string");
		return FALSE;
	}

	/* try to connect */
	blob = fu_redfish_client_fetch_data (self, collection_uri, error);
	if (blob == NULL)
		return FALSE;

	/* get the inventory object */
	if (!json_parser_load_from_data (parser,
					 g_bytes_get_data (blob, NULL),
					 (gssize) g_bytes_get_size (blob),
					 error)) {
		g_prefix_error (error, "failed to parse node: ");
		return FALSE;
	}
	node_root = json_parser_get_root (parser);
	if (node_root == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no root node");
		return FALSE;
	}
	collection = json_node_get_object (node_root);
	if (collection == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no collection object");
		return FALSE;
	}

	return fu_redfish_client_coldplug_collection (self, collection, error);
}

gboolean
fu_redfish_client_coldplug (FuRedfishClient *self, GError **error)
{
	JsonNode *node_root;
	JsonObject *obj_root = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(JsonParser) parser = json_parser_new ();

	/* nothing set */
	if (self->update_uri_path == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no update_uri_path");
		return FALSE;
	}

	/* try to connect */
	blob = fu_redfish_client_fetch_data (self, self->update_uri_path, error);
	if (blob == NULL)
		return FALSE;

	/* get the update service */
	if (!json_parser_load_from_data (parser,
					 g_bytes_get_data (blob, NULL),
					 (gssize) g_bytes_get_size (blob),
					 error)) {
		g_prefix_error (error, "failed to parse node: ");
		return FALSE;
	}
	node_root = json_parser_get_root (parser);
	if (node_root == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no root node");
		return FALSE;
	}
	obj_root = json_node_get_object (node_root);
	if (obj_root == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no root object");
		return FALSE;
	}
	if (!json_object_get_boolean_member (obj_root, "ServiceEnabled")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "service is not enabled");
		return FALSE;
	}
	if (!json_object_has_member (obj_root, "HttpPushUri")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "HttpPushUri is not available");
		return FALSE;
	}
	self->push_uri_path = g_strdup (json_object_get_string_member (obj_root, "HttpPushUri"));
	if (self->push_uri_path == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "HttpPushUri is invalid");
		return FALSE;
	}
	if (json_object_has_member (obj_root, "FirmwareInventory")) {
		JsonObject *tmp = json_object_get_object_member (obj_root, "FirmwareInventory");
		return fu_redfish_client_coldplug_inventory (self, tmp, error);
	}
	if (json_object_has_member (obj_root, "SoftwareInventory")) {
		JsonObject *tmp = json_object_get_object_member (obj_root, "SoftwareInventory");
		return fu_redfish_client_coldplug_inventory (self, tmp, error);
	}
	return TRUE;
}

static gboolean
fu_redfish_client_set_uefi_credentials (FuRedfishClient *self, GError **error)
{
	guint32 indications_le;
	g_autofree gchar *userpass_safe = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(GBytes) indications = NULL;
	g_autoptr(GBytes) userpass = NULL;

	/* get the uint32 specifying if there are EFI variables set */
	indications = fu_redfish_common_get_evivar_raw (REDFISH_EFI_INFORMATION_GUID,
						     REDFISH_EFI_INFORMATION_INDICATIONS,
						     error);
	if (indications == NULL)
		return FALSE;
	if (g_bytes_get_size (indications) != 4) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid value for %s, got %" G_GSIZE_FORMAT " bytes",
			     REDFISH_EFI_INFORMATION_INDICATIONS,
			     g_bytes_get_size (indications));
		return FALSE;
	}
	memcpy (&indications_le, g_bytes_get_data (indications, NULL), 4);
	if ((indications_le & REDFISH_EFI_INDICATIONS_OS_CREDENTIALS) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no indications for OS credentials");
		return FALSE;
	}

	/* read the correct EFI var for runtime */
	userpass = fu_redfish_common_get_evivar_raw (REDFISH_EFI_INFORMATION_GUID,
						  REDFISH_EFI_INFORMATION_OS_CREDENTIALS,
						  error);
	if (userpass == NULL)
		return FALSE;

	/* it might not be NUL terminated */
	userpass_safe = g_strndup (g_bytes_get_data (userpass, NULL),
				   g_bytes_get_size (userpass));
	split = g_strsplit (userpass_safe, ":", -1);
	if (g_strv_length (split) != 2) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "invalid format for username:password, got '%s'",
			     userpass_safe);
		return FALSE;
	}
	fu_redfish_client_set_username (self, split[0]);
	fu_redfish_client_set_password (self, split[1]);
	return TRUE;
}

static void
fu_redfish_client_parse_interface_data (const guint8 *buf, guint8 sz)
{
	switch (buf[0]) {
	case REDFISH_INTERFACE_TYPE_USB_NEWORK:
		g_debug ("USB Network Interface");
		/*
		 * uint16	idVendor(2-bytes)
		 * uint16	idProduct(2-bytes)
		 * uint8	SerialNumberLen:
		 * uint8	DescriptorType:
		 * uint8*	SerialNumber:
		 */
		break;
	case REDFISH_INTERFACE_TYPE_PCI_NEWORK:
		g_debug ("PCI Network Interface");
		/*
		 * uint16	VendorID
		 * uint16	DeviceID
		 * uint16	Subsystem_Vendor_ID
		 * uint16	Subsystem_ID
		 */
		break;
	default:
		break;
	}
}

typedef struct __attribute__((packed)) {
	guint8		 service_uuid[16];
	guint8		 host_ip_assignment_type;
	guint8		 host_ip_address_format;
	guint8		 host_ip_address[16];
	guint8		 host_ip_mask[16];
	guint8		 service_ip_assignment_type;
	guint8		 service_ip_address_format;
	guint8		 service_ip_address[16];
	guint8		 service_ip_mask[16];
	guint16		 service_ip_port;
	guint32		 service_ip_vlan_id;
	guint8		 service_hostname_len;
	/*		 service_hostname; */
} RedfishProtocolDataOverIp;

static gboolean
fu_redfish_client_parse_protocol_data (FuRedfishClient *self,
				       const guint8 *buf,
				       guint8 sz,
				       GError **error)
{
	RedfishProtocolDataOverIp *pr;
	if (sz < sizeof(RedfishProtocolDataOverIp)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "protocol data too small");
		return FALSE;
	}
	pr = (RedfishProtocolDataOverIp *) buf;

	/* parse the hostname and port */
	if (pr->service_ip_assignment_type == REDFISH_IP_ASSIGNMENT_TYPE_STATIC ||
	    pr->service_ip_assignment_type == REDFISH_IP_ASSIGNMENT_TYPE_AUTO_CONFIG) {
		if (pr->service_ip_address_format == REDFISH_IP_ADDRESS_FORMAT_V4) {
			g_autofree gchar *tmp = NULL;
			tmp = fu_redfish_common_buffer_to_ipv4 (pr->service_ip_address);
			fu_redfish_client_set_hostname (self, tmp);
		} else if (pr->service_ip_address_format == REDFISH_IP_ADDRESS_FORMAT_V6) {
			g_autofree gchar *tmp = NULL;
			tmp = fu_redfish_common_buffer_to_ipv6 (pr->service_ip_address);
			fu_redfish_client_set_hostname (self, tmp);
		} else {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "address format is invalid");
			return FALSE;
		}
		fu_redfish_client_set_port (self, pr->service_ip_port);
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "DHCP address formats not supported (%0x2)",
			     pr->service_ip_assignment_type);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_redfish_client_set_smbios_interfaces (FuRedfishClient *self,
					 GBytes *smbios_table,
					 GError **error)
{
	const guint8 *buf;
	gsize sz = 0;

	/* check size */
	buf = g_bytes_get_data (smbios_table, &sz);
	if (sz < 0x09) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "SMBIOS entry too small: %" G_GSIZE_FORMAT,
			     sz);
		return FALSE;
	}

	/* check interface type */
	if (buf[0x04] != REDFISH_CONTROLLER_INTERFACE_TYPE_NETWORK_HOST) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "only Network Host Interface supported");
		return FALSE;
	}

	/* check length */
	if (buf[0x05] > sz - 0x08) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "interface specific data too large %u > %" G_GSIZE_FORMAT,
			     buf[0x05], sz - 0x08);
		return FALSE;
	}

	/* parse data, for not just for debugging */
	if (buf[0x05] > 0)
		fu_redfish_client_parse_interface_data (&buf[0x06], buf[0x05]);

	/* parse protocol records */
	for (guint8 i = 0x07 + buf[0x05]; i < sz - 1; i++) {
		guint8 protocol_id = buf[i];
		guint8 protocol_sz = buf[i+1];
		if (protocol_sz > sz - buf[0x05] + 0x07) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "protocol length too large");
			return FALSE;
		}
		if (protocol_id == REDFISH_PROTOCOL_REDFISH_OVER_IP) {
			if (!fu_redfish_client_parse_protocol_data (self,
								    &buf[i+2],
								    protocol_sz,
								    error))
				return FALSE;
		} else {
			g_debug ("ignoring unsupported protocol ID %02x",
				 protocol_id);
		}
		i += protocol_sz - 1;
	}

	return TRUE;
}

gboolean
fu_redfish_client_update (FuRedfishClient *self, FuDevice *device, GBytes *blob_fw,
			  GError **error)
{
	FwupdRelease *release;
	g_autofree gchar *filename = NULL;

	guint status_code;
	g_autoptr(SoupMessage) msg = NULL;
	g_autoptr(SoupURI) uri = NULL;
	g_autoptr(SoupMultipart) multipart = NULL;
	g_autoptr(SoupBuffer) buffer = NULL;
	g_autofree gchar *uri_str = NULL;

	/* Get the update version */
	release = fwupd_device_get_release_default (FWUPD_DEVICE (device));
	if (release != NULL) {
		filename = g_strdup_printf ("%s-%s.bin",
					    fu_device_get_name (device),
					    fwupd_release_get_version (release));
	} else {
		filename = g_strdup_printf ("%s.bin",
					    fu_device_get_name (device));
	}

	/* create URI */
	uri = soup_uri_new (NULL);
	soup_uri_set_scheme (uri, self->use_https ? "https" : "http");
	soup_uri_set_path (uri, self->push_uri_path);
	soup_uri_set_host (uri, self->hostname);
	soup_uri_set_port (uri, self->port);
	uri_str = soup_uri_to_string (uri, FALSE);

	/* Create the multipart request */
	multipart = soup_multipart_new (SOUP_FORM_MIME_TYPE_MULTIPART);
	buffer = soup_buffer_new (SOUP_MEMORY_COPY,
				  g_bytes_get_data (blob_fw, NULL),
				  g_bytes_get_size (blob_fw));
	soup_multipart_append_form_file (multipart, filename, filename,
					 "application/octet-stream",
					 buffer);
	msg = soup_form_request_new_from_multipart (uri_str, multipart);
	if (msg == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to create message for URI %s", uri_str);
		return FALSE;
	}
	fu_redfish_client_set_auth (self, uri, msg);
	status_code = soup_session_send_message (self->session, msg);
	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "failed to upload %s to %s: %s",
			     filename, uri_str,
			     soup_status_get_phrase (status_code));
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_redfish_client_setup (FuRedfishClient *self, GBytes *smbios_table, GError **error)
{
	JsonNode *node_root;
	JsonObject *obj_root = NULL;
	JsonObject *obj_update_service = NULL;
	const gchar *data_id;
	const gchar *version = NULL;
	g_autofree gchar *user_agent = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(JsonParser) parser = json_parser_new ();

	/* sanity check */
	if (self->port == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no port specified");
		return FALSE;
	}
	if (self->port > 0xffff) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "invalid port specified: 0x%x",
			     self->port);
		return FALSE;
	}

	/* create the soup session */
	user_agent = g_strdup_printf ("%s/%s", PACKAGE_NAME, PACKAGE_VERSION);
	self->session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, user_agent,
						       SOUP_SESSION_TIMEOUT, 60,
						       NULL);
	if (self->session == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to setup networking");
		return FALSE;
	}

	if (self->cacheck == FALSE) {
		g_object_set (G_OBJECT (self->session),
			      SOUP_SESSION_SSL_STRICT, FALSE,
			      NULL);
	}

	/* this is optional */
	if (smbios_table != NULL) {
		g_autoptr(GError) error_smbios = NULL;
		g_autoptr(GError) error_uefi = NULL;
		if (!fu_redfish_client_set_smbios_interfaces (self,
							   smbios_table,
							   &error_smbios)) {
			g_debug ("failed to get connection URI automatically: %s",
				 error_smbios->message);
		}
		if (!fu_redfish_client_set_uefi_credentials (self, &error_uefi)) {
			g_debug ("failed to get username and password automatically: %s",
				 error_uefi->message);
		}
	}
	if (self->hostname != NULL)
		g_debug ("Hostname: %s", self->hostname);
	if (self->port != 0)
		g_debug ("Port:     %u", self->port);
	if (self->username != NULL)
		g_debug ("Username: %s", self->username);
	if (self->password != NULL)
		g_debug ("Password: %s", self->password);

	/* try to connect */
	blob = fu_redfish_client_fetch_data (self, "/redfish/v1/", error);
	if (blob == NULL)
		return FALSE;

	/* get the update service */
	if (!json_parser_load_from_data (parser,
					 g_bytes_get_data (blob, NULL),
					 (gssize) g_bytes_get_size (blob),
					 error)) {
		g_prefix_error (error, "failed to parse node: ");
		return FALSE;
	}
	node_root = json_parser_get_root (parser);
	if (node_root == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no root node");
		return FALSE;
	}
	obj_root = json_node_get_object (node_root);
	if (obj_root == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no root object");
		return FALSE;
	}
	if (json_object_has_member (obj_root, "ServiceVersion")) {
		version = json_object_get_string_member (obj_root,
							 "ServiceVersion");
	} else if (json_object_has_member (obj_root, "RedfishVersion")) {
		version = json_object_get_string_member (obj_root,
							 "RedfishVersion");
	}
	g_debug ("Version:  %s", version);
	g_debug ("UUID:     %s",
		 json_object_get_string_member (obj_root, "UUID"));

	if (json_object_has_member (obj_root, "UpdateService"))
		obj_update_service = json_object_get_object_member (obj_root, "UpdateService");
	if (obj_update_service == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no UpdateService object");
		return FALSE;
	}
	data_id = json_object_get_string_member (obj_update_service, "@odata.id");
	if (data_id == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no @odata.id string");
		return FALSE;
	}
	self->update_uri_path = g_strdup (data_id);
	return TRUE;
}

GPtrArray *
fu_redfish_client_get_devices (FuRedfishClient *self)
{
	return self->devices;
}

void
fu_redfish_client_set_hostname (FuRedfishClient *self, const gchar *hostname)
{
	g_free (self->hostname);
	self->hostname = g_strdup (hostname);
}

void
fu_redfish_client_set_port (FuRedfishClient *self, guint port)
{
	self->port = port;
}

void
fu_redfish_client_set_https (FuRedfishClient *self, gboolean use_https)
{
	self->use_https = use_https;
}

void
fu_redfish_client_set_cacheck (FuRedfishClient *self, gboolean cacheck)
{
	self->cacheck = cacheck;
}

void
fu_redfish_client_set_username (FuRedfishClient *self, const gchar *username)
{
	g_free (self->username);
	self->username = g_strdup (username);
}

void
fu_redfish_client_set_password (FuRedfishClient *self, const gchar *password)
{
	g_free (self->password);
	self->password = g_strdup (password);
}

static void
fu_redfish_client_finalize (GObject *object)
{
	FuRedfishClient *self = FU_REDFISH_CLIENT (object);
	if (self->session != NULL)
		g_object_unref (self->session);
	g_free (self->update_uri_path);
	g_free (self->push_uri_path);
	g_free (self->hostname);
	g_free (self->username);
	g_free (self->password);
	g_ptr_array_unref (self->devices);
	G_OBJECT_CLASS (fu_redfish_client_parent_class)->finalize (object);
}

static void
fu_redfish_client_class_init (FuRedfishClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_redfish_client_finalize;
}

static void
fu_redfish_client_init (FuRedfishClient *self)
{
	self->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

FuRedfishClient *
fu_redfish_client_new (void)
{
	FuRedfishClient *self;
	self = g_object_new (REDFISH_TYPE_CLIENT, NULL);
	return FU_REDFISH_CLIENT (self);
}

/* vim: set noexpandtab: */
