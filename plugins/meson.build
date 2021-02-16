subdir('acpi-dmar')
subdir('acpi-facp')
subdir('bcm57xx')
subdir('cpu')
subdir('ep963x')
subdir('iommu')
subdir('linux-lockdown')
subdir('linux-sleep')
subdir('linux-swap')
subdir('linux-tainted')
subdir('dell-dock')
subdir('nitrokey')
subdir('pci-bcr')
subdir('pci-mei')
subdir('test')
subdir('upower')
subdir('pixart-rf')

if get_option('plugin_msr')
subdir('msr')
endif

if get_option('gudev')
subdir('ata')
subdir('elantp')
subdir('optionrom')
subdir('superio')
subdir('thelio-io')
subdir('wacom-raw')
endif

if get_option('gudev') and get_option('plugin_synaptics_rmi')
subdir('synaptics-rmi')
endif

if get_option('gudev') and get_option('gusb')
subdir('logitech-hidpp')
endif

if get_option('systemd')
subdir('logind')
endif

if get_option('gusb')
subdir('ccgx')
subdir('colorhug')
subdir('cros-ec')
subdir('dfu')
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

# depends on dfu
subdir('csr')
endif

if get_option('tpm')
if not get_option('gudev')
  error('gudev is required for tpm')
endif
subdir('tpm')
subdir('tpm-eventlog')
endif

if get_option('plugin_emmc')
if not get_option('gudev')
  error('gudev is required for plugin_emmc')
endif
subdir('emmc')
endif

if get_option('plugin_nvme')
if not get_option('gudev')
  error('gudev is required for plugin_nvme')
endif
subdir('nvme')
endif

if get_option('plugin_modem_manager')
subdir('modem-manager')
endif

if get_option('plugin_altos')
if not get_option('gudev') or not get_option('gusb')
  error('gudev and gusb is required for plugin_altos')
endif
subdir('altos')
endif

if get_option('plugin_amt')
subdir('amt')
endif

if get_option('plugin_thunderbolt')
if not get_option('gudev')
  error('gudev is required for plugin_thunderbolt')
endif
subdir('thunderbolt')
endif

if get_option('plugin_redfish')
if not get_option('curl')
  error('curl is required for plugin_redfish')
endif
subdir('redfish')
endif

if get_option('plugin_dell')
subdir('dell')
subdir('dell-esrt')
endif

if get_option('plugin_synaptics_mst')
if not get_option('gudev')
  error('gudev is required for plugin_synaptics_mst')
endif
subdir('synaptics-mst')
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

if get_option('plugin_flashrom')
subdir('flashrom')
endif

if get_option('plugin_platform_integrity')
subdir('platform-integrity')
endif
