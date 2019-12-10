Redfish Support
===============

Introduction
------------

Redfish is an open industry standard specification and schema that helps enable
simple and secure management of modern scalable platform hardware.

By specifying a RESTful interface and utilizing JSON and OData, Redfish helps
customers integrate solutions within their existing tool chains.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

 * org.dmtf.redfish

GUID Generation
---------------

These devices use the provided GUID provided in the `SoftwareId` parameter
without modification. Devices without GUIDs are not supported.

Vendor ID Security
------------------

No vendor ID is set as there is no vendor field in the schema.

Setting Service IP Manually
---------------------------

The service IP may not be automatically discoverable due to the absence of
Type 0x42 entry in SMBIOS. In this case, you have to specify the service IP
to RedfishUri in /etc/fwupd/redfish.conf

Take HPE Gen10 for example, the service IP can be found with the following
command:

    # ilorest --nologo list --selector=EthernetInterface. -j

This command lists all network interfaces, and the Redfish service IP belongs
to one of "Manager Network" Interfaces. For example:

    {
      "@odata.context": "/redfish/v1/$metadata#EthernetInterface.EthernetInterface",
      "@odata.id": "/redfish/v1/Managers/1/EthernetInterfaces/1/",
      "@odata.type": "#EthernetInterface.v1_0_3.EthernetInterface",
      "Description": "Configuration of this Manager Network Interface",
      "HostName": "myredfish",
      "IPv4Addresses": [
        {
          "SubnetMask": "255.255.255.0",
          "AddressOrigin": "DHCP",
          "Gateway": "192.168.0.1",
          "Address": "192.168.0.133"
        }
      ],
      ...

In this example, the service IP is "192.168.0.133".

Since the conventional HTTP port is 80 and HTTPS port is 443, we can set
RedfishUri to either "http://192.168.0.133:80" or "https://192.168.0.133:443"
and verify the uri with

    $ curl http://192.168.0.133:80/redfish/v1/

or

    $ curl -k https://192.168.0.133:443/redfish/v1/
