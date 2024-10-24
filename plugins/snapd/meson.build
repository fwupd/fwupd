cargs = ['-DG_LOG_DOMAIN="FuPluginSnapd"']
plugins += {meson.current_source_dir().split('/')[-1]: true}

if get_option('plugin_snapd').require(libcurl.found(),
  error_message: 'curl is needed for plugin_snapd').disable_auto_if(host_machine.system() != 'linux').allowed()
endif

plugin_builtin_snapd = static_library('fu_plugin_snapd',
  sources: [
    'fu-snapd-error.c',
    'fu-snapd-observer.c',
    'fu-snapd-plugin.c',
    'fu-snapd-snap.c',
  ],
  include_directories: plugin_incdirs,
  link_with: plugin_libs,
  c_args: cargs,
  dependencies: [
    plugin_deps,
    libcurl,
  ]
)
plugin_builtins += plugin_builtin_snapd
