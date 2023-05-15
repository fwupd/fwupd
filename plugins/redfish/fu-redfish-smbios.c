/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-redfish-common.h"
#include "fu-redfish-smbios.h"
#include "fu-redfish-struct.h"

struct _FuRedfishSmbios {
	FuFirmwareClass parent_instance;
	guint16 port;
	gchar *hostname;
	gchar *mac_addr;
	gchar *ip_addr;
	guint16 vid;
	guint16 pid;
};

G_DEFINE_TYPE(FuRedfishSmbios, fu_redfish_smbios, FU_TYPE_FIRMWARE)

guint16
fu_redfish_smbios_get_port(FuRedfishSmbios *self)
{
	return self->port;
}

guint16
fu_redfish_smbios_get_vid(FuRedfishSmbios *self)
{
	return self->vid;
}

guint16
fu_redfish_smbios_get_pid(FuRedfishSmbios *self)
{
	return self->pid;
}

const gchar *
fu_redfish_smbios_get_hostname(FuRedfishSmbios *self)
{
	return self->hostname;
}

const gchar *
fu_redfish_smbios_get_mac_addr(FuRedfishSmbios *self)
{
	return self->mac_addr;
}

const gchar *
fu_redfish_smbios_get_ip_addr(FuRedfishSmbios *self)
{
	return self->ip_addr;
}

static void
fu_redfish_smbios_set_hostname(FuRedfishSmbios *self, const gchar *hostname)
{
	g_free(self->hostname);
	self->hostname = g_strdup(hostname);
}

static void
fu_redfish_smbios_set_mac_addr(FuRedfishSmbios *self, const gchar *mac_addr)
{
	g_free(self->mac_addr);
	self->mac_addr = g_strdup(mac_addr);
}

static void
fu_redfish_smbios_set_ip_addr(FuRedfishSmbios *self, const gchar *ip_addr)
{
	g_free(self->ip_addr);
	self->ip_addr = g_strdup(ip_addr);
}

static void
fu_redfish_smbios_set_port(FuRedfishSmbios *self, guint16 port)
{
	self->port = port;
}

static void
fu_redfish_smbios_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuRedfishSmbios *self = FU_REDFISH_SMBIOS(firmware);
	fu_xmlb_builder_insert_kx(bn, "port", self->port);
	fu_xmlb_builder_insert_kv(bn, "hostname", self->hostname);
	fu_xmlb_builder_insert_kv(bn, "mac_addr", self->mac_addr);
	fu_xmlb_builder_insert_kv(bn, "ip_addr", self->ip_addr);
	fu_xmlb_builder_insert_kx(bn, "vid", self->vid);
	fu_xmlb_builder_insert_kx(bn, "pid", self->pid);
}

static gboolean
fu_redfish_smbios_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuRedfishSmbios *self = FU_REDFISH_SMBIOS(firmware);
	const gchar *tmp;
	guint64 tmpu;

	/* optional properties */
	tmpu = xb_node_query_text_as_uint(n, "port", NULL);
	if (tmpu != G_MAXUINT64)
		fu_redfish_smbios_set_port(self, (guint16)tmpu);
	tmpu = xb_node_query_text_as_uint(n, "vid", NULL);
	if (tmpu != G_MAXUINT64)
		self->vid = (guint16)tmpu;
	tmpu = xb_node_query_text_as_uint(n, "pid", NULL);
	if (tmpu != G_MAXUINT64)
		self->pid = (guint16)tmpu;
	tmp = xb_node_query_text(n, "hostname", NULL);
	if (tmp != NULL)
		fu_redfish_smbios_set_hostname(self, tmp);
	tmp = xb_node_query_text(n, "mac_addr", NULL);
	if (tmp != NULL)
		fu_redfish_smbios_set_mac_addr(self, tmp);
	tmp = xb_node_query_text(n, "ip_addr", NULL);
	if (tmp != NULL)
		fu_redfish_smbios_set_ip_addr(self, tmp);

	/* success */
	return TRUE;
}

