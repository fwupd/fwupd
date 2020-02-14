#!/usr/bin/python3
#
# Copyright (C) 20202 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import os
import sys
import gzip
import xml.etree.ElementTree as etree

def get_kind_from_component(md):

    # ideally we would use <category> but that's not available in the XML yet
    name = md.find('name').text
    if name.find('Embedded Controller') != -1:
        return 'Embedded Controller'
    if name.find('Corporate ME') != -1:
        return 'Intel Management Engine'
    if name.find('Consumer ME') != -1:
        return 'Intel Management Engine'
    return None

def generate_quirk_from_xml(infn, outfn):

    # parse GZipped XML
    with gzip.open(infn, 'rb') as xml:
        tree = etree.parse(xml)

    # look for any interesting devices
    seen_guids = {}
    with open(outfn, 'w') as out:
        out.write('# generated using {} -- DO NOT EDIT\n'.format(os.path.basename(sys.argv[0])))
        for md in tree.findall('component'):

            # filter by protocol
            protocol = None
            for val in md.findall('custom/value'):
                if val.attrib['key'] == 'LVFS::UpdateProtocol':
                    protocol = val.text
            if protocol != 'org.uefi.capsule':
                continue

            # it is unlikely, but there might be multiple GUIDs here
            guids = []
            for prov in md.findall('provides/firmware'):
                if prov.text in seen_guids:
                    continue
                guids.append(prov.text)
            if not guids:
                continue

            # map to a suitable name for fwupd
            kind = get_kind_from_component(md)
            if not kind:
                continue

            # output another chunk
            appstream_id = md.find('id').text
            out.write('\n# from {}\n'.format(appstream_id))
            for guid in guids:
                out.write('[Guid={}]\n'.format(guid))
                out.write('Name = {}\n'.format(kind))
                seen_guids[guid] = True

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('invalid args, expected INPUT OUTPUT')
        sys.exit(1)
    generate_quirk_from_xml(sys.argv[1], sys.argv[2])
