project('fwupd', 'c',
  version : '1.6.1',
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

# get source version, falling back to package version
git = find_program('git', required : false)
if git.found()
  source_version = run_command(git, 'describe').stdout().strip()
  if source_version == ''
    source_version = fwupd_version
  endif
else
  source_version = fwupd_version
endif
conf.set_quoted('SOURCE_VERSION', source_version)

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
libfwupd_lt_current = '2'
libfwupd_lt_revision = '0'
libfwupd_lt_age = '0'
libfwupd_lt_version = '@0@.@1@.@2@'.format(libfwupd_lt_current, libfwupd_lt_age, libfwupd_lt_revision)
libfwupdplugin_lt_current = '2'
libfwupdplugin_lt_revision = '0'
libfwupdplugin_lt_age = '0'
libfwupdplugin_lt_version = '@0@.@1@.@2@'.format(libfwupdplugin_lt_current, libfwupdplugin_lt_age, libfwupdplugin_lt_revision)

# get supported warning flags
warning_flags = [
  '-Waggregate-return',
  '-Wunused',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wdeclaration-after-statement',
  '-Wdiscarded-qualifiers',
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
  '-Wmaybe-uninitialized',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-cast-function-type',
  '-Wno-address-of-packed-member',  # incompatible with g_autoptr()
  '-Wno-unknown-pragmas',
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
  '-Wunused-variable',
  '-Wvla',
  '-Wwrite-strings'
]
cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(warning_flags), language : 'c')

# use honggfuzz for parallel persistent fuzzing
honggfuzz = find_program('honggfuzz', required: false)
if cc.has_function('HF_ITER')
  conf.set('HAVE_HF_ITER', '1')
endif

if not meson.is_cross_build()
  add_project_arguments('-fstack-protector-strong', language : 'c')
endif

# enable full RELRO where possible
# FIXME: until https://github.com/mesonbuild/meson/issues/1140 is fixed
global_link_args = []
test_link_args = [
  '-Wl,-z,relro',
  '-Wl,-z,defs',
  '-Wl,-z,now',
  '-Wl,-z,ibt,-z,shstk',
]
foreach arg: test_link_args
  if cc.has_link_argument(arg)
    global_link_args += arg
  endif
endforeach
add_project_link_arguments(
  global_link_args,
  language: 'c'
)

add_project_arguments('-DFWUPD_COMPILATION', language : 'c')

# Needed for realpath(), syscall(), cfmakeraw(), etc.
add_project_arguments('-D_DEFAULT_SOURCE', language : 'c')

# do not use deprecated symbols or defines internally
add_project_arguments('-DFWUPD_DISABLE_DEPRECATED', language : 'c')

# needed for symlink() and BYTE_ORDER
add_project_arguments('-D_BSD_SOURCE', language : 'c')
add_project_arguments('-D__BSD_VISIBLE', language : 'c')
add_project_arguments('-D_XOPEN_SOURCE=700', language : 'c')

# needed for memfd_create()
add_project_arguments('-D_GNU_SOURCE', language : 'c')

# sanity check
if get_option('build') == 'all'
  build_standalone = true
  build_daemon = true
elif get_option('build') == 'standalone'
  build_standalone = true
  build_daemon = false
elif get_option('build') == 'library'
  build_standalone = false
  build_daemon = false
endif

prefix = get_option('prefix')

