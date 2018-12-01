project('fwupd', 'c',
  version : '1.2.4',
  license : 'LGPL-2.1+',
  meson_version : '>=0.47.0',
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
warning_flags = [
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
  '-Wincompatible-pointer-types-discards-qualifiers',
  '-Winit-self',
  '-Wlogical-op',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-cast-function-type',
  '-Wno-error=cpp',
  '-Wno-unknown-pragmas',
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
  '-Wvla',
  '-Wwrite-strings'
]
cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(warning_flags), language : 'c')

# enable full RELRO where possible
# FIXME: until https://github.com/mesonbuild/meson/issues/1140 is fixed
global_link_args = []
test_link_args = [
  '-Wl,-z,relro',
  '-Wl,-z,defs',
  '-Wl,-z,now',
]
foreach arg: test_link_args
  if cc.has_link_argument(arg)
    global_link_args += arg
  endif
endforeach
add_global_link_arguments(
  global_link_args,
  language: 'c'
)

# Needed for realpath(), syscall(), cfmakeraw(), etc.
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')

# do not use deprecated symbols or defines internally
add_project_arguments('-DFWUPD_DISABLE_DEPRECATED', language : 'c')

# needed for symlink() and BYTE_ORDER
add_project_arguments('-D_BSD_SOURCE', language : 'c')
add_project_arguments('-D_XOPEN_SOURCE=700', language : 'c')

prefix = get_option('prefix')

