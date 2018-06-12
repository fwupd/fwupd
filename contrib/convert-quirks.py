#!/usr/bin/env python3
""" This pivots the data format in a quirk file """

# pylint: disable=wrong-import-position

"""
SPDX-License-Identifier: LGPL-2.1+
"""

import sys
import configparser

for argv in sys.argv[1:]:

    # create new file
    config2 = configparser.ConfigParser()
    config2.optionxform = str

    # parse existing file
    config = configparser.ConfigParser(strict=False)
    config.optionxform = str
    config.read(argv)
    for sect in config.sections():
        for key in config[sect]:
            value = config[sect][key]

            print(sect + "::" + key, "==", value)

            key2 = 'DeviceInstanceId=' + key
            sect2 = sect
            if sect.startswith('Fu') and sect.endswith('Device'):
                sect2 = 'flags'
            if sect2.startswith('DeviceInstanceId='):
                sect2 = sect2[12:]
            sect2 = sect2.capitalize()
            if key2 not in config2:
                config2[key2] = {}
            config2[key2][sect2] = value

    # write new file #argv
    with open(argv + '.new', 'w') as configfile:
        config2.write(configfile, space_around_delimiters=True)
