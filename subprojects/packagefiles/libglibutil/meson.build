project('libglibutil', 'c',
  version: '1.1.79')

glib_dep = dependency('glib-2.0')
gobject_dep = dependency('gobject-2.0')

glibutil_sources = []
glibutil_sources += files(
    'src/gutil_datapack.c',
    'src/gutil_history.c',
    'src/gutil_idlepool.c',
    'src/gutil_idlequeue.c',
    'src/gutil_inotify.c',
    'src/gutil_intarray.c',
    'src/gutil_ints.c',
    'src/gutil_log.c',
    'src/gutil_misc.c',
    'src/gutil_ring.c',
    'src/gutil_strv.c',
    'src/gutil_timenotify.c',
    'src/gutil_objv.c',
    'src/gutil_version.c',
    'src/gutil_weakref.c',
    # include ???
)

libglibutil_deps = [glib_dep, gobject_dep]
libglibutil = library('glibutil',
    sources: glibutil_sources,
    dependencies: libglibutil_deps,
    include_directories: include_directories('include'),
    install: true,
)

libglibutil_dep = declare_dependency(
  sources: glibutil_sources,
  dependencies: libglibutil_deps,
  include_directories: include_directories('include'),
  link_with: libglibutil,
)
