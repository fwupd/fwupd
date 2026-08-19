---
title: Plugin: bootupd Integration
---

## Introduction

This plugin allows integration with bootupd, to allow it to sync the ESP as required.

This can be tested by:

    sudo dnf copr enable rapneset/bootupd-varlink
    sudo dnf install bootupd --repo 'copr:copr.fedorainfracloud.org:rapneset:bootupd-varlink'
    sudo systemctl start bootupd-varlink.service

## External Interface Access

This plugin requires varlink access to `/run/bootupd/org.coreos.bootupd1`.

## Version Considerations

This plugin has been available since fwupd version `2.1.8`.
