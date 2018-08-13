#!/usr/bin/python3
""" This parses avrdude.conf and generates quirks for fwupd """

# pylint: disable=wrong-import-position,pointless-string-statement

"""
SPDX-License-Identifier: LGPL-2.1+
"""

import sys
from difflib import SequenceMatcher

# finds a part using the ID
def _find_part_by_id(parts, part_id):
    for part in parts:
        if 'id' not in part:
            continue
        if part['id'] == part_id:
            return part
    return None

# finds a memory layout for a part, climbing up the tree to the parent if reqd.
def _find_mem_layout(parts, part):
    if 'memory-application' in part:
        memory_flash = part['memory-application']
        if memory_flash:
            return memory_flash

    #look at the parent
    if 'parent' in part:
        parent = _find_part_by_id(parts, part['parent'])
        if parent:
            return _find_mem_layout(parts, parent)
        print('no parent ', part['parent'], 'found for', part['id'])
    return None

# parses the weird syntax of avrdude.conf and makes lots of nested dictionaries
def _parse_parts(fn_source):
    print("reading", fn_source)

    part = None
    memory_id = None
    parts = []

    for line in open(fn_source).readlines():

        # try to clean up crazy syntax
        line = line.replace('\n', '')
        if line.endswith(';'):
            line = line[:-1]

        # ignore blank lines
        line = line.rstrip()
        if not line:
            continue

        # count how many spaces deep this is
        lvl = 0
        for char in line:
            if char != ' ':
                break
            lvl = lvl + 1

        # ignore comments
        line = line.strip()
        if line[0] == '#':
            continue

        # level 0 of hell
        if lvl == 0:
            if line.startswith('part'):
                memory_id = None
                part = {}
                parts.append(part)
                if line.startswith('part parent '):
                    part['parent'] = line[13:].replace('"', '')
            continue

        # level 4 of hell
        if lvl == 4:
            if line.startswith('memory'):
                memory_id = 'memory-' + line[7:].replace('"', '')
                part[memory_id] = {}
                continue
            split = line.split('=')
            if len(split) != 2:
                print('ignoring', line)
                continue
            part[split[0].strip()] = split[1].strip().replace('"', '')
            continue

        # level 8 of hell
        if lvl == 8:
            if memory_id:
                split = line.split('=')
                if len(split) != 2:
                    continue
                memory = part[memory_id]
                memory[split[0].strip()] = split[1].strip()
            continue
    return parts

def _get_longest_substring(s1, s2):
    match = SequenceMatcher(None, s1, s2).find_longest_match(0, len(s1), 0, len(s2))
    return s2[match.b: match.b + match.size]

# writes important data to the quirks file
def _write_quirks(parts, fn_destination):
    outp = []

    results = {}

    for part in parts:

        # ignore meta parts with deprecated names
        if 'desc' not in part:
            continue
        if 'signature' not in part:
            continue

        # find the layout
        mem_part = _find_mem_layout(parts, part)
        if not mem_part:
            print("no memory layout for", part['desc'])
            continue
        if not 'size' in mem_part:
            print("no memory size for", part['desc'])
            continue
        if mem_part['size'].startswith('0x'):
            size = int(mem_part['size'], 16)
        else:
            size = int(mem_part['size'], 10)

        # output the line for the quirk
        chip_id = '0x' + part['signature'].replace('0x', '').replace(' ', '')
        mem_layout = '@Flash/0x0/1*%.0iKg' % int(size / 1024)

        # merge duplicate quirks
        if chip_id in results:
            result = results[chip_id]
            result['desc'] = _get_longest_substring(result['desc'], part['desc'])
        else:
            result = {}
            result['desc'] = part['desc']
            result['size'] = size
            result['mem_layout'] = mem_layout
            results[chip_id] = result

    for chip_id in results:
        result = results[chip_id]
        outp.append('# ' + result['desc'] + '	[USER]		USER=0x%x' % result['size'] + '\n')
        outp.append(chip_id + '=' + result['mem_layout'] + '\n\n')

    # write file
    print("writing", fn_destination)
    open(fn_destination, 'w').writelines(outp)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("USAGE: %s avrdude.conf tmp.quirk" % sys.argv[0])
        sys.exit(1)

    all_parts = _parse_parts(sys.argv[1])
    _write_quirks(all_parts, sys.argv[2])
