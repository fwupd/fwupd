subdir('dfu')
subdir('colorhug')
subdir('ebitdo')
subdir('fastboot')
subdir('fresco-pd')
subdir('jabra')
subdir('steelseries')
subdir('dell-dock')
subdir('nitrokey')
subdir('rts54hid')
subdir('rts54hub')
subdir('solokey')
subdir('synaptics-cxaudio')
subdir('synaptics-prometheus')
subdir('test')
subdir('upower')
subdir('wacom-usb')
subdir('vli')

if get_option('gudev')
subdir('ata')
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

if get_option('plugin_tpm') and get_option('gudev')
subdir('tpm')
subdir('tpm-eventlog')
endif

if get_option('plugin_emmc') and get_option('gudev')
subdir('emmc')
endif

if get_option('plugin_nvme') and get_option('gudev')
subdir('nvme')
endif

if get_option('plugin_modem_manager')
subdir('modem-manager')
endif

if get_option('plugin_altos') and get_option('gudev')
subdir('altos')
endif

if get_option('plugin_amt')
subdir('amt')
endif

if get_option('plugin_thunderbolt') and get_option('gudev')
subdir('thunderbolt')
subdir('thunderbolt-power')
endif

if get_option('plugin_redfish')
subdir('redfish')
endif

if get_option('plugin_dell')
subdir('dell')
subdir('dell-esrt')
endif

if get_option('plugin_synaptics') and get_option('gudev')
subdir('synaptics-mst')
endif

if get_option('plugin_uefi')
subdir('uefi')
subdir('uefi-recovery')
endif

if get_option('plugin_flashrom')
subdir('flashrom')
endif

if get_option('plugin_coreboot')
subdir('coreboot')
endif
