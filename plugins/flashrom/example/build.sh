#!/bin/sh
appstream-util validate-relax com.Flashrom.Laptop.metainfo.xml
tar -cf firmware.tar startup.sh random-tool
gcab --create --nopath Flashrom-Laptop-1.2.3.cab firmware.tar com.Flashrom.Laptop.metainfo.xml