static gboolean
fu_redfish_smbios_parse_interface_data(FuRedfishSmbios *self,
				       GBytes *fw,
				       gsize offset,
				       GError **error)
{
	gsize bufsz = 0;
	gsize offset_mac_addr = G_MAXSIZE;
	gsize offset_vid_pid = G_MAXSIZE;
	guint8 interface_type = 0x0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* parse the data depending on the interface type */
	if (!fu_memread_uint8_safe(buf, bufsz, offset, &interface_type, error))
		return FALSE;
	offset++;
	switch (interface_type) {
	case REDFISH_INTERFACE_TYPE_USB_NETWORK:
	case REDFISH_INTERFACE_TYPE_PCI_NETWORK:
		offset_vid_pid = 0x00;
		break;
	case REDFISH_INTERFACE_TYPE_USB_NETWORK_V2:
		offset_vid_pid = 0x01;
		offset_mac_addr = 0x06;
		break;
	case REDFISH_INTERFACE_TYPE_PCI_NETWORK_V2:
		offset_vid_pid = 0x01;
		offset_mac_addr = 0x09;
		break;
	default:
		g_debug("unknown Network Interface");
		break;
	}

	/* MAC address */
	if (offset_mac_addr != G_MAXSIZE) {
		guint8 mac_addr[6] = {0x0};
		g_autofree gchar *mac_addr_str = NULL;
		if (!fu_memcpy_safe(mac_addr,
				    sizeof(mac_addr),
				    0x0, /* dst */
				    buf,
				    bufsz,
				    offset + offset_mac_addr, /* src */
				    sizeof(mac_addr),
				    error))
			return FALSE;
		mac_addr_str = fu_redfish_common_buffer_to_mac(mac_addr);
		fu_redfish_smbios_set_mac_addr(self, mac_addr_str);
	}

	/* VID:PID */
	if (offset_vid_pid != G_MAXSIZE) {
		if (!fu_memread_uint16_safe(buf,
					    bufsz,
					    offset + offset_vid_pid,
					    &self->vid,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (!fu_memread_uint16_safe(buf,
					    bufsz,
					    offset + offset_vid_pid + 0x02,
					    &self->pid,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_redfish_smbios_parse_over_ip(FuRedfishSmbios *self, GBytes *fw, gsize offset, GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	guint8 hostname_length;
	guint8 service_ip_address_format;
	g_autoptr(GByteArray) st = NULL;

	/* port + IP address */
	st = fu_struct_redfish_protocol_over_ip_parse(buf, bufsz, offset, error);
	if (st == NULL)
		return FALSE;
	fu_redfish_smbios_set_port(self,
				   fu_struct_redfish_protocol_over_ip_get_service_ip_port(st));
	service_ip_address_format =
	    fu_struct_redfish_protocol_over_ip_get_service_ip_address_format(st);
	if (service_ip_address_format == REDFISH_IP_ADDRESS_FORMAT_V4) {
		const guint8 *ip_address =
		    fu_struct_redfish_protocol_over_ip_get_service_ip_address(st, NULL);
		g_autofree gchar *tmp = fu_redfish_common_buffer_to_ipv4(ip_address);
		fu_redfish_smbios_set_ip_addr(self, tmp);
	} else if (service_ip_address_format == REDFISH_IP_ADDRESS_FORMAT_V6) {
		const guint8 *ip_address =
		    fu_struct_redfish_protocol_over_ip_get_service_ip_address(st, NULL);
		g_autofree gchar *tmp = fu_redfish_common_buffer_to_ipv6(ip_address);
		fu_redfish_smbios_set_ip_addr(self, tmp);
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "address format is invalid");
		return FALSE;
	}

	/* hostname */
	hostname_length = fu_struct_redfish_protocol_over_ip_get_service_hostname_len(st);
	if (hostname_length > 0) {
		g_autofree gchar *hostname = g_malloc0(hostname_length + 1);
		if (!fu_memcpy_safe((guint8 *)hostname,
				    hostname_length,
				    0x0, /* dst */
				    buf,
				    bufsz,
				    offset + st->len, /* src */
				    hostname_length,
				    error))
			return FALSE;
		fu_redfish_smbios_set_hostname(self, hostname);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_redfish_smbios_parse(FuFirmware *firmware,
			GBytes *fw,
			gsize offset,
			FwupdInstallFlags flags,
			GError **error)
{
	FuRedfishSmbios *self = FU_REDFISH_SMBIOS(firmware);
	gsize bufsz = 0;
	guint8 protocol_rcds = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* check size */
	if (bufsz < 0x09 + offset) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "SMBIOS entry too small: %" G_GSIZE_FORMAT,
			    bufsz);
		return FALSE;
	}

	/* check type */
	if (buf[offset + 0x0] != REDFISH_SMBIOS_TABLE_TYPE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "not Management Controller Host Interface");
		return FALSE;
	}
	if (buf[offset + 0x1] != bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "size of table 0x%x does not match binary 0x%x",
			    buf[0x1],
			    (guint)bufsz);
		return FALSE;
	}

	/* check interface type */
	if (buf[offset + 0x04] != REDFISH_CONTROLLER_INTERFACE_TYPE_NETWORK_HOST) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "only Network Host Interface supported");
		return FALSE;
	}

	/* check length */
	if (buf[offset + 0x05] > 0) {
		if (!fu_redfish_smbios_parse_interface_data(self, fw, 0x06, error))
			return FALSE;
	}

	/* parse protocol records */
	if (!fu_memread_uint8_safe(buf, bufsz, offset + 0x06 + buf[0x05], &protocol_rcds, error))
		return FALSE;
	offset += 0x07 + buf[0x05];
	g_debug("protocol_rcds: %u", protocol_rcds);
	for (guint i = 0; i < protocol_rcds; i++) {
		guint8 protocol_id = 0;
		guint8 protocol_sz = 0;
		if (!fu_memread_uint8_safe(buf, bufsz, offset, &protocol_id, error))
			return FALSE;
		if (!fu_memread_uint8_safe(buf, bufsz, offset + 0x1, &protocol_sz, error))
			return FALSE;
		if (protocol_id == REDFISH_PROTOCOL_REDFISH_OVER_IP) {
			if (!fu_redfish_smbios_parse_over_ip(self, fw, offset + 0x2, error))
				return FALSE;
		} else {
			g_debug("ignoring protocol ID 0x%02x", protocol_id);
		}
		offset += protocol_sz + 1;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_redfish_smbios_write(FuFirmware *firmware, GError **error)
{
	FuRedfishSmbios *self = FU_REDFISH_SMBIOS(firmware);
	gsize hostname_sz = 0;
	g_autoptr(GByteArray) st = fu_struct_redfish_protocol_over_ip_new();
	g_autoptr(GByteArray) buf = g_byte_array_new();

	if (self->hostname != NULL)
		hostname_sz = strlen(self->hostname);
	fu_byte_array_append_uint8(buf, REDFISH_SMBIOS_TABLE_TYPE);
	fu_byte_array_append_uint8(buf, 0x6D + hostname_sz);	   /* length */
	fu_byte_array_append_uint16(buf, 0x1234, G_LITTLE_ENDIAN); /* handle */
	fu_byte_array_append_uint8(buf, REDFISH_CONTROLLER_INTERFACE_TYPE_NETWORK_HOST);
	fu_byte_array_append_uint8(buf, 0x09);				     /* iface datalen */
	fu_byte_array_append_uint8(buf, REDFISH_INTERFACE_TYPE_USB_NETWORK); /* iface */
	fu_byte_array_append_uint16(buf, self->vid, G_LITTLE_ENDIAN);	     /* iface:VID */
	fu_byte_array_append_uint16(buf, self->pid, G_LITTLE_ENDIAN);	     /* iface:PID */
	fu_byte_array_append_uint8(buf, 0x02);				     /* iface:serialsz */
	fu_byte_array_append_uint8(buf, 0x03);				     /* iType */
	fu_byte_array_append_uint8(buf, 'S');				     /* iface:serial */
	fu_byte_array_append_uint8(buf, 'n');				     /* iface:serial */
	fu_byte_array_append_uint8(buf, 0x1);				     /* nr protocol rcds */

	/* protocol record */
	fu_byte_array_append_uint8(buf, REDFISH_PROTOCOL_REDFISH_OVER_IP);
	fu_byte_array_append_uint8(buf, st->len + hostname_sz);

	if (self->hostname != NULL)
		hostname_sz = strlen(self->hostname);
	fu_struct_redfish_protocol_over_ip_set_service_ip_port(st, self->port);
	fu_struct_redfish_protocol_over_ip_set_service_ip_address_format(
	    st,
	    REDFISH_IP_ADDRESS_FORMAT_V4);
	fu_struct_redfish_protocol_over_ip_set_service_ip_assignment_type(
	    st,
	    REDFISH_IP_ASSIGNMENT_TYPE_STATIC);
	fu_struct_redfish_protocol_over_ip_set_service_hostname_len(st, hostname_sz);
	g_byte_array_append(buf, st->data, st->len);
	if (hostname_sz > 0)
		g_byte_array_append(buf, (guint8 *)self->hostname, hostname_sz);
	return g_steal_pointer(&buf);
}

static void
fu_redfish_smbios_finalize(GObject *object)
{
	FuRedfishSmbios *self = FU_REDFISH_SMBIOS(object);
	g_free(self->hostname);
	g_free(self->mac_addr);
	g_free(self->ip_addr);
	G_OBJECT_CLASS(fu_redfish_smbios_parent_class)->finalize(object);
}

static void
fu_redfish_smbios_init(FuRedfishSmbios *self)
{
}

static void
fu_redfish_smbios_class_init(FuRedfishSmbiosClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_redfish_smbios_finalize;
	klass_firmware->parse = fu_redfish_smbios_parse;
	klass_firmware->write = fu_redfish_smbios_write;
	klass_firmware->build = fu_redfish_smbios_build;
	klass_firmware->export = fu_redfish_smbios_export;
}

FuRedfishSmbios *
fu_redfish_smbios_new(void)
{
	return FU_REDFISH_SMBIOS(g_object_new(FU_TYPE_REDFISH_SMBIOS, NULL));
}
