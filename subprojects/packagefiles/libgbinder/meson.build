project('libgbinder', 'c',
  version: '1.1.40',
  meson_version : '>=0.56.0')

conf = configuration_data()

glib_dep = dependency('glib-2.0')
gobject_dep = dependency('gobject-2.0')
libglibutil_dep = dependency('libglibutil')

libgbinder_deps = [glib_dep, gobject_dep, libglibutil_dep]

libgbinder_sources = []
libgbinder_sources += files(
  'src/gbinder_bridge.c',
  'src/gbinder_buffer.c',
  'src/gbinder_cleanup.c',
  'src/gbinder_client.c',
  'src/gbinder_config.c',
  'src/gbinder_driver.c',
  'src/gbinder_eventloop.c',
  'src/gbinder_fmq.c',
  'src/gbinder_io_32.c',
  'src/gbinder_io_64.c',
  'src/gbinder_ipc.c',
  'src/gbinder_local_object.c',
  'src/gbinder_local_reply.c',
  'src/gbinder_local_request.c',
  'src/gbinder_log.c',
  'src/gbinder_proxy_object.c',
  'src/gbinder_reader.c',
  'src/gbinder_remote_object.c',
  'src/gbinder_remote_reply.c',
  'src/gbinder_remote_request.c',
  'src/gbinder_rpc_protocol.c',
  'src/gbinder_servicename.c',
  'src/gbinder_servicepoll.c',
  'src/gbinder_writer.c',
)

libgbinder_sources += files(
  'src/gbinder_servicemanager.c',
  'src/gbinder_servicemanager_aidl.c',
  'src/gbinder_servicemanager_aidl2.c',
  'src/gbinder_servicemanager_aidl3.c',
  'src/gbinder_servicemanager_aidl4.c',
  'src/gbinder_servicemanager_hidl.c',
)

libgbinder_sources += files(
  'src/gbinder_system.c',
)

libgbinder_headers = files(
  'include/gbinder_bridge.h',
  'include/gbinder_buffer.h',
  'include/gbinder_client.h',
  'include/gbinder_eventloop.h',
  'include/gbinder_fmq.h',
  'include/gbinder.h',
  'include/gbinder_local_object.h',
  'include/gbinder_local_reply.h',
  'include/gbinder_local_request.h',
  'include/gbinder_reader.h',
  'include/gbinder_remote_object.h',
  'include/gbinder_remote_reply.h',
  'include/gbinder_remote_request.h',
  'include/gbinder_servicemanager.h',
  'include/gbinder_servicename.h',
  'include/gbinder_types.h',
  'include/gbinder_writer.h',
)

libgbinder_include = include_directories('include')

cc = meson.get_compiler('c')

conf.set10('HAS_PTHREAD_TIMEDJOIN_NP', cc.has_function('pthread_timedjoin_np'))

add_project_arguments('-DHAS_CONFIG_H=1', language: 'c')
configure_file(
  output: 'config.h',
  configuration: conf,
)

install_headers(libgbinder_headers, subdir: 'gbinder')

libgbinder = library(
  'gbinder',
  sources: libgbinder_sources,
  dependencies: libgbinder_deps,
  include_directories: [libgbinder_include],
  install: true,
)


pkg = import('pkgconfig')
pkg.generate(libgbinder,
  name: meson.project_name(),
  description: 'GLib-style interface to binder',
  requires: ['glib-2.0', 'gobject-2.0', 'libglibutil'],
  subdirs: ['gbinder']
)

libgbinder_dep = declare_dependency(
    dependencies: libgbinder_deps,
    include_directories: include_directories('include'),
    link_with: libgbinder,
)

meson.override_dependency('libgbinder', libgbinder_dep)
