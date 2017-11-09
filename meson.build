project('fwupd', 'c',
  version : '1.0.1',
  license : 'LGPL-2.1+',
  meson_version : '>=0.37.0',
  default_options : ['warning_level=2', 'c_std=c99'],
)

fwupd_version = meson.project_version()
varr = fwupd_version.split('.')
fwupd_major_version = varr[0]
fwupd_minor_version = varr[1]
fwupd_micro_version = varr[2]

conf = configuration_data()
conf.set('FWUPD_MAJOR_VERSION', fwupd_major_version)
conf.set('FWUPD_MINOR_VERSION', fwupd_minor_version)
conf.set('FWUPD_MICRO_VERSION', fwupd_micro_version)
conf.set_quoted('PACKAGE_VERSION', fwupd_version)

archiver = find_program('git', required : false)
if archiver.found()
  result = run_command('git', 'describe')
  if result.returncode() == 0
    describe = result.stdout().strip()
    conf.set_quoted('FWUPD_GIT_DESCRIBE', describe)
  endif
endif

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
lt_current = '2'
lt_revision = '0'
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
  '-Wduplicated-branches',
  '-Wduplicated-cond',
  '-Wempty-body',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Winit-self',
  '-Wlogical-op',
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
  '-Wnull-dereference',
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
  '-Wunused-variable',
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

# ONLY for CI and only when using gcc, do not enable this for distro builds
if get_option('enable-werror') and meson.get_compiler('c').get_id() == 'gcc'
  add_project_arguments('-Werror', language : 'c')
  add_project_arguments('-Wno-error=cpp', language : 'c')
endif

# Needed for realpath(), syscall(), cfmakeraw(), etc.
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')

# do not use deprecated symbols or defines internally
add_project_arguments('-DFWUPD_DISABLE_DEPRECATED', language : 'c')

gio = dependency('gio-2.0', version : '>= 2.45.8')
gmodule = dependency('gmodule-2.0')
giounix = dependency('gio-unix-2.0', version : '>= 2.45.8')
polkit = dependency('polkit-gobject-1', version : '>= 0.103')
if polkit.version().version_compare('>= 0.114')
  conf.set('HAVE_POLKIT_0_114', '1')
endif
gudev = dependency('gudev-1.0')
if gudev.version().version_compare('>= 232')
  conf.set('HAVE_GUDEV_232', '1')
endif
appstream_glib = dependency('appstream-glib', version : '>= 0.6.13')
gusb = dependency('gusb', version : '>= 0.2.9')
sqlite = dependency('sqlite3')
libarchive = dependency('libarchive')
if meson.version().version_compare('>0.41.0')
  valgrind = dependency('valgrind', required: false)
else
  valgrind = dependency('valgrindXXX', required: false)
endif
soup = dependency('libsoup-2.4', version : '>= 2.51.92')
if get_option('enable-pkcs7')
  gnutls = dependency('gnutls')
  conf.set('ENABLE_PKCS7', '1')
endif
if get_option('enable-gpg')
  gpgme = cc.find_library('gpgme')
  gpgerror = cc.find_library('gpg-error')
  conf.set('ENABLE_GPG', '1')
endif
libm = cc.find_library('m', required: false)
udev = dependency('udev')
uuid = dependency('uuid')

if get_option('enable-uefi-labels')
  cairo = dependency('cairo')
  fontconfig = cc.find_library('fontconfig')
  freetype = cc.find_library('freetype')
  r = run_command('po/test-deps')
  if r.returncode() != 0
    error(r.stdout())
  endif
endif

if valgrind.found()
  conf.set('HAVE_VALGRIND', '1')
endif

if get_option('enable-colorhug')
  colorhug = dependency('colorhug', version : '>= 1.2.12')
  conf.set('HAVE_COLORHUG', '1')
endif

if get_option('enable-altos')
  libelf = dependency('libelf')
endif

if get_option('enable-uefi')
  fwup = dependency('fwup', version : '>= 5')
  if fwup.version().version_compare('>= 10')
    conf.set('HAVE_FWUP_GET_BGRT_INFO', '1')
  endif
endif

if get_option('enable-dell')
  libsmbios_c = dependency('libsmbios_c', version : '>= 2.3.0')
  efivar = dependency('efivar')
  fwup = dependency('fwup', version : '>= 5')
  conf.set('HAVE_DELL', '1')
endif

if get_option('enable-synaptics')
    conf.set('HAVE_SYNAPTICS', '1')
endif

if get_option('enable-thunderbolt')
  umockdev = dependency('umockdev-1.0', required: false)
  conf.set('HAVE_THUNDERBOLT', '1')
endif

if get_option('enable-systemd')
  systemd = dependency('systemd', version : '>= 231')
  conf.set('HAVE_SYSTEMD' , '1')
endif

if get_option('enable-consolekit')
  conf.set('HAVE_CONSOLEKIT' , '1')
endif

systemdunitdir = get_option('with-systemdunitdir')
if systemdunitdir == '' and get_option('enable-systemd')
  systemdunitdir = systemd.get_pkgconfig_variable('systemdsystemunitdir')
endif

udevdir = get_option('with-udevdir')
if udevdir == ''
  udevdir = udev.get_pkgconfig_variable('udevdir')
endif

gnome = import('gnome')
i18n = import('i18n')

plugin_dir = join_paths(get_option('prefix'),
                        get_option('libdir'),
                        'fwupd-plugins-3')

conf.set_quoted('BINDIR',
                join_paths(get_option('prefix'),
                           get_option('bindir')))
conf.set_quoted('LIBEXECDIR',
                join_paths(get_option('prefix'),
                           get_option('libexecdir')))

conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('LOCALEDIR', get_option('localedir'))
configure_file(
  output : 'config.h',
  configuration : conf
)

default_sysconfdir = get_option('sysconfdir')
if default_sysconfdir == 'etc'
  message('sysconfdir of etc makes no sense, using /etc')
  default_sysconfdir = '/etc'
endif

localstatedir = join_paths(get_option('prefix'),
                           get_option('localstatedir'))

plugin_deps = []
plugin_deps += appstream_glib
plugin_deps += gio
plugin_deps += giounix
plugin_deps += gmodule
plugin_deps += gusb
plugin_deps += soup
plugin_deps += libarchive

subdir('data')
subdir('docs')
subdir('libfwupd')
subdir('po')
subdir('policy')
subdir('src')
subdir('plugins')
subdir('contrib')

if meson.version().version_compare('<0.41.0')
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
endif

if get_option('enable-systemd')
  meson.add_install_script('meson_post_install.sh', systemdunitdir, localstatedir)
endif
