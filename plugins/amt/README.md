Intel Management Engine
=======================

Introduction
------------

This plugin is used to get the version number on the Intel Management Engine.

If AMT is enabled and provisioned and the AMT version is between 6.0 and 11.2,
and you have not upgraded your firmware, you are vulnerable to CVE-2017-5689 and
you should disable AMT in your system firmware.

This code is inspired by 'AMT status checker for Linux' by Matthew Garrett
which can be found here: https://github.com/mjg59/mei-amt-check

That tool in turn is heavily based on mei-amt-version from samples/mei in the
Linux source tree and copyright Intel Corporation.

GUID Generation
---------------

These devices use the existing GUID provided by the AMT host interface.

Vendor ID Security
------------------

The device is not upgradable and thus requires no vendor ID set.
