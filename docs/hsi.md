Title: Host Security ID Specification

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

Introduction
============

Not all system vendors prioritize building a secure platform.
The truth is that **security costs money**.
Vendors have to choose between saving a few cents on a bill-of-materials by sharing a SPI chip, or correctly implementing BootGuard.
Discovering security vulnerabilities often takes an external researcher filing a disclosure.
These disclosures are often technical in nature and difficult for an average consumer to decipher.

The Linux Vendor Firmware Service (LVFS) could provide some **easy-to-understand** information to people buying hardware.
The service already knows a huge amount of information about machines from signed reports uploaded to the LVFS and from analyzing firmware binaries.
However this information alone does not explain firmware security to the user in a way they can actually interpret.

Other Tools
-----------

Traditionally, figuring out the true security of your hardware and firmware requires sifting through the marketing documentation provided by the OEM and in many cases just "trusting" they did it right.
Tools such as Chipsec can check the hardware configuration, but they do not work out of the box and use technical jargon that an average user cannot interpret.
Unfortunately, running a tool like Chipsec requires that you actively turn off some security layers such as UEFI Secure Boot, and allow 3rd party unsigned kernel modules to be loaded.

Verifying Host Firmware Security {#verifying}
================================

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

Runtime Behavior {#runtime-behaviour}
----------------

Orthogonal to the security features provided by the firmware there are other security considerations related to the firmware which may require internet access to discover or that runtime OS changes directly affect the security of the firmware.
It would not make sense to have *have updates on the LVFS* as a requirement for a specific security level as this would mean offline the platform might be a higher level initially but as soon as it is brought online it is downgraded which would be really confusing to users.
The *core* security level will not change at Operating System runtime, but the suffix may.

HSI:0 (Insecure) {#hsi-level0}
----------------

The lowest security level with little or no detected firmware protections.
This is the default security level if no tests can be run or some tests in the next security level have failed.

HSI:1 (Critical) {#hsi-level1}
----------------

This security level corresponds to the most basic of security protections considered essential by security professionals.
Any failures at this level would have critical security impact and could likely be used to compromise the system firmware without physical access.

HSI:3 (Theoretical) {#hsi-level2}
-------------------

This security level corresponds to firmware security issues that pose a theoretical concern or where any exploit would be difficult or impractical to use.
At this level various technologies may be employed to protect the boot process from modification by an attacker with local access to the machine.

HSI:4 (System Protection) {#hsi-level4}
-------------------------

This security level corresponds to out-of-band protection of the system firmware perhaps including recovery.

HSI:5 (System Attestation) {#hsi-level5}
--------------------------

This security level corresponds to out-of-band attestation of the system firmware.
There are currently no tests implemented for HSI:5 and so this security level cannot yet be obtained.

### HSI Runtime Suffix `!` {#runtime-bang}

A runtime security issue detected.

- UEFI [Secure Boot](https://wiki.ubuntu.com/UEFI/SecureBoot) has been turned off. *[v1.5.0]*

- The kernel is [tainted](https://www.kernel.org/doc/html/latest/admin-guide/tainted-kernels.html) due to a non-free module or critical firmware issue. *[v1.5.0]*

- The kernel is not [locked down](https://mjg59.dreamwidth.org/50577.html). [*[v1.5.0]*]

- Unencrypted [swap partition](https://wiki.archlinux.org/index.php/Dm-crypt/Swap_encryption). *[v1.5.0]*

- The installed fwupd is running with [custom or modified plugins](https://github.com/fwupd/fwupd/tree/master/plugins). *[v1.5.0]*

Tests included in fwupd {#tests}
-----------------------

The set of tests is currently x86 UEFI-centric, but will be expanded in the future for various ARM or RISC-V firmware protections as required.
Where the requirement is architecture or processor specific it has been noted.

### UEFI SecureBoot {#org.fwupd.hsi.Uefi.SecureBoot}

UEFI Secure boot is a verification mechanism for ensuring that code launched by firmware is trusted.

Secure Boot requires that each binary loaded at boot is validated against trusted certificates.

- For HSI-1 SecureBoot must be available for use on UEFI systems. *[v1.5.0]*

See also:

- [https://wiki.ubuntu.com/UEFI/SecureBoot]()

### UEFI PK {#org.fwupd.hsi.Uefi.Pk}

UEFI defines a platform key for the system.
This should not be a test key, e.g. `DO NOT TRUST - AMI Test PK`

- For HSI-1 a test key must not be enrolled. *[v1.5.0]*

See also:

- [https://wiki.ubuntu.com/UEFI/SecureBoot/Testing]()

### BIOS Write Enable (BWE) {#org.fwupd.hsi.Spi.Bioswe}

Intel hardware provides this mechanism to protect the SPI ROM chip located on the motherboard from being overwritten by the operating
system.

- For HSI-1 the `BIOSWE` bit must be unset. *[v1.5.0]*

See also:

- [Intel C200 Datasheet](http://www.intel.com/content/www/us/en/chipsets/6-chipset-c200-chipset-datasheet.html)

### BIOS Lock Enable (BLE) {#org.fwupd.hsi.Spi.Ble}

If the lock bit is set then System Management Interrupts (SMIs) are raised when setting BIOS Write Enable.
The `BLE` bit must be enabled in the PCH otherwise `BIOSWE` can easily be unset.

- For HSI-1 this should be set. *[v1.5.0]*

See also:

- [Intel C200 Datasheet](http://www.intel.com/content/www/us/en/chipsets/6-chipset-c200-chipset-datasheet.html)

### SMM Bios Write Protect (SMM\_BWP) {#org.fwupd.hsi.Spi.SmmBwp}

This bit set defines when the BIOS region can be written by the host.
The `SMM_BWP` bit must be set to make the BIOS region non-writable unless all processors are in system management mode.

- For HSI-1 this should be set *[v1.5.0]*

See also:

- [Intel C200 Datasheet](http://www.intel.com/content/www/us/en/chipsets/6-chipset-c200-chipset-datasheet.html)

### Read-only SPI Descriptor {#org.fwupd.hsi.Spi.Descriptor}

The SPI descriptor must always be read only from all other regions.
Additionally on Intel architectures the FLOCKDN register must be set to prevent configuration registers in the SPI BAR from being changed.

- For HSI-1 this should be read only from all regions *[v1.6.0]*

### TPM 2.0 Present {#org.fwupd.hsi.Tpm.Version20}

A TPM securely stores platform specific secrets that can only be divulged to trusted consumers in a secure environment.

- For HSI-1 this should be available for use by the OS or applications
    *[v1.5.0]*

See also:

- [https://en.wikipedia.org/wiki/Trusted_Platform_Module]()

### ME not in manufacturing mode {#org.fwupd.hsi.Mei.ManufacturingMode}

There have been some unfortunate cases of the ME being distributed in manufacturing mode.
In manufacturing mode many features from the ME can be interacted with that decrease the platform's security.

- For HSI-1 this should be unset *[v1.5.0]*

See also:

- [ME Manufacturing Mode: obscured dangers](http://blog.ptsecurity.com/2018/10/intel-me-manufacturing-mode-macbook.html)
- [Intel security advisory SA-00086](https://www.intel.com/content/www/us/en/security-center/advisory/intel-sa-00086.html)

### ME Flash Descriptor Override {#org.fwupd.hsi.Mei.OverrideStrap}

The Flash Descriptor Security Override Strap is not accessible to end users on consumer boards and Intel stresses that this is for debugging
only.

- For HSI-1 this should be unset *[v1.5.0]*

See also:

- [Chromium documentation for Intel ME](https://chromium.googlesource.com/chromiumos/third_party/flashrom/+/master/Documentation/mysteries_intel.txt)

### CSME Version {#org.fwupd.hsi.Mei.Version}

Converged Security and Manageability Engine is a standalone management module that can manage and control some local devices without the host CPU involvement.
The CSME lives in the PCH and can only be updated by the OEM vendor.
The version of the CSME module can be checked to detect the most common and serious vulnerabilities.

- For HSI-1 this should not be vulnerable to CVE-2017-5705, CVE-2017-5708, CVE-2017-5711, CVE-2017-5712, CVE-2017-5711, CVE-2017-5712, CVE-2017-5706, CVE-2017-5709, CVE-2017-5707 or CVE-2017-5710 *[v1.5.0]*

See also:

- [Intel CSME Security Review Cumulative Update](https://www.intel.com/content/www/us/en/security-center/advisory/intel-sa-00086.html)

### Intel DCI {#org.fwupd.hsi.IntelDci}

Newer Intel CPUs support debugging over USB3 via a proprietary Direct Connection Interface (DCI) with the use of off-the-shelf hardware.
DCI should always be disabled and locked on production hardware.

- For HSI-1 this should be disabled. *[v1.5.0]*

- For HSI-2 this should be locked. *[v1.5.0]*

See also:

- [Intel Direct Connect Interface](https://www.intel.co.uk/content/www/uk/en/support/articles/000029393/processors.html)
- [Chipsec 4xxlp register definitions](https://github.com/chipsec/chipsec/blob/master/chipsec/cfg/8086/pch_4xxlp.xml#L270)
- [RISC-V EDK PCH register definitions](https://github.com/riscv/riscv-edk2-platforms/blob/85a50de1b459d1d6644a402081120770aa6dd8c7/Silicon/Intel/CoffeelakeSiliconPkg/Pch/Include/Register/PchRegsDci.h)

### PCR0 TPM Event Log Reconstruction {#org.fwupd.hsi.Tpm.ReconstructionPcr0}

The TPM event log records which events are registered for the PCR0 hash.
When reconstructed the event log values should always match the TPM PCR0.
If extra events are included in the event log, or some are missing, the reconstitution will fail.

- For HSI-2 this should match the TPM-provided PCR0 *[v1.5.0]*

See also:

- [Linux Kernel TPM Documentation](https://www.kernel.org/doc/html/latest/security/tpm/tpm_event_log.html)

### Pre-boot DMA protection {#org.fwupd.hsi.AcpiDmar}

The IOMMU on modern systems is used to mitigate against DMA attacks.
All I/O for devices capable of DMA is mapped into a private virtual memory region.
The ACPI DMAR table is used to set up pre-boot DMA protection which eliminates some firmware attacks.

- For HSI-2 this should be available *[v1.5.0]*

See also:

- [https://en.wikipedia.org/wiki/Input%E2%80%93output_memory_management_unit]()

### Intel BootGuard {#org.fwupd.hsi.IntelBootguard}

BootGuard is a processor feature that prevents the machine from running firmware images not released by the system manufacturer.
It forms a root-of-trust by fusing in cryptographic keys into the processor itself that are used to verify the Authenticated Code Modules found in the SPI flash.

- For HSI-1 verified boot must be enabled with ACM protection. *[v1.5.0]*

- For HSI-2 the error enforcement policy must be set to "immediate shutdown". *[v1.5.0]*

See also:

- [Coreboot documentation](https://github.com/coreboot/coreboot/blob/master/src/soc/intel/jasperlake/include/soc/me.h)

### Suspend to RAM disabled {#org.fwupd.hsi.SuspendToRam}

Suspend to Ram (S3) keeps the raw contents of the DRAM refreshed when the system is asleep.
This means that the memory modules can be physically removed and the contents recovered, or a cold boot attack can be performed with a USB device.

- For HSI-3 the firmware should be configured to prefer using suspend to idle instead of suspend to ram or to not offer suspend to RAM. *[v1.5.0]*

See also:

- [https://en.wikipedia.org/wiki/Cold_boot_attack]()

### Intel CET Available {#org.fwupd.hsi.IntelCet}

Control enforcement technology is available on new Intel platforms and prevents exploits from hijacking the control-flow transfer instructions for both forward-edge (indirect call/jmp) and back-edge transfer (ret).

- For HSI-3 this should be available and enabled *[v1.5.0]*

See also:

- [Intel CET Technology Preview](https://software.intel.com/sites/default/files/managed/4d/2a/control-flow-enforcement-technology-preview.pdf)

### DRAM memory encryption {#org.fwupd.hsi.EncryptedRam}

TME (Intel) or TSME (AMD) is used by the firmware on supported SOCs to encrypt all data on external memory buses.
It mitigates against an attacker being able to capture memory data while the system is running or to capture memory by removing a DRAM chip.

- For HSI-4 this should be supported and enabled *[v1.5.0]*

See also:

- [Intel TME Press Release](https://software.intel.com/content/www/us/en/develop/blogs/intel-releases-new-technology-specification-for-memory-encryption.html)
- [WikiChip SME Overview](https://en.wikichip.org/wiki/x86/sme)

### Supervisor Mode Access Prevention {#org.fwupd.hsi.IntelSmap}

Without Supervisor Mode Access Prevention, the supervisor code usually has full read and write access to user-space memory mappings.
This can make exploits easier to write, as it allows the kernel to access user-space memory when it did not intend to.

- For HSI-4 the SMAP and SMEP features should be available on the CPU. *[v1.5.0]*

See also:

- [https://en.wikipedia.org/wiki/Supervisor_Mode_Access_Prevention]()

### Kernel DMA protection {#org.fwupd.hsi.Iommu}

The IOMMU on modern systems is used to mitigate against DMA attacks.
All I/O for devices capable of DMA is mapped into a private virtual memory region.
Common implementations are Intel VT-d and AMD-Vi.

- For HSI-2 this should be available for use. *[v1.5.0]*

See also:

- [https://en.wikipedia.org/wiki/Input%E2%80%93output_memory_management_unit]()

### Suspend-to-Idle {#org.fwupd.hsi.SuspendToIdle}

The platform should be set up with Suspend-to-Idle as the default S3 sleep state.

- For HSI-3 this should be set *[v1.5.0]*

Conclusion {#conclusions}
==========

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

Intentional Omissions {#ommissions}
---------------------

### Intel SGX

This is not widely used as it has several high severity security issues.

### Intel MPX

MPX support was removed from GCC and the Linux kernel in 2019 and it is now considered obsolete.

Further Work
============

More internal and external devices should be factored into the security equation.
For now the focus for further tests should be around internal device firmware as it is what can be most directly controlled by fwupd and the hardware manufacturer.

Security conscious manufacturers are actively participating in the development of future initiatives in the Trusted Computing Group (TCG).
As those become ratified standards that are available in hardware, there are opportunities for synergy with this specification.