bindir = join_paths(prefix, get_option('bindir'))
libdir = join_paths(prefix, get_option('libdir'))
datadir = join_paths(prefix, get_option('datadir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
sysconfdir = join_paths(prefix, get_option('sysconfdir'))
localstatedir = join_paths(prefix, get_option('localstatedir'))
mandir = join_paths(prefix, get_option('mandir'))
localedir = join_paths(prefix, get_option('localedir'))

gio = dependency('gio-2.0', version : '>= 2.45.8')
if gio.version().version_compare ('>= 2.55.0')
  conf.set('HAVE_GIO_2_55_0', '1')
endif
gmodule = dependency('gmodule-2.0')
giounix = dependency('gio-unix-2.0', version : '>= 2.45.8')
gudev = dependency('gudev-1.0')
if gudev.version().version_compare('>= 232')
  conf.set('HAVE_GUDEV_232', '1')
endif
libxmlb = dependency('xmlb', version : '>= 0.1.5', fallback : ['libxmlb', 'libxmlb_dep'])
gusb = dependency('gusb', version : '>= 0.2.9')
sqlite = dependency('sqlite3')
libarchive = dependency('libarchive')
libjsonglib = dependency('json-glib-1.0', version : '>= 1.1.1')
valgrind = dependency('valgrind', required: false)
soup = dependency('libsoup-2.4', version : '>= 2.51.92')
if get_option('daemon')
  polkit = dependency('polkit-gobject-1', version : '>= 0.103')
  if polkit.version().version_compare('>= 0.114')
    conf.set('HAVE_POLKIT_0_114', '1')
  endif
  udevdir = get_option('udevdir')
  if udevdir == ''
    udev = dependency('udev')
    udevdir = udev.get_pkgconfig_variable('udevdir')
  endif
endif
if get_option('pkcs7')
  gnutls = dependency('gnutls', version : '>= 3.4.4.1')
  conf.set('ENABLE_PKCS7', '1')
endif
if get_option('gpg')
  gpgme = cc.find_library('gpgme')
  gpgerror = cc.find_library('gpg-error')
  conf.set('ENABLE_GPG', '1')
endif
libm = cc.find_library('m', required: false)
uuid = dependency('uuid')
libgcab = dependency('libgcab-1.0')
if libgcab.version().version_compare('>= 0.8')
  conf.set('HAVE_GCAB_0_8', '1')
endif
if libgcab.version().version_compare('>= 1.0')
  conf.set('HAVE_GCAB_1_0', '1')
endif
gcab = find_program('gcab', required : true)
bashcomp = dependency('bash-completion', required: false)
python3 = find_program('python3')

if valgrind.found()
  conf.set('HAVE_VALGRIND', '1')
endif

if get_option('plugin_redfish')
  efivar = dependency('efivar')
endif

if get_option('plugin_altos')
  libelf = dependency('libelf')
endif

if get_option('plugin_uefi')
  cairo = dependency('cairo')
  fontconfig = cc.find_library('fontconfig')
  freetype = cc.find_library('freetype')
  efivar = dependency('efivar', version : '>= 33')
  conf.set_quoted('EFIVAR_LIBRARY_VERSION', efivar.version())
  efiboot = dependency('efiboot')
  objcopy = find_program ('objcopy')
  readelf = find_program ('readelf')

  efi_app_location = join_paths(libexecdir, 'fwupd', 'efi')
  conf.set_quoted ('EFI_APP_LOCATION', efi_app_location)

  efi_arch = host_machine.cpu_family()
  if efi_arch == 'x86'
          EFI_MACHINE_TYPE_NAME = 'ia32'
          gnu_efi_arch = 'ia32'
  elif efi_arch == 'x86_64'
          EFI_MACHINE_TYPE_NAME = 'x64'
          gnu_efi_arch = 'x86_64'
  elif efi_arch == 'arm'
          EFI_MACHINE_TYPE_NAME = 'arm'
          gnu_efi_arch = 'arm'
  elif efi_arch == 'aarch64'
          EFI_MACHINE_TYPE_NAME = 'aa64'
          gnu_efi_arch = 'aarch64'
  else
          EFI_MACHINE_TYPE_NAME = ''
          gnu_efi_arch = ''
  endif
  conf.set_quoted('EFI_MACHINE_TYPE_NAME', EFI_MACHINE_TYPE_NAME)
  r = run_command([python3, 'po/test-deps'])
  if r.returncode() != 0
    error(r.stdout())
  endif
endif

if get_option('plugin_dell')
  libsmbios_c = dependency('libsmbios_c', version : '>= 2.4.0')
  efivar = dependency('efivar')
  conf.set('HAVE_DELL', '1')
  if not get_option('plugin_uefi')
    error('plugin_dell also needs plugin_uefi to work')
  endif
endif

if get_option('plugin_modem_manager')
  libmm_glib = dependency('mm-glib', version : '>= 1.9.1')
  add_project_arguments('-DMM_REQUIRED_VERSION="1.9.1"', language : 'c')
endif

if get_option('plugin_nvme')
  efivar = dependency('efivar')
  if not cc.has_header('linux/nvme_ioctl.h')
    error('NVMe support requires kernel >= 4.4')
  endif
endif

if get_option('plugin_synaptics')
    conf.set('HAVE_SYNAPTICS', '1')
endif

if get_option('plugin_thunderbolt')
  umockdev = dependency('umockdev-1.0', required: false)
  conf.set('HAVE_THUNDERBOLT', '1')
endif

if get_option('systemd')
  systemd = dependency('systemd', version : '>= 211')
  conf.set('HAVE_SYSTEMD' , '1')
endif

if get_option('consolekit')
  conf.set('HAVE_CONSOLEKIT' , '1')
endif

systemdunitdir = get_option('systemdunitdir')
if systemdunitdir == '' and get_option('systemd')
  systemdunitdir = systemd.get_pkgconfig_variable('systemdsystemunitdir')
endif

gnome = import('gnome')
i18n = import('i18n')

plugin_dir = join_paths(libdir, 'fwupd-plugins-3')

conf.set_quoted('BINDIR', bindir)
conf.set_quoted('LIBEXECDIR', libexecdir)
conf.set_quoted('DATADIR', datadir)
conf.set_quoted('LOCALSTATEDIR', localstatedir)
conf.set_quoted('SYSCONFDIR', sysconfdir)
conf.set_quoted('PLUGINDIR', plugin_dir)

conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('LOCALEDIR', localedir)
configure_file(
  output : 'config.h',
  configuration : conf
)

plugin_deps = []
plugin_deps += libxmlb
plugin_deps += gio
plugin_deps += giounix
plugin_deps += gmodule
plugin_deps += gusb
plugin_deps += soup
plugin_deps += libarchive
plugin_deps += gudev

subdir('data')
if get_option('gtkdoc')
  gtkdocscan = find_program('gtkdoc-scan', required : true)
  subdir('docs')
endif
subdir('libfwupd')
subdir('po')
if get_option('daemon')
  subdir('policy')
endif
subdir('src')
subdir('plugins')
subdir('contrib')

if get_option('systemd') and get_option('daemon')
  meson.add_install_script('meson_post_install.sh', systemdunitdir, localstatedir)
endif
