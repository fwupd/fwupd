subdir('ata')
subdir('dfu')
subdir('colorhug')
subdir('ebitdo')
subdir('fastboot')
subdir('flashrom')
subdir('steelseries')
subdir('dell-dock')
subdir('nitrokey')
subdir('rts54hid')
subdir('rts54hub')
subdir('test')
subdir('udev')
subdir('unifying')
subdir('upower')
subdir('wacom-raw')
subdir('wacom-usb')
subdir('superio')

# depends on dfu
subdir('csr')

if get_option('plugin_nvme')
subdir('nvme')
endif

if get_option('plugin_modem_manager')
subdir('modem-manager')
endif

if get_option('plugin_altos')
subdir('altos')
endif

if get_option('plugin_amt')
subdir('amt')
endif

if get_option('plugin_thunderbolt')
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

if get_option('plugin_synaptics')
subdir('synapticsmst')
endif

if get_option('plugin_uefi')
subdir('uefi')
endif
