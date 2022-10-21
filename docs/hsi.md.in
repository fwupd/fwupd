---
title: Host Security ID Specification
---

**WARNING:
This specification is still in active development: it is incomplete, subject to change, and may have errors; use this at your own risk.
It is based on publicly available information.**

Authors:

- Richard Hughes
- Mario Limonciello
- Alex Bazhaniuk
- Alex Matrosov

---

## Introduction

Not all system vendors prioritize building a secure platform.
The truth is that **security costs money**.
Vendors have to choose between saving a few cents on a bill-of-materials by sharing a SPI chip, or correctly implementing BootGuard.
Discovering security vulnerabilities often takes an external researcher filing a disclosure.
These disclosures are often technical in nature and difficult for an average consumer to decipher.

The Linux Vendor Firmware Service (LVFS) could provide some **easy-to-understand** information to people buying hardware.
The service already knows a huge amount of information about machines from signed reports uploaded to the LVFS and from analyzing firmware binaries.
However this information alone does not explain firmware security to the user in a way they can actually interpret.

### Other Tools

Traditionally, figuring out the true security of your hardware and firmware requires sifting through the marketing documentation provided by the OEM and in many cases just "trusting" they did it right.
Tools such as Chipsec can check the hardware configuration, but they do not work out of the box and use technical jargon that an average user cannot interpret.
Unfortunately, running a tool like Chipsec requires that you actively turn off some security layers such as UEFI Secure Boot, and allow 3rd party unsigned kernel modules to be loaded.

<a id="verifying"></a>

## [Verifying Host Firmware Security](#verifying)

To start out some core protections must be assigned a relative
importance.
Then an evaluation must be done to determine how each vendor is conforming to the model.
For instance, a user might say that for home use any hardware the bare minimum security level (`HSI:1`) is *good enough*.
For a work laptop the company IT department might restrict the choice of models to anything meeting the criteria of level `HSI:2` or above.
A journalist or a security researcher would only buy level `HSI:3` and above.
The reality is that `HSI:4` is going to be more expensive than some unbranded hardware that is rated `HSI:0`.

To be trusted, this rating information should be distributed in a centralized agnostic database such as the LVFS.

Of course, tools need to detect implementation errors, and to verify that the model that is measured does indeed match the HSI level advertised by the LVFS.
Some existing compliance solutions place the burden on the OEM to define what firmware security has been implemented, which is easy to get wrong and in some cases impossible to verify.

For this reason HSI will only measure security protections that can be verified by the end user without requiring any extra hardware to be connected, additional software to be installed, or disabling any existing security layers to measure.

The HSI specification is primarily designed for laptop and desktop hardware, although some tests *may* still make sense on server or embedded hardware.
It is not expected that non-consumer hardware will publish an HSI number.

<a id="runtime-behaviour"></a>

## [Runtime Behavior](#runtime-behaviour)

Orthogonal to the security features provided by the firmware there are other security considerations related to the firmware which may require internet access to discover or that runtime OS changes directly affect the security of the firmware.
It would not make sense to have *have updates on the LVFS* as a requirement for a specific security level as this would mean offline the platform might be a higher level initially but as soon as it is brought online it is downgraded which would be really confusing to users.
The *core* security level will not change at Operating System runtime, but the suffix may.

