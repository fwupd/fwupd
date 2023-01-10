---
title: Plugin: Lenovo ThinkLMI
---

## Introduction

This allows checking whether the firmware on a Lenovo system is configured to
allow UEFI capsule updates using the thinklmi kernel module.

## External Interface Access

This plugin requires:

* read access to `/sys/class/firmware-attributes`.
