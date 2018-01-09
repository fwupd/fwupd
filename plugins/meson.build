subdir('dfu')
subdir('ebitdo')
subdir('raspberrypi')
subdir('steelseries')
subdir('nitrokey')
subdir('test')
subdir('udev')
subdir('unifying')
subdir('upower')

# depends on dfu
subdir('csr')

if get_option('plugin_altos')
subdir('altos')
endif

if get_option('plugin_amt')
subdir('amt')
endif

if get_option('plugin_thunderbolt') and udev.found()
subdir('thunderbolt')
subdir('thunderbolt-power')
endif

if get_option('plugin_colorhug')
subdir('colorhug')
endif

if get_option('plugin_dell')
subdir('dell')
endif

if get_option('plugin_synaptics')
subdir('synapticsmst')
endif

if get_option('plugin_uefi')
subdir('uefi')
endif
