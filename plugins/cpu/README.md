# CPU Microcode

## Introduction

This plugin reads the sysfs attributes associated with CPU microcode.
It displays a read-only value of the CPU microcode version loaded onto
the physical CPU at fwupd startup.

This plugin can also update the microcode for supported CPU vendors. It can
late load microcode using sysfs and early load via initramfs for subsequent
boots.

Note: For debian based distros, there is a dependency on kernel module "msr" to
be loaded. This is required for msr plugin to work in general which updates the
microcode version for cpu plugin.
This can be manually done as:
 # modprobe msr

## GUID Generation

These devices add extra instance IDs from the CPUID values, e.g.

* `CPUID\PRO_0&FAM_06`
* `CPUID\PRO_0&FAM_06&MOD_0E`
* `CPUID\PRO_0&FAM_06&MOD_0E&STP_3`

## External Interface Access

This plugin requires no extra access.
