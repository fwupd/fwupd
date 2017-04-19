project('fwupd', 'c',
  version : '0.9.1',
  license : 'LGPL-2.1+',
  meson_version : '>=0.37.0'
)

fwupd_version = meson.project_version()
varr = fwupd_version.split('.')
fwupd_major_version = varr[0]
fwupd_minor_version = varr[1]
fwupd_micro_version = varr[2]

conf = configuration_data()
conf.set('FWUPD_MAJOR_VERSION_CONF', fwupd_major_version)
conf.set('FWUPD_MINOR_VERSION_CONF', fwupd_minor_version)
conf.set('FWUPD_MICRO_VERSION_CONF', fwupd_micro_version)
conf.set_quoted('PACKAGE_VERSION', fwupd_version)

# libtool versioning - this applies to libfwupd
#
# See http://sources.redhat.com/autobook/autobook/autobook_91.html#SEC91 for details
#
# - If interfaces have been changed or added, but binary compatibility
#   has been preserved, change:
#      CURRENT += 1
#      REVISION = 0
#      AGE += 1
# - If binary compatibility has been broken (eg removed or changed
#   interfaces), change:
#      CURRENT += 1
#      REVISION = 0
#      AGE = 0
# - If the interface is the same as the previous version, but bugs are
#   fixed, change:
#      REVISION += 1
lt_current = '1'
lt_revision = '1'
lt_age = '0'
lt_version = '@0@.@1@.@2@'.format(lt_current, lt_age, lt_revision)

# get suported warning flags
test_args = [
  '-fstack-protector-strong',
  '-Waggregate-return',
  '-Wunused',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wdeclaration-after-statement',
  '-Wempty-body',
  '-Wextra',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Winit-self',
  '-Winline',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-discarded-qualifiers',
  '-Wno-missing-field-initializers',
  '-Wno-strict-aliasing',
  '-Wno-suggest-attribute=format',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Woverride-init',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wreturn-type',
  '-Wshadow',
  '-Wsign-compare',
  '-Wstrict-aliasing',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wtype-limits',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused-but-set-variable',
  '-Wwrite-strings'
]
cc = meson.get_compiler('c')
foreach arg: test_args
  if cc.has_argument(arg)
    add_project_arguments(arg, language : 'c')
  endif
endforeach

# enable full RELRO where possible
# FIXME: until https://github.com/mesonbuild/meson/issues/1140 is fixed
global_link_args = []
test_link_args = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
]
foreach arg: test_link_args
  if cc.has_argument(arg)
    global_link_args += arg
  endif
endforeach
add_global_link_arguments(
  global_link_args,
  language: 'c'
)

gio = dependency('gio-2.0', version : '>= 2.45.8')
gmodule = dependency('gmodule-2.0')
giounix = dependency('gio-unix-2.0', version : '>= 2.45.8')
polkit = dependency('polkit-gobject-1', version : '>= 0.103')
gcab = dependency('libgcab-1.0')
gudev = dependency('gudev-1.0')
appstream_glib = dependency('appstream-glib', version : '>= 0.5.10')
gusb = dependency('gusb', version : '>= 0.2.9')
sqlite = dependency('sqlite3')
libarchive = dependency('libarchive')
valgrind = dependency('valgrindXXX', required: false)
soup = dependency('libsoup-2.4', version : '>= 2.51.92')
gpgme = cc.find_library('gpgme')
gpgerror = cc.find_library('libgpg-error')
libm = cc.find_library('libm', required: false)

if valgrind.found()
  conf.set('HAVE_VALGRIND', '1')
endif

if get_option('enable-colorhug')
  colorhug = dependency('colorhug', version : '>= 1.2.12')
  conf.set('HAVE_COLORHUG', '1')
endif

if get_option('enable-libelf')
  libelf = dependency('libelf')
  conf.set('HAVE_LIBELF', '1')
endif

if get_option('enable-uefi')
  fwup = dependency('fwup', version : '>= 5')
  conf.set('HAVE_UEFI', '1')
  conf.set('HAVE_UEFI_UNLOCK', '1')
  conf.set('HAVE_UEFI_GUID', '1')
endif

if get_option('enable-dell')
  libsmbios_c = dependency('libsmbios_c', version : '>= 2.3.0')
  efivar = dependency('efivar')
  conf.set('HAVE_DELL', '1')
endif

if get_option('enable-thunderbolt')
  thunderbolt = dependency('libtbtfwu', version : '>= 1', required: false)
endif

gnome = import('gnome')
i18n = import('i18n')

plugin_dir = join_paths(get_option('prefix'),
                        get_option('libdir'),
                        'fwupd-plugins-2')
conf.set_quoted('PLUGINDIR', plugin_dir)

conf.set_quoted('SYSCONFDIR',
                join_paths(get_option('prefix'),
                           get_option('sysconfdir')))
conf.set_quoted('LIBEXECDIR',
                join_paths(get_option('prefix'),
                           get_option('libexecdir')))
conf.set_quoted('LOCALSTATEDIR',
                join_paths(get_option('prefix'),
                           get_option('localstatedir')))

conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('LOCALEDIR', get_option('localedir'))
configure_file(
  output : 'config.h',
  configuration : conf
)

plugin_deps = []
plugin_deps += appstream_glib
plugin_deps += gio
plugin_deps += giounix
plugin_deps += gmodule
plugin_deps += gusb

subdir('data')
subdir('docs')
subdir('libdfu')
subdir('libfwupd')
subdir('plugins')
subdir('po')
subdir('policy')
subdir('src')

archiver = find_program('git', required : false)
if archiver.found()
  run_target('dist',
    # git config tar.tar.xz.command "xz -c"
    command: [
      'git', 'archive',
      '--prefix=' + meson.project_name() + '-' + meson.project_version() + '/',
      'HEAD',
      '--format=tar.xz',
      '--output',
      meson.project_name() + '-' + meson.project_version() + '.tar.xz'
    ]
  )
else
  message('git not found, you will not be able to run `ninja dist`')
endif

meson.add_install_script('meson_post_install.sh')
