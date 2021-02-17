subdir('bcm57xx')
subdir('cpu')
subdir('ep963x')
subdir('dell-dock')
subdir('nitrokey')
subdir('test')

#Plugins requiring Gudev backend
if get_option('gudev')
  subdir('ata')
  subdir('elantp')
  subdir('optionrom')
  subdir('superio')
  subdir('thelio-io')
  subdir('wacom-raw')

  #Plugins requiring Gudev + Gusb backends
  if get_option('gusb')
    subdir('dfu')
    subdir('csr') # depends on dfu
    subdir('logitech-hidpp')
  endif
endif

# Plugins only requiring GUSB backend
if get_option('gusb')
  subdir('ccgx')
  subdir('colorhug')
  subdir('cros-ec')
  subdir('ebitdo')
  subdir('fastboot')
  subdir('fresco-pd')
  subdir('goodix-moc')
  subdir('hailuck')
  subdir('jabra')
  subdir('rts54hid')
  subdir('rts54hub')
  subdir('solokey')
  subdir('steelseries')
  subdir('synaptics-cxaudio')
  subdir('synaptics-prometheus')
  subdir('system76-launch')
  subdir('vli')
  subdir('wacom-usb')
endif

# Individual plugins that can be enabled/disabled
if get_option('plugin_altos')
  if not get_option('gudev') or not get_option('gusb')
    error('gudev and gusb is required for plugin_altos')
  endif
  subdir('altos')
endif

if get_option('plugin_amt')
  subdir('amt')
endif

if get_option('plugin_dell')
  subdir('dell')
  subdir('dell-esrt')
endif

if get_option('plugin_emmc')
  if not get_option('gudev')
    error('gudev is required for plugin_emmc')
  endif
  subdir('emmc')
endif

if get_option('plugin_flashrom')
  subdir('flashrom')
endif

if get_option('plugin_modem_manager')
  subdir('modem-manager')
endif

if get_option('plugin_msr')
  subdir('msr')
endif

if get_option('plugin_nvme')
  if not get_option('gudev')
    error('gudev is required for plugin_nvme')
  endif
  subdir('nvme')
endif

if get_option('plugin_platform_integrity')
  subdir('platform-integrity')
endif

if get_option('plugin_redfish')
  if not get_option('curl')
    error('curl is required for plugin_redfish')
  endif
  subdir('redfish')
endif

if get_option('plugin_synaptics_mst')
  if not get_option('gudev')
    error('gudev is required for plugin_synaptics_mst')
  endif
  subdir('synaptics-mst')
endif

if get_option('plugin_synaptics_rmi')
  if not get_option('gudev')
    error('gudev is required for plugin_synaptics_rmi')
  endif
  subdir('synaptics-rmi')
endif

if get_option('plugin_thunderbolt')
  if not get_option('gudev')
    error('gudev is required for plugin_thunderbolt')
  endif
  subdir('thunderbolt')
endif

if get_option('plugin_uefi_capsule')
  subdir('bios')
  subdir('uefi-capsule')
  subdir('uefi-recovery')
  subdir('uefi-dbx')
endif

if get_option('plugin_uefi_pk')
  subdir('uefi-pk')
endif

# Daemon support plugins
if get_option('systemd')
  subdir('logind')
endif

# TPM plugins
if get_option('tpm')
  if not get_option('gudev')
    error('gudev is required for tpm')
  endif
  subdir('tpm')
  subdir('tpm-eventlog')
endif

# These don't offer individual meson options
# but will currently only run on Linux
if host_machine.system() == 'linux'
  subdir('acpi-dmar')       #/sys/firmware/acpi
  subdir('acpi-facp')       #/sys/firmware/acpi
  subdir('iommu')           #linux kernel iommu
  subdir('linux-lockdown')
  subdir('linux-sleep')
  subdir('linux-swap')
  subdir('linux-tainted')
  subdir('pci-bcr')         #pci
  subdir('pci-mei')         #pci
  subdir('upower')
  subdir('pixart-rf')       #linux kernel hidraw
endif
