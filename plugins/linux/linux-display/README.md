---
title: Plugin: Linux Display
---

## Introduction

This plugin checks if there are displays connected to each DRM device. The result will be used to
inhibit devices that require a monitor to be attached.

## External Interface Access

This plugin requires read access to `/sys/class/drm`.

## Version Considerations

This plugin has been available since fwupd version `1.9.6`.
