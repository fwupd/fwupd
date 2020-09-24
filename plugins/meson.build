subdir('acpi-dmar')
subdir('acpi-facp')
subdir('bios')
subdir('ccgx')
subdir('cros-ec')
subdir('cpu')
subdir('dfu')
subdir('colorhug')
subdir('ebitdo')
subdir('ep963x')
subdir('fastboot')
subdir('fresco-pd')
subdir('iommu')
subdir('jabra')
subdir('linux-lockdown')
subdir('linux-sleep')
subdir('linux-swap')
subdir('linux-tainted')
subdir('steelseries')
subdir('dell-dock')
subdir('nitrokey')
subdir('pci-bcr')
subdir('pci-mei')
subdir('platform-integrity')
subdir('rts54hid')
subdir('rts54hub')
subdir('solokey')
subdir('synaptics-cxaudio')
subdir('synaptics-prometheus')
subdir('test')
subdir('upower')
subdir('wacom-usb')
subdir('vli')
subdir('goodix-moc')

if get_option('plugin_msr')
subdir('msr')
endif

if get_option('gudev')
subdir('ata')
subdir('elantp')
subdir('logitech-hidpp')
subdir('optionrom')
subdir('superio')
subdir('synaptics-rmi')
subdir('thelio-io')
subdir('wacom-raw')
endif

if get_option('systemd')
subdir('logind')
endif

# depends on dfu
subdir('csr')

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
if not get_option('gudev')
  error('gudev is required for plugin_altos')
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
subdir('redfish')
endif

if get_option('plugin_dell')
subdir('dell')
subdir('dell-esrt')
endif

if get_option('plugin_synaptics')
if not get_option('gudev')
  error('gudev is required for plugin_synaptics_mst')
endif
subdir('synaptics-mst')
endif

if get_option('plugin_uefi')
subdir('uefi')
subdir('uefi-recovery')
subdir('uefi-dbx')
endif

if get_option('plugin_flashrom')
subdir('flashrom')
endif

if get_option('plugin_coreboot')
subdir('coreboot')
endif
