host_machine.system() == 'linux' or subdir_done()

cargs = ['-DG_LOG_DOMAIN="FuPluginNvidiaOobRedfish"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

plugin_quirks += files('nvidia-oob-redfish.quirk')

install_data(
  'nvidia_oob.conf',
  install_dir: join_paths(sysconfdir, 'fwupd'),
)

# Install helper scripts
install_data(
  'fwupd-nvidia-oob-env.sh',
  'install-plugin.sh',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

shared_module('fu_plugin_nvidia_oob_redfish',
  sources: [
    'fu-nvidia-oob-plugin.c',
    'fu-nvidia-oob-device.c',
    'fu-nvidia-oob-redfish-client.c',
  ],
  include_directories: plugin_incdirs,
  install: true,
  install_rpath: libdir_pkg,
  install_dir: libdir_pkg,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    libcurl,
  ],
)
