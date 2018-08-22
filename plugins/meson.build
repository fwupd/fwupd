subdir('dfu')
subdir('colorhug')
subdir('ebitdo')
subdir('flashrom')
subdir('steelseries')
subdir('nitrokey')
subdir('rts54hub')
subdir('test')
subdir('udev')
subdir('unifying')
subdir('upower')
subdir('wacomhid')
subdir('superio')

# depends on dfu
subdir('csr')

if get_option('plugin_nvme')
subdir('nvme')
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
