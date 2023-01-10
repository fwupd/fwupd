---
title: Plugin: Linux Kernel Tainted
---

## Introduction

This plugin checks if the currently running kernel is tainted. The result will
be stored in an security attribute for HSI.

## External Interface Access

This plugin requires read access to `/sys/kernel/tainted`.
