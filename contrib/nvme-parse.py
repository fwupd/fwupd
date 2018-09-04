#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+

import csv
import binascii
import os
import struct
import glob
from collections import namedtuple

class Record(object):
    def __init__(self, filename, cns):
        self.filename = filename
        self.cns = cns

def load_pci_ids():
    pci_vendors = {}
    pci_vendors[0x1987] = 'Freescale'
    for ln in open('/usr/share/hwdata/pci.ids').read().split('\n'):
        if ln.startswith('#'):
            continue
        if ln.startswith('\t'):
            continue
        data = ln.split('  ')
        if len(data) != 2:
            continue
        pci_vendors[int(data[0], 16)] = data[1].split(' ')[0]
        if data[0] == 'ffff':
            break
    return pci_vendors

def _data_to_utf8(s):
    return s.decode('utf-8', 'replace').replace('\0', ' ')

def main():

    # open files
    records = []
    for fn in glob.glob('tests/nvme/*'):
        blob = open(fn, 'rb').read()
        if len(blob) != 4096:
            print('WARNING: ignoring %s of size %i' % (fn, len(blob)))
            continue
        Cns = namedtuple('Cns',
                         'vid ssvid sn mn fr rab ieee cmic mdts cntlid ver ' \
                         'rtd3r rtd3e oaes ctratt rrls rsvd102 oacs acl aerl ' \
                         'frmw lpa elpe npss avscc apsta wctemp cctemp mtfa ' \
                         'hmpre hmmin tnvmcap unvmcap rpmbs edstt dsto fwug ' \
                         'kas hctma mntmt mxtmt sanicap hmminds hmmaxd ' \
                         'nsetidmax rsvd340 anatt anacap anagrpmax nanagrpid ' \
                         'rsvd352 sqes cqes maxcmd nn oncs fuses fna vwc awun ' \
                         'awupf nvscc nwpc acwu rsvd534 sgls mnan rsvd544 ' \
                         'subnqn rsvd1024 ioccsz iorcsz icdoff ctrattr msdbd ' \
                         'rsvd1804 psd vs')
        try:
            cns = Cns._make(struct.unpack('<HH20s40s8sB3pBBHIIIIIH154pHBBBBBBBBHHHII16p' \
                                          '16pIHBBHHHHIIHH2pBBII160pBBHIHHBBHHBBH2pII' \
                                          '224p256p768pIIHBB244p1024p1024p', blob))
        except struct.error as e:
            print('WARNING: ignoring %s of size %i' % (fn, len(blob)))
            continue
        records.append(Record(fn, cns))

    # try to sort in sane way
    records = sorted(records,
                     key=lambda k: str(k.cns.vid) + k.cns.mn.decode('utf-8', 'replace') + k.cns.sn.decode('utf-8', 'replace'),
                     reverse=True)

    # export csv
    with open('all.csv', 'w', newline='') as csvfile:
        exp = csv.writer(csvfile)
        exp.writerow(['id', 'vid', 'sn', 'mn', 'fr',
                      'rrls', 'frmw', 'fwug', 'subnqn', 'vs'])
        for r in records:
            cns = r.cns
            sn = cns.sn.decode('utf-8', 'replace').replace('\0', ' ')
            mn = cns.mn.decode('utf-8', 'replace').replace('\0', ' ')
            fr = cns.fr.decode('utf-8', 'replace').replace('\0', ' ')
            exp.writerow([os.path.basename(r.filename)[:6],
                          '%04x' % cns.vid,
                          sn, mn, fr, cns.rrls,
                          '%02x' % cns.frmw,
                          cns.fwug, cns.subnqn,
                          binascii.hexlify(cns.vs)])

    # frmw stats
    s1ro_cnt = 0
    fawr_cnt = 0
    nfws_map = {}
    for r in records:
        if r.cns.frmw & 0x01 > 0:
            s1ro_cnt += 1
        if (r.cns.frmw & 0x10) >> 4:
            fawr_cnt += 1
        nfws = (r.cns.frmw & 0x0e) >> 1
        if nfws in nfws_map:
            nfws_map[nfws] += 1
            continue
        nfws_map[nfws] = 1
    print('s1ro=%i/%i' % (s1ro_cnt, len(records)))
    print('fawr=%i/%i' % (fawr_cnt, len(records)))
    nfws = sorted(nfws_map.items(), key=lambda k: k[0], reverse=True)
    for nfws, cnt in nfws:
        print('nfws[%i]=%i' % (nfws, cnt))

    # vendor popularity
    vids = {}
    for r in records:
        if r.cns.vid not in vids:
            vids[r.cns.vid] = 1
            continue
        vids[r.cns.vid] += 1
    vids = sorted(vids.items(), key=lambda k: k[1], reverse=True)
    pci_vendors = load_pci_ids()
    for vid, cnt in vids:
        name = '0x%04x' % vid
        if vid in pci_vendors:
            name = pci_vendors[vid]
        print('%s,%i' % (name, cnt))

    # vendor records
    vs_records = []
    for r in records:
        if r.cns.vs:
            vs_records.append(r)
    print('nr_vs=%i' % len(vs_records))

main()
