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

<a id="tests"></a>

## [Tests included in fwupd](#tests)

The set of tests is currently x86 UEFI-centric, but will be expanded in the future for various ARM or RISC-V firmware protections as required.
Where the requirement is architecture or processor specific it has been noted.

<a id="org.fwupd.hsi.Uefi.SecureBoot"></a>

### [UEFI SecureBoot](#org.fwupd.hsi.Uefi.SecureBoot)

UEFI Secure boot is a verification mechanism for ensuring that code launched by firmware is trusted.

Secure Boot requires that each binary loaded at boot is validated against trusted certificates.

**Impact:** When Secure Boot is not enabled any EFI binary can be run at startup, which gives the attacker full access to your hardware.

**Possible results:**

- `not-found`: support has not been detected
- `not-enabled`: detected, but has been turned off
- `enabled`: supported and enabled

To meet HSI-1 on UEFI systems that run this test, the result must be `enabled`. *[v1.5.0]*

**Resolution:**

Turn off CSM boot and enable Secure Boot in the BIOS setup.

**References:**

- [Ubuntu SecureBoot Wiki Page](https://wiki.ubuntu.com/UEFI/SecureBoot)

<a id="org.fwupd.hsi.Uefi.Pk"></a>

### [UEFI PK](#org.fwupd.hsi.Uefi.Pk)

UEFI defines a platform key for the system.
This should not be a test key, e.g. `DO NOT TRUST - AMI Test PK`

**Impact:** It is possible to sign an EFI binary with the test platform key, which invalidates the Secure Boot trust chain.
It effectively gives the local attacker full access to your hardware.

**Possible results:**

- `valid`: valid key
- `not-valid`: an invalid key has been enrolled

To meet HSI-1 on UEFI systems that run this test, the result must be `valid`. *[v1.5.0]*

**References:**

- [Ubuntu SecureBoot Wiki Page](https://wiki.ubuntu.com/UEFI/SecureBoot/Testing)

<a id="org.fwupd.hsi.Spi.Bioswe"></a>

### [BIOS Write Enable (BWE)](#org.fwupd.hsi.Spi.Bioswe)

Intel hardware provides this mechanism to protect the SPI ROM chip located on the motherboard from being overwritten by the operating
system. The `BIOSWE` bit must be unset otherwise userspace can write to the SPI chip.

**Impact:** The system firmware can be written from userspace.
This gives any attacker with root access a method to write persistent executable code to the firmware, which survives even a full disk wipe and OS reinstall.

**Possible results:**

- `not-found`: the SPI device was not found
- `not-enabled`: write enable is disabled
- `enabled`: write enable is enabled

To meet HSI-1 on systems that run this test, the result must be `not-enabled`. *[v1.5.0]*

**References:**

- [Intel C200 Datasheet](https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/6-chipset-c200-chipset-datasheet.pdf)

<a id="org.fwupd.hsi.Spi.Ble"></a>

### [BIOS Lock Enable (BLE)](#org.fwupd.hsi.Spi.Ble)

If the lock bit is set then System Management Interrupts (SMIs) are raised when setting BIOS Write Enable.
The `BLE` bit must be enabled in the PCH otherwise `BIOSWE` can easily be unset.

**Impact:** The system firmware can be written from userspace.
This gives any attacker with root access a method to write persistent executable code to the firmware, which survives even a full disk wipe and OS reinstall.

**Possible results:**

- `enabled`: the register is locked
- `not-enabled`: the register is not locked

To meet HSI-1 on systems that run this test, the result must be `enabled`. *[v1.5.0]*

**References:**

- [Intel C200 Datasheet](https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/6-chipset-c200-chipset-datasheet.pdf)

<a id="org.fwupd.hsi.Spi.SmmBwp"></a>

### [SMM Bios Write Protect (SMM\_BWP)](#org.fwupd.hsi.Spi.SmmBwp)

This bit set defines when the BIOS region can be written by the host.
The `SMM_BWP` bit must be set to make the BIOS region non-writable unless all processors are in system management mode.

**Impact:** The system firmware can be written from userspace by exploiting a race condition in checking `BLE`.
This gives any attacker with root access a method to write persistent executable code to the firmware, which survives even a full disk wipe and OS reinstall.

**Possible results:**

- `locked`: the region is locked
- `not-locked`: the region is not locked

To meet HSI-1 on systems that run this test, the result must be `locked`. *[v1.5.0]*

**References:**

- [Intel C200 Datasheet](https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/6-chipset-c200-chipset-datasheet.pdf)

<a id="org.fwupd.hsi.Spi.Descriptor"></a>

### [Read-only SPI Descriptor](#org.fwupd.hsi.Spi.Descriptor)

The SPI descriptor must always be read only from all other regions.
Additionally on Intel architectures the FLOCKDN register must be set to prevent configuration registers in the SPI BAR from being changed.

**Impact:** The system firmware can be written from userspace by changing the protected region.
This gives any attacker with root access a method to write persistent executable code to the firmware, which survives even a full disk wipe and OS reinstall.

**Possible results:**

- `not-valid`: any region can write to the flash descriptor
- `locked`: the SPI BAR is locked and read only from all regions
- `not-locked`: the SPI BAR is not locked

To meet HSI-1 on systems that run this test, the result must be `locked`. *[v1.6.0]*

<a id="org.fwupd.hsi.Tpm.Version20"></a>

### [TPM 2.0 Present](#org.fwupd.hsi.Tpm.Version20)

A TPM securely stores platform specific secrets that can only be divulged to trusted consumers in a secure environment.

**Impact:** The PCR registers will not be available for use by the bootloader and kernel.
This means userspace cannot either encrypt disks to the specific machine, and also can't know if the system firmware was externally modified.

**Possible results:**

- `found`: device found in v2 mode
- `not-found`: no device found
- `not-enabled`: not in v2 mode

To meet HSI-1 on systems that run this test, the result must be `found`. *[v1.5.0]*

**References:**

- [TPM Wikipedia Page](https://en.wikipedia.org/wiki/Trusted_Platform_Module)

<a id="org.fwupd.hsi.Mei.ManufacturingMode"></a>

### [ME not in manufacturing mode](#org.fwupd.hsi.Mei.ManufacturingMode)

There have been some unfortunate cases of the ME being distributed in manufacturing mode.
In manufacturing mode many features from the ME can be interacted with that decrease the platform's security.

**Impact:** If the ME is in manufacturing mode then any user with root access can provision the ME engine with new keys.
This gives them full access to the system even when the system is powered off.

**Possible results:**

- `locked`: device has had manufacturing mode disabled
- `not-locked`: device is in manufacturing mode

To meet HSI-1 on systems that run this test, the result must be `locked`. *[v1.5.0]*

**References:**

- [ME Manufacturing Mode: obscured dangers](https://malware.news/t/intel-me-manufacturing-mode-obscured-dangers-and-their-relationship-to-apple-macbook-vulnerability-cve-2018-4251/23214)
- [Intel security advisory SA-00086](https://www.intel.com/content/www/us/en/security-center/advisory/intel-sa-00086.html)

<a id="org.fwupd.hsi.Mei.OverrideStrap"></a>

### [ME Flash Descriptor Override](#org.fwupd.hsi.Mei.OverrideStrap)

The Flash Descriptor Security Override Strap is not accessible to end users on consumer boards and Intel stresses that this is for debugging
only.

**Impact:** The system firmware can be written from userspace by changing the protected region.
This gives any attacker with root access a method to write persistent executable code to the firmware, which survives even a full disk wipe and OS reinstall.

**Possible results:**

- `locked`: device in in normal runtime mode
- `not-locked`: device is in debugging mode

To meet HSI-1 on systems that run this test, the result must be `locked`. *[v1.5.0]*

**References:**

- [Chromium documentation for Intel ME](https://chromium.googlesource.com/chromiumos/third_party/flashrom/+/master/Documentation/mysteries_intel.txt)

<a id="org.fwupd.hsi.Mei.Version"></a>

### [CSME Version](#org.fwupd.hsi.Mei.Version)

Converged Security and Manageability Engine is a standalone management module that can manage and control some local devices without the host CPU involvement.
The CSME lives in the PCH and can only be updated by the OEM vendor.
The version of the CSME module can be checked to detect the most common and serious vulnerabilities: CVE-2017-5705, CVE-2017-5708, CVE-2017-5711, CVE-2017-5712, CVE-2017-5711, CVE-2017-5712, CVE-2017-5706, CVE-2017-5709, CVE-2017-5707 or CVE-2017-5710.

**Impact:** Using any one of the critical vulnerabilities, a remote attacker can take full control of the system and all connected devices, even when the system is powered off.

**Possible results:**

- `valid`: is not affected by the most critical CVEs
- `not-valid`: affected by one of the below CVEs

To meet HSI-1 on systems that run this test, the result must be `valid`. *[v1.5.0]*

**References:**

- [Intel CSME Security Review Cumulative Update](https://www.intel.com/content/www/us/en/security-center/advisory/intel-sa-00086.html)

<a id="org.fwupd.hsi.IntelDci.Enabled"></a>

### [Intel DCI](#org.fwupd.hsi.IntelDci.Enabled)

Newer Intel CPUs support debugging over USB3 via a proprietary Direct Connection Interface (DCI) with the use of off-the-shelf hardware.

**Impact:** Using DCI an attacker with physical access to the computer has full access to all registers and memory in the system, and is able to make changes.
This makes privilege escalation from user to root possible, and also modifying SMM makes it possible to write to system firmware for a persistent backdoor.

**Possible results:**

- `enabled`: debugging is currently enabled
- `not-enabled`: debugging is not currently enabled

To meet HSI-1 on systems that run this test, the result must be `not-enabled`. *[v1.5.0]*

**References:**

- [Intel Direct Connect Interface](https://www.intel.co.uk/content/www/uk/en/support/articles/000029393/processors.html)
- [Chipsec 4xxlp register definitions](https://github.com/chipsec/chipsec/blob/master/chipsec/cfg/8086/pch_4xxlp.xml#L270)
- [RISC-V EDK PCH register definitions](https://github.com/riscv/riscv-edk2-platforms/blob/85a50de1b459d1d6644a402081120770aa6dd8c7/Silicon/Intel/CoffeelakeSiliconPkg/Pch/Include/Register/PchRegsDci.h)

<a id="org.fwupd.hsi.IntelDci.Locked"></a>

### [Intel DCI](#org.fwupd.hsi.IntelDci.Locked)

Newer Intel CPUs support debugging over USB3 via a proprietary Direct Connection Interface (DCI) with the use of off-the-shelf hardware.

**Impact:** A local attacker with root access would be able to enable DCI. This would allow them full access to all registers and memory in the system, and is able to make changes.
This allows using SMM to write to system firmware for a persistent backdoor.

**Possible results:**

- `locked`: CPU debugging has been disabled
- `not-locked`: is is still possible to enable CPU debugging

To meet HSI-2 on systems that run this test, the result must be `locked`. *[v1.5.0]*

**References:**

- [Intel Direct Connect Interface](https://www.intel.co.uk/content/www/uk/en/support/articles/000029393/processors.html)

<a id="org.fwupd.hsi.Tpm.EmptyPcr"></a>

### [Empty PCR in TPM](#org.fwupd.hsi.Tpm.EmptyPcr)

The system firmware is responsible for measuring values about its boot stage in PCRs 0 through 7.
Some firmwares have bugs that prevent them from measuring some of those values, breaking the fundamental assumption of the Measured Boot chain-of-trust.

**Impact:** A local attacker could measure fake values into the empty PCR, corresponding to a firmware and OS that do not match the ones actually loaded.
This allows hiding a compromised boot chain or fooling a remote-attestation server into believing that a different kernel is running.

**Possible results:**

- `valid`: all correct
- `not-valid`: at least one empty checksum has been found
- `not-found`: no TPM hardware could be found

To meet HSI-1 on systems that run this test, all PCRs from 0 to 7 in all banks must have non-empty measurements *[v1.7.2]*

**References:**

- [CVE-2021-42299: TPM Carte Blanche](https://github.com/google/security-research/blob/master/pocs/bios/tpm-carte-blanche/writeup.md)

<a id="org.fwupd.hsi.Tpm.ReconstructionPcr0"></a>

### [PCR0 TPM Event Log Reconstruction](#org.fwupd.hsi.Tpm.ReconstructionPcr0)

The TPM event log records which events are registered for the PCR0 hash.
When reconstructed the event log values should always match the TPM PCR0.
If extra events are included in the event log, or some are missing, the reconstitution will fail.

**Impact:** This is not a vulnerability per-se, but it shows that the system firmware checksum cannot be verified as the PCR result has been calculated incorrectly.

**Possible results:**

- `valid`: all correct
- `not-valid`: could not reconstitute the hash value
- `not-found`: no TPM hardware could be found

To meet HSI-2 on systems that run this test, the result must be `valid`. *[v1.5.0]*

**References:**

- [Linux Kernel TPM Documentation](https://www.kernel.org/doc/html/latest/security/tpm/tpm_event_log.html)

<a id="org.fwupd.hsi.AcpiDmar"></a>

### [Pre-boot DMA protection](#org.fwupd.hsi.AcpiDmar)

The IOMMU on modern systems is used to mitigate against DMA attacks.
All I/O for devices capable of DMA is mapped into a private virtual memory region.
The ACPI DMAR table is used to set up pre-boot DMA protection which eliminates some firmware attacks.

**Impact:** Without a DMAR table the IOMMU is disabled at boot.
An attacker could connect a malicious peripheral using ThunderBolt and reboot the machine, which would allow the attacker to modify the system memory.
This would allow subverting the Secure Boot protection, and also invalidate any system attestation.

**Possible results:**

- `enabled`: detected correctly
- `not-valid`: could not determine state
- `not-enabled`: was not enabled

To meet HSI-3 on systems that run this test, the result must be `enabled`. *[v1.5.0]*

**References:**

- [IOMMU Wikipedia Page](https://en.wikipedia.org/wiki/Input%E2%80%93output_memory_management_unit)

<a id="org.fwupd.hsi.Kernel.IntelBootguard"></a>
<a id="org.fwupd.hsi.IntelBootguard.Enabled"></a>

### [Intel BootGuard: Enabled](#org.fwupd.hsi.IntelBootguard.Enabled)

BootGuard is a processor feature that prevents the machine from running firmware images not released by the system manufacturer.
It forms a root-of-trust by fusing in cryptographic keys into the processor itself that are used to verify the Authenticated Code Modules found in the SPI flash.

**Impact:** When BootGuard is not set up correctly then the chain-of-trust between the CPU and the bootloader can not be verified.
This would allow subverting the Secure Boot protection which gives the attacker full access to your hardware.

**Possible results:**

- `enabled`: detected and enabled
- `not-enabled`: not detected, or detected but not enabled

To meet HSI-2 on systems that run this test, the result must be `enabled`. *[v1.5.0]*

**References:**

- [Coreboot documentation](https://github.com/coreboot/coreboot/blob/master/src/soc/intel/jasperlake/include/soc/me.h)

<a id="org.fwupd.hsi.IntelBootguard.Verified"></a>

### [Intel BootGuard: Verified](#org.fwupd.hsi.IntelBootguard.Verified)

BootGuard is a processor feature that prevents the machine from running firmware images not released by the system manufacturer.
It forms a root-of-trust by fusing in cryptographic keys into the processor itself that are used to verify the Authenticated Code Modules found in the SPI flash.

**Impact:** When BootGuard is not set up correctly then the chain-of-trust between the CPU and the bootloader can not be verified.
This would allow subverting the Secure Boot protection which gives the attacker full access to your hardware.

**Possible results:**

- `success`: verified boot chain
- `not-valid`: boot is not verified

To meet HSI-2 on systems that run this test, the result must be `success`. *[v1.5.0]*

<a id="org.fwupd.hsi.IntelBootguard.Acm"></a>

### [Intel BootGuard: ACM](#org.fwupd.hsi.IntelBootguard.Acm)

BootGuard is a processor feature that prevents the machine from running firmware images not released by the system manufacturer.
It forms a root-of-trust by fusing in cryptographic keys into the processor itself that are used to verify the Authenticated Code Modules found in the SPI flash.

**Impact:** When BootGuard is not set up correctly then the chain-of-trust between the CPU and the bootloader can not be verified.
This would allow subverting the Secure Boot protection which gives the attacker full access to your hardware.

**Possible results:**

- `valid`: ACM protected
- `not-valid`: boot is not verified

To meet HSI-2 on systems that run this test, the result must be `valid`. *[v1.5.0]*

<a id="org.fwupd.hsi.IntelBootguard.Policy"></a>

### [Intel BootGuard: Policy](#org.fwupd.hsi.IntelBootguard.Policy)

BootGuard is a processor feature that prevents the machine from running firmware images not released by the system manufacturer.
It forms a root-of-trust by fusing in cryptographic keys into the processor itself that are used to verify the Authenticated Code Modules found in the SPI flash.

**Impact:** The attacker can invalidate the chain of trust (subverting Secure Boot), and the user would get just a console warning and then continue to boot.

**Possible results:**

- `valid`: error enforce policy is set to shutdown
- `not-valid`: policy is invalid

To meet HSI-3 on systems that run this test, the result must be `valid`. *[v1.5.0]*

<a id="org.fwupd.hsi.IntelBootguard.Otp"></a>

### [Intel BootGuard: OTP](#org.fwupd.hsi.IntelBootguard.Otp)

BootGuard is a processor feature that prevents the machine from running firmware images not released by the system manufacturer.
It forms a root-of-trust by fusing in cryptographic keys into the processor itself that are used to verify the Authenticated Code Modules found in the SPI flash.

**Impact:** When BootGuard is not set up correctly then the chain-of-trust between the CPU and the bootloader can not be verified.
This would allow subverting the Secure Boot protection which gives the attacker full access to your hardware.

**Possible results:**

- `valid`: SOC is locked
- `not-valid`: SOC is not locked

To meet HSI-2 on systems that run this test, the result must be `valid`. *[v1.5.0]*

<a id="org.fwupd.hsi.SuspendToRam"></a>

### [Suspend to RAM disabled](#org.fwupd.hsi.SuspendToRam)

Suspend to Ram (S3) keeps the raw contents of the DRAM refreshed when the system is asleep.
This means that the memory modules can be physically removed and the contents recovered, or a cold boot attack can be performed with a USB device.
The firmware should be configured to prefer using suspend to idle instead of suspend to ram or to not offer suspend to RAM.

**Impact:** An attacker with physical access to a system can obtain the un-encrypted contents of the RAM by suspending the machine, removing the DIMM and inserting it into another machine with modified DRAM controller before the memory contents decay.

**Possible results:**

- `enabled`: sleep enabled
- `not-enabled`: suspend-to-ram being used
- `not-valid`: could not determine the default

To meet HSI-3 on systems that run this test, the result must be `not-enabled`. *[v1.5.0]*

**References:**

- [Cold Boot Attack Wikipedia Page](https://en.wikipedia.org/wiki/Cold_boot_attack)

<a id="org.fwupd.hsi.IntelCet.Enabled"></a>

### [Intel CET: Available](#org.fwupd.hsi.IntelCet.Enabled)

Control enforcement technology is available on new Intel platforms and prevents exploits from hijacking the control-flow transfer instructions for both forward-edge (indirect call/jmp) and back-edge transfer (ret).

**Impact:** A local or physical attacker with an existing unrelated vulnerability can use a reliable and well-known method to run arbitrary code.

**Possible results:**

- `enabled`: feature enabled by the platform
- `not-supported`: not supported

To meet HSI-3 on systems that run this test, the result must be `enabled`. *[v1.5.0]*

**References:**

- [Intel CET Technology Preview](https://software.intel.com/sites/default/files/managed/4d/2a/control-flow-enforcement-technology-preview.pdf)

<a id="org.fwupd.hsi.IntelCet.Active"></a>

### [Intel CET: Active](#org.fwupd.hsi.IntelCet.Active)

Control enforcement technology is available on new Intel platforms and prevents exploits from hijacking the control-flow transfer instructions for both forward-edge (indirect call/jmp) and back-edge transfer (ret).

**Impact:** A local or physical attacker with an existing unrelated vulnerability can use a ROP gadget to run arbitrary code.

**Possible results:**

- `supported`: being used
- `not-supported`: not being used by the host

To meet HSI-3 on systems that run this test, the result must be `supported`. *[v1.5.0]*

**References:**

- [Intel CET Technology Preview](https://software.intel.com/sites/default/files/managed/4d/2a/control-flow-enforcement-technology-preview.pdf)

<a id="org.fwupd.hsi.EncryptedRam"></a>

### [DRAM memory encryption](#org.fwupd.hsi.EncryptedRam)

TME (Intel) or SME (AMD) is used by the hardware on supported SOCs to encrypt all data on external memory buses.
It mitigates against an attacker being able to capture memory data while the system is running or to capture memory by removing a DRAM chip.

This encryption may be activated by either transparently via firmware configuration or by code running in the Linux kernel.

**Impact:** A local attacker can either extract unencrypted content by attaching debug probes on the DIMM modules, or by removing them and inserting them into a computer with a modified DRAM controller.

**Possible results:**

- `encrypted`: detected and enabled
- `not-encrypted`: detected but disabled
- `not-supported`: not available

To meet HSI-4 on systems that run this test, the result must be `enabled`. *[v1.5.0]*

**References:**

- [Intel TME Press Release](https://software.intel.com/content/www/us/en/develop/blogs/intel-releases-new-technology-specification-for-memory-encryption.html)
- [WikiChip SME Overview](https://en.wikichip.org/wiki/x86/sme)

<a id="org.fwupd.hsi.IntelSmap"></a>

### [Supervisor Mode Access Prevention](#org.fwupd.hsi.IntelSmap)

Without Supervisor Mode Access Prevention, the supervisor code usually has full read and write access to user-space memory mappings.
This can make exploits easier to write, as it allows the kernel to access user-space memory when it did not intend to.

**Impact:** A local or remote attacker can use a simple exploit to modify the contents of kernel memory which can lead to privilege escalation.

**Possible results:**

- `enabled`: features are detected and enabled
- `not-supported`: not enabled

To meet HSI-4 on systems that run this test, the result must be `enabled`. *[v1.5.0]*

**References:**

- [SMAP Wikipedia Page](https://en.wikipedia.org/wiki/Supervisor_Mode_Access_Prevention)

<a id="org.fwupd.hsi.Iommu"></a>

### [Kernel DMA protection](#org.fwupd.hsi.Iommu)

The IOMMU on modern systems is used to mitigate against DMA attacks.
All I/O for devices capable of DMA is mapped into a private virtual memory region.
Common implementations are Intel VT-d and AMD-Vi.

**Impact:** An attacker with inexpensive PCIe development hardware can write to system RAM from the ThunderBolt or Firewire ports which can lead to privilege escalation.

**Possible results:**

- `enabled`: hardware detected and enabled
- `not-found`: hardware was not detected

To meet HSI-2 on systems that run this test, the result must be `enabled`. *[v1.5.0]*

**Resolution:** If available, turn on IOMMU in the system BIOS. You may also have to use additional kernel boot parameters, for example `intel_iommu=on iommu=pt`.

**References:**

- [IOMMU Wikipedia Page](https://en.wikipedia.org/wiki/Input%E2%80%93output_memory_management_unit)

<a id="org.fwupd.hsi.SuspendToIdle"></a>

### [Suspend-to-Idle](#org.fwupd.hsi.SuspendToIdle)

The platform should be set up with Suspend-to-Idle as the default S3 sleep state.

**Impact:** A local attacker could overwrite the S3 resume script to modify system RAM which can lead to privilege escalation.

**Possible results:**

- `enabled`: deep sleep enabled
- `not-enabled`: suspend-to-idle being used
- `not-valid`: could not determine the default

To meet HSI-3 on systems that run this test, the result must be `not-enabled`. *[v1.5.0]*

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
