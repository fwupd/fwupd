subdir('dfu')
subdir('ebitdo')
subdir('steelseries')
subdir('nitrokey')
subdir('test')
subdir('udev')
subdir('unifying')
subdir('upower')
subdir('csr')

static_plugins = [dfu_static,
                  ebitdo_static,
                  steelseries_static,
                  nitrokey_static,
                  test_static,
                  udev_static,
                  unifying_static,
                  upower_static,
                  csr_static]

if get_option('plugin_altos')
subdir('altos')
static_plugins += altos_static
endif

if get_option('plugin_amt')
subdir('amt')
static_plugins += amt_static
endif

if get_option('plugin_thunderbolt') and udev.found()
subdir('thunderbolt')
subdir('thunderbolt-power')
static_plugins += [thunderbolt_static, thunderbolt_power_static]
endif

if get_option('plugin_colorhug')
subdir('colorhug')
static_plugins += colorhug_static
endif

if get_option('plugin_dell')
subdir('dell')
static_plugins += dell_static
endif

if get_option('plugin_synaptics')
subdir('synapticsmst')
static_plugins += synapticsmst_static
endif

if get_option('plugin_uefi')
subdir('uefi')
static_plugins += uefi_static
endif

#Create a static library of ALL plugins
plugin_objects = []
foreach p : static_plugins
  plugin_objects += p.extract_all_objects ()
endforeach
plugins_static = static_library ('plugins_static',
                                 objects : plugin_objects,
                                 install : false
)
