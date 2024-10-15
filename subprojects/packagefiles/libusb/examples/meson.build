if not get_option('examples')
  subdir_done()
endif

foreach e : ['dpfp', 'hotplugtest', 'listdevs', 'sam3u_benchmark', 'testlibusb', 'xusb']
  executable(e, '@0@.c'.format(e), dependencies: libusb_dep, include_directories: config_inc)
endforeach

executable(
  'dpfp_threaded',
  'dpfp.c',
  c_args: '-DDPFP_THREADED',
  dependencies: [libusb_dep, thread_dep],
  include_directories: config_inc,
)

fxload_sources = files('ezusb.c', 'fxload.c')
getopt_inc = []
if cc.get_argument_syntax() == 'msvc'
  getopt_inc = include_directories('../msvc/getopt')
  fxload_sources += files('../msvc/getopt/getopt.c', '../msvc/getopt/getopt1.c')
endif

executable(
  'fxload',
  fxload_sources,
  dependencies: libusb_dep,
  include_directories: [config_inc, getopt_inc],
  c_args: cc.get_argument_syntax() == 'msvc' ? '-DHAVE_STRING_H' : [],
)