bindir = join_paths(prefix, get_option('bindir'))
libdir = join_paths(prefix, get_option('libdir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
#this ends up in compiled code, ignore prefix
if host_machine.system() == 'windows'
  sysconfdir = get_option('sysconfdir')
  localstatedir = get_option('localstatedir')
  datadir = get_option('datadir')
  installed_test_bindir = get_option('libexecdir')
  installed_test_datadir = get_option('datadir')
else
  datadir = join_paths(prefix, get_option('datadir'))
  sysconfdir = join_paths(prefix, get_option('sysconfdir'))
  localstatedir = join_paths(prefix, get_option('localstatedir'))
  installed_test_bindir = join_paths(libexecdir, 'installed-tests', meson.project_name())
  installed_test_datadir = join_paths(datadir, 'installed-tests', meson.project_name())
endif
mandir = join_paths(prefix, get_option('mandir'))
localedir = join_paths(prefix, get_option('localedir'))

diffcmd = find_program('diff')
gio = dependency('gio-2.0', version : '>= 2.45.8')
giounix = dependency('gio-unix-2.0', version : '>= 2.45.8', required: false)
if giounix.found()
  conf.set('HAVE_GIO_UNIX', '1')
endif
if gio.version().version_compare ('>= 2.55.0')
  conf.set('HAVE_GIO_2_55_0', '1')
endif
if build_standalone
gmodule = dependency('gmodule-2.0')
if get_option('gudev')
  gudev = dependency('gudev-1.0', version : '>= 232')
  conf.set('HAVE_GUDEV', '1')
else
  gudev = dependency('', required : false)
endif
if get_option('bluez')
  conf.set('HAVE_BLUEZ', '1')
endif
libxmlb = dependency('xmlb', version : '>= 0.1.13', fallback : ['libxmlb', 'libxmlb_dep'])
if get_option('gusb')
  gusb = dependency('gusb', version : '>= 0.3.5', fallback : ['gusb', 'gusb_dep'])
  conf.set('HAVE_GUSB', '1')
endif
sqlite = dependency('sqlite3')
if get_option('libarchive')
  libarchive = dependency('libarchive')
  conf.set('HAVE_LIBARCHIVE', '1')
endif
endif
libjcat = dependency('jcat', version : '>= 0.1.0', fallback : ['libjcat', 'libjcat_dep'])
libjsonglib = dependency('json-glib-1.0', version : '>= 1.1.1')
valgrind = dependency('valgrind', required: false)
if get_option('curl')
  libcurl = dependency('libcurl', version : '>= 7.56.0')
  conf.set('HAVE_LIBCURL', '1')
  if libcurl.version().version_compare('>= 7.62.0')
    conf.set('HAVE_LIBCURL_7_62_0', '1')
  endif
endif
if build_daemon
  if get_option('polkit')
    polkit = dependency('polkit-gobject-1', version : '>= 0.103')
    conf.set('HAVE_POLKIT', '1')
    if polkit.version().version_compare('>= 0.114')
      conf.set('HAVE_POLKIT_0_114', '1')
    endif
  conf.set_quoted ('POLKIT_ACTIONDIR', polkit.get_pkgconfig_variable('actiondir'))
  else
    warning('Polkit is disabled, the daemon will allow ALL client actions')
  endif
  udevdir = get_option('udevdir')
  if udevdir == '' and host_machine.system() == 'linux'
    udev = dependency('udev')
    udevdir = udev.get_pkgconfig_variable('udevdir')
  endif
endif
libm = cc.find_library('m', required: false)
libgcab = dependency('libgcab-1.0', version : '>= 1.0', fallback : ['gcab', 'gcab_dep'])
gcab = find_program('gcab', required : get_option('tests'))
bashcomp = dependency('bash-completion', required: false)
python3 = find_program('python3.8', 'python3', 'python3.9')

if get_option('gnutls')
  gnutls = dependency('gnutls', version : '>= 3.6.0')
  conf.set('HAVE_GNUTLS', '1')
else
  if get_option('plugin_uefi_pk')
    error('plugin_uefi_pk needs -Dgnutls=true to work')
  endif
  if get_option('plugin_synaptics_rmi')
    error('plugin_synaptics_rmi needs -Dgnutls=true to work')
  endif
endif

if get_option('lzma')
  lzma = dependency('liblzma')
  conf.set('HAVE_LZMA', '1')
else
  if get_option('plugin_intel_spi')
    error('plugin_intel_spi needs -Dlzma=true to work')
  endif
endif

platform_deps = []
if get_option('default_library') != 'static'
  if host_machine.system() == 'windows'
    platform_deps += cc.find_library('shlwapi')
  endif
  if host_machine.system() == 'freebsd'
    platform_deps += cc.find_library('efivar')
  endif
endif

if valgrind.found()
  conf.set('HAVE_VALGRIND', '1')
endif

if build_standalone and get_option('plugin_altos')
  libelf = dependency('libelf')
endif

host_cpu = host_machine.cpu_family()

if cc.has_header('sys/utsname.h')
  conf.set('HAVE_UTSNAME_H', '1')
endif
if cc.has_header('sys/ioctl.h')
  conf.set('HAVE_IOCTL_H', '1')
endif
if cc.has_header('errno.h')
  conf.set('HAVE_ERRNO_H', '1')
endif
if cc.has_header('sys/socket.h')
  conf.set('HAVE_SOCKET_H', '1')
endif
if cc.has_header('linux/ethtool.h')
  conf.set('HAVE_ETHTOOL_H', '1')
endif
if cc.has_header('linux/hidraw.h')
  conf.set('HAVE_HIDRAW_H', '1')
endif
if cc.has_header('sys/mman.h')
  conf.set('HAVE_MMAN_H', '1')
endif
if cc.has_header('poll.h')
  conf.set('HAVE_POLL_H', '1')
endif
if cc.has_header('fnmatch.h')
  conf.set('HAVE_FNMATCH_H', '1')
endif
if cc.has_header('kenv.h')
  conf.set('HAVE_KENV_H', '1')
endif
if cc.has_header('malloc.h')
  conf.set('HAVE_MALLOC_H', '1')
  if cc.has_function('malloc_trim', prefix: '#include <malloc.h>')
	 conf.set('HAVE_MALLOC_TRIM', '1')
  endif
endif
if cc.has_header('cpuid.h') and cc.has_header_symbol('cpuid.h', '__get_cpuid_count') and (host_cpu == 'x86' or host_cpu == 'x86_64')
  conf.set('HAVE_CPUID_H', '1')
else
  if get_option('plugin_msr')
    error('cpuid.h is required for -Dplugin_msr=true')
  endif
endif
if cc.has_function('getuid')
  conf.set('HAVE_GETUID', '1')
endif
if cc.has_function('realpath')
  conf.set('HAVE_REALPATH', '1')
endif
if cc.has_function('memmem')
  conf.set('HAVE_MEMMEM', '1')
endif
if cc.has_function('sigaction')
  conf.set('HAVE_SIGACTION', '1')
endif
if cc.has_function('memfd_create')
  conf.set('HAVE_MEMFD_CREATE', '1')
endif
if cc.has_header_symbol('locale.h', 'LC_MESSAGES')
  conf.set('HAVE_LC_MESSAGES', '1')
endif
if cc.has_header_symbol('fcntl.h', 'F_WRLCK')
  conf.set('HAVE_WRLCK', '1')
endif
if cc.has_header_symbol('fcntl.h', 'F_OFD_SETLK')
  conf.set('HAVE_OFD', '1')
endif
if cc.has_function('pwrite', args : '-D_XOPEN_SOURCE')
  conf.set('HAVE_PWRITE', '1')
endif

if build_standalone and get_option('plugin_tpm')
  tpm2tss = dependency('tss2-esys', version : '>= 2.0')
  conf.set('HAVE_TSS2', '1')
else
  tpm2tss = dependency('', required: false)
endif

if build_standalone and get_option('plugin_uefi_capsule')
  efiboot = dependency('efiboot')

  efi_app_location = join_paths(libexecdir, 'fwupd', 'efi')
  conf.set_quoted('EFI_APP_LOCATION', efi_app_location)

  if host_cpu == 'x86'
          EFI_MACHINE_TYPE_NAME = 'ia32'
  elif host_cpu == 'x86_64'
          EFI_MACHINE_TYPE_NAME = 'x64'
  elif host_cpu == 'arm'
          EFI_MACHINE_TYPE_NAME = 'arm'
  elif host_cpu == 'aarch64'
          EFI_MACHINE_TYPE_NAME = 'aa64'
  else
          EFI_MACHINE_TYPE_NAME = ''
  endif
  conf.set_quoted('EFI_MACHINE_TYPE_NAME', EFI_MACHINE_TYPE_NAME)

  if get_option('efi_binary')
    efi_binary = dependency ('fwupd-efi', fallback: ['fwupd-efi', 'fwupd_efi_dep'])
  endif

  if get_option('plugin_uefi_capsule_splash')
    r = run_command([python3, 'po/test-deps'])
    if r.returncode() != 0
      error(r.stdout())
    endif
  endif
endif

if build_standalone and get_option('plugin_dell')
  libsmbios_c = dependency('libsmbios_c', version : '>= 2.4.0')
  conf.set('HAVE_DELL', '1')
  if not get_option('plugin_uefi_capsule')
    error('plugin_dell also needs plugin_uefi_capsule to work')
  endif
endif

if build_standalone and get_option('plugin_modem_manager')
  libmm_glib = dependency('mm-glib', version : '>= 1.10.0')
  add_project_arguments('-DMM_REQUIRED_VERSION="1.10.0"', language : 'c')
  libqmi_glib = dependency('qmi-glib', version : '>= 1.23.1')
endif

if get_option('soup_session_compat')
    conf.set('SOUP_SESSION_COMPAT', '1')
endif

if build_standalone and get_option('plugin_nvme')
  if not cc.has_header('linux/nvme_ioctl.h')
    error('NVMe support requires kernel >= 4.4')
  endif
endif

if build_standalone and get_option('plugin_thunderbolt')
  umockdev = dependency('umockdev-1.0', required: false)
  conf.set('HAVE_THUNDERBOLT', '1')
endif

if build_standalone and get_option('plugin_flashrom')
  libflashrom = dependency('flashrom', fallback : ['flashrom', 'flashrom_dep'])
endif

if build_standalone and get_option('systemd')
  systemd = dependency('systemd', version : '>= 211')
  libsystemd = dependency('libsystemd')
  conf.set('HAVE_SYSTEMD' , '1')
  conf.set('HAVE_LOGIND' , '1')
  systemd_root_prefix = get_option('systemd_root_prefix')
  if systemd_root_prefix == ''
    systemdunitdir = systemd.get_pkgconfig_variable('systemdsystemunitdir')
    systemdsystempresetdir = systemd.get_pkgconfig_variable('systemdsystempresetdir')
    systemd_shutdown_dir = systemd.get_pkgconfig_variable('systemdshutdowndir')
    systemd_modules_load_dir = systemd.get_pkgconfig_variable('modulesloaddir')
  else
    systemdunitdir = systemd.get_pkgconfig_variable('systemdsystemunitdir', define_variable: ['rootprefix', systemd_root_prefix])
    systemdsystempresetdir = systemd.get_pkgconfig_variable('systemdsystempresetdir', define_variable: ['rootprefix', systemd_root_prefix])
    systemd_root_prefix_varname = 'root_prefix'
    if systemd.version().version_compare('< 246')
      systemd_root_prefix_varname = 'rootprefix'
    endif
    systemd_shutdown_dir = systemd.get_pkgconfig_variable('systemdshutdowndir', define_variable: [systemd_root_prefix_varname, systemd_root_prefix])
    systemd_modules_load_dir = systemd.get_pkgconfig_variable('modulesloaddir', define_variable: [systemd_root_prefix_varname, systemd_root_prefix])
  endif
else
  libsystemd = dependency('', required: false)
endif

if build_standalone and get_option('elogind')
  elogind = dependency('libelogind', version : '>= 211')
  conf.set('HAVE_LOGIND' , '1')
endif

if build_standalone and get_option('consolekit')
  conf.set('HAVE_CONSOLEKIT' , '1')
endif

if get_option('supported_build')
    conf.set('SUPPORTED_BUILD', '1')
endif

gnome = import('gnome')
i18n = import('i18n')

conf.set_quoted('FWUPD_BINDIR', bindir)
conf.set_quoted('FWUPD_LIBDIR', libdir)
conf.set_quoted('FWUPD_LIBEXECDIR', libexecdir)
conf.set_quoted('FWUPD_DATADIR', datadir)
conf.set_quoted('FWUPD_LOCALSTATEDIR', localstatedir)
conf.set_quoted('FWUPD_SYSCONFDIR', sysconfdir)
conf.set_quoted('FWUPD_LOCALEDIR', localedir)
conf.set_quoted('FWUPD_FUZZINGSRCDIR', join_paths(meson.source_root(), 'src', 'fuzzing'))

if build_standalone
if host_machine.system() == 'windows'
  plugin_dir = 'fwupd-plugins-3'
else
  plugin_dir = join_paths(libdir, 'fwupd-plugins-3')
endif
conf.set_quoted('FWUPD_PLUGINDIR', plugin_dir)
endif

# sanity check, otherwise there is not point building
if host_machine.system() == 'windows' and not get_option('gusb')
  error('-Dgusb=true is required for Windows build')
endif

conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('PACKAGE_NAME', meson.project_name())
conf.set_quoted('VERSION', meson.project_version())

motd_file = '85-fwupd'
motd_dir = 'motd.d'
conf.set_quoted('MOTD_FILE', motd_file)
conf.set_quoted('MOTD_DIR', motd_dir)

configure_file(
  output : 'config.h',
  configuration : conf
)

if build_standalone
  plugin_deps = []
  plugin_deps += libxmlb
  plugin_deps += gio
  plugin_deps += giounix
  plugin_deps += gmodule
  plugin_deps += gudev
  plugin_deps += libjsonglib
endif

if get_option('gusb')
  plugin_deps += gusb
endif

if get_option('libarchive')
  plugin_deps += libarchive
endif

root_incdir = include_directories('.')

if get_option('gtkdoc')
  gtkdocscan = find_program('gtkdoc-scan', required : true)
  subdir('docs')
endif
subdir('libfwupd')
if build_daemon and get_option('polkit')
  subdir('policy')
endif
if build_standalone
  subdir('data')
  subdir('po')
  subdir('libfwupdplugin')
  subdir('src')
  subdir('plugins')
  subdir('contrib')
endif

if get_option('systemd') and build_daemon
  meson.add_install_script('meson_post_install.sh', systemdunitdir, localstatedir)
endif

makensis = find_program('makensis', required : false)
if makensis.found()
  run_target(
    'makensis',
    command: [
      makensis,
      join_paths(meson.source_root(), 'contrib', 'setup-win32.nsi'),
  ])
endif

codespell = find_program('codespell', required : false)
if codespell.found()
run_target('codespell',
  command: [
    codespell,
    meson.source_root(),
    '--write-changes',
    '--builtin', 'clear,informal,en-GB_to_en-US',
    '--skip', '*.po,*.csv,*.svg,*.p7c,subprojects,.git,pcrs,build*,.ossfuzz',
    '--ignore-words-list', 'conexant,Conexant,gir,GIR,hsi,HSI,cancelled,Cancelled,te',
  ]
)
endif