**More information**
Additional information about specific bugs and debugging steps are available on the [fwupd wiki](https://github.com/fwupd/fwupd/wiki/Host-security-ID-runtime-issues).

<a id="hsi-level0"></a>

### [HSI:0 (Insecure State)](#hsi-level0)

Limited firmware protection.

The lowest security level with little or no detected firmware protections.
This is the default security level if no tests can be run or some tests in the next security level have failed.

<a id="hsi-level1"></a>

### [HSI:1 (Critical State)](#hsi-level1)

Basic protection but any failure would lead to a critical security impact.

This security level corresponds to the most basic of security protections considered essential by security professionals.
Any failures at this level would have critical security impact and could likely be used to compromise the system firmware without physical access.

<a id="hsi-level2"></a>

### [HSI:2 (Risky State)](#hsi-level2)

The failure is only happened by the theoretical exploit in the lab.

This security level corresponds to firmware security issues that pose a theoretical concern or where any exploit would be difficult or impractical to use.
At this level various technologies may be employed to protect the boot process from modification by an attacker with local access to the machine.

<a id="hsi-level3"></a>

### [HSI:3 (Protected State)](#hsi-level3)

The system firmware only has few minor issues which do not affect the security status.

This security level corresponds to out-of-band protection of the system firmware perhaps including recovery.

<a id="hsi-level4"></a>

### [HSI:4 (Secure State)](#hsi-level4)

The system is in a robust secure state.

The system is corresponding several kind of encryption and execution protection for the system firmware.

<a id="hsi-level5"></a>

### [HSI:5 (Secure Proven State)](#hsi-level5)

This security level corresponds to out-of-band attestation of the system firmware.
There are currently no tests implemented for HSI:5 and so this security level cannot yet be obtained.

<a id="runtime-bang"></a>

### [HSI Runtime Suffix `!`](#runtime-bang)

A runtime security issue detected.

- UEFI [Secure Boot](https://wiki.ubuntu.com/UEFI/SecureBoot) has been turned off. *[v1.5.0]*

- The kernel is [tainted](https://www.kernel.org/doc/html/latest/admin-guide/tainted-kernels.html) due to a non-free module or critical firmware issue. *[v1.5.0]*

- The kernel is not [locked down](https://mjg59.dreamwidth.org/50577.html). *[v1.5.0]*

- Unencrypted [swap partition](https://wiki.archlinux.org/index.php/Dm-crypt/Swap_encryption). *[v1.5.0]*

- The installed fwupd is running with [custom or modified plugins](https://github.com/fwupd/fwupd/tree/main/plugins). *[v1.5.0]*

<a id="low-security-level"></a>

## [Low Security Level](#low-security-level)

A safe baseline for security should be HSI-1. If your system isn't at least meeting this criteria, you should adjust firmware setup options, contact your manufacturer or replace the hardware.

The command line tool `fwupdmgr security` included with fwupd 1.8.4 or later will make individual recommendations on what you can do for individual test failures.  GUI tools built against `libfwupd` 1.8.4 or later may also make these recommendation as well.

<a id="not-enough-info"></a>

## [Not enough information](#not-enough-info)

HSI calculations require that the SOC, firmware, and kernel provide enough data to the fwupd daemon about the state of the system.  If any HSI test that runs on the system declares it's *missing data* then the client will show a message like this:

**Not enough data was provided to make an HSI calculation.**

The HSI level will also be set to `INVALID` indicating this.

<a id="tests"></a>

## [Tests included in fwupd](#tests)

The set of tests is currently x86 UEFI-centric, but will be expanded in the future for various ARM or RISC-V firmware protections as required.
Where the requirement is architecture or processor specific it has been noted.

{{tests}}

<a id="conclusions"></a>

## [Conclusion](#conclusions)

Any system with a Host Security ID of `0` can easily be modified from userspace.
PCs with confidential documents should have a `HSI:3` or higher level of protection.
In a graphical tool that would show details about the computer (such as GNOME Control Center's details tab) the OS could display a field indicating Host Security ID.
The ID should be shown with an alert color if the security is not at least `HSI:1` or the suffix is `!`.

On Linux `fwupd` is used to enumerate and update firmware.
It exports a property `HostSecurityId` and a `GetHostSecurityAttrs()` method.
The attributes are supposed to represent the *system as a whole* but individual (internal) devices are able to make a claim that they worsened the state of the security of the system.
Certain attributes can "obsolete" other attributes.
An example is BIOSGuard will set obsoletes to `org.intel.prx`.

A plugin method gets called on each plugin which adds attributes directly from the hardware or kernel.
Several attributes may be dependent upon the kernel performing measurements and it will take time for these to be upstreamed.
In some cases security level measurements will only be possible on systems with a newer kernel.

The long term goal is to increase the `HSI:x` level of systems being sold to consumers.
By making some of the `HSI:x` attributes part of the LVFS uploaded report we can allow users to compare vendors and models before purchasing hardware.

<a id="ommissions"></a>

## [Intentional Omissions](#ommissions)

### Intel SGX

This is not widely used as it has several high severity security issues.

### Intel MPX

MPX support was removed from GCC and the Linux kernel in 2019 and it is now considered obsolete.

## Further Work

More internal and external devices should be factored into the security equation.
For now the focus for further tests should be around internal device firmware as it is what can be most directly controlled by fwupd and the hardware manufacturer.

Security conscious manufacturers are actively participating in the development of future initiatives in the Trusted Computing Group (TCG).
As those become ratified standards that are available in hardware, there are opportunities for synergy with this specification.
