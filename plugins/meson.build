subdir('dfu')
subdir('ebitdo')
subdir('raspberrypi')
subdir('steelseries')
subdir('test')
subdir('udev')
subdir('unifying')
subdir('upower')

if get_option('enable-altos')
subdir('altos')
endif

if get_option('enable-amt')
subdir('amt')
endif

if get_option('enable-thunderbolt') and udev.found()
subdir('thunderbolt')
subdir('thunderbolt-power')
endif

if get_option('enable-colorhug')
subdir('colorhug')
endif

if get_option('enable-dell')
subdir('dell')
endif

if get_option('enable-synaptics')
subdir('synapticsmst')
endif

if get_option('enable-uefi')
subdir('uefi')
endif

if get_option('enable-usb-fallback')
subdir('usb')
endif

