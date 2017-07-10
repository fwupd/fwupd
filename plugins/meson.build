subdir('altos')
subdir('dfu')
subdir('ebitdo')
subdir('raspberrypi')
subdir('steelseries')
subdir('test')
subdir('udev')
subdir('upower')
subdir('usb')

if appstream_glib.version().version_compare('>=0.6.13')
subdir('unifying')
else
message('appstream-glib 0.6.13+ not found, disabling unifying plugin')
endif

if get_option('enable-amt')
subdir('amt')
endif

if get_option('enable-thunderbolt') and thunderbolt.found()
subdir('thunderbolt')
endif

if get_option('enable-colorhug')
subdir('colorhug')
endif

if get_option('enable-dell')
subdir('dell')
subdir('synapticsmst')
endif

if get_option('enable-uefi')
subdir('uefi')
endif

