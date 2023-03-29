---
title: Plugin: MSR
---

## Introduction

This plugin checks if the Model-specific registers (MSRs) indicate the
Direct Connect Interface (DCI) is enabled.

DCI allows debugging of Intel processors using the USB3 port. DCI should
always be disabled and locked on production hardware as it allows the
attacker to disable other firmware protection methods.

The result will be stored in a security attribute for HSI.

## External Interface Access

This plugin requires read access to `/sys/class/msr`.

## Version Considerations

This plugin has been available since fwupd version `1.5.0`.
