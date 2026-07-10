# Init. {{{

project(
  'curl',
  'c',
  version: '8.10.1',
  meson_version: '>=0.60.0',
)

fs = import('fs')
pkg = import('pkgconfig')
cc = meson.get_compiler('c')
cdata = configuration_data()

sys_deps = []
lib_deps = []
public_args = []

# Constants. {{{

CURL_PUBLIC_INC = include_directories('include')
CURL_INTERNAL_INC = include_directories('lib')
CURL_VERSION = cc.get_define(
  'LIBCURL_VERSION',
  include_directories: CURL_PUBLIC_INC,
  prefix: '#include"curl/curlver.h"',
).strip('"')
CURL_VERSION_NUM = cc.get_define(
  'LIBCURL_VERSION_NUM',
  include_directories: CURL_PUBLIC_INC,
  prefix: '#include"curl/curlver.h"',
)

if CURL_VERSION != meson.project_version()
  error(
    'versions mismatch, meson has @0@, while `culver.h` reports @1@!'.format(
      meson.project_version(),
      CURL_VERSION,
    ),
  )
endif

WINDOWS_ONLY_ERROR = 'only supported on Windows'
MACOS_ONLY_ERROR = 'only supported on macOS'

MISSING_LIBS_ERROR = 'required libraries missing'

SSL_DISABLED_ERROR = 'feature ssl is disabled'
HTTP_DISABLED_ERROR = 'feature http is disabled'

DISABLED_OPT = get_option('_disabled')
ENABLED_OPT = get_option('_enabled')

# }}}

# Basic feature options. {{{
# (not dependent on checks)

foreach _opt : [
  'aws',
  'basic-auth',
  'bearer-auth',
  'bindlocal',
  'cookies',
  'dict',
  'digest-auth',
  'doh',
  'file',
  'ftp',
  'getoptions',
  'gopher',
  'imap',
  'kerberos-auth',
  'libcurl-option',
  'mime',
  'mqtt',
  'negotiate-auth',
  'netrc',
  'parsedate',
  'pop3',
  'progress-meter',
  'proxy',
  'rtsp',
  'sha512_256',
  'shuffle-dns',
  'smtp',
  'socketpair',
  'telnet',
  'tftp',
  'verbose-strings',
]
  set_variable(_opt.underscorify() + '_opt', get_option(_opt))
endforeach

debug_opt = get_option('debug') ? ENABLED_OPT : DISABLED_OPT
trackmemory_opt = get_option('curldebug') ? ENABLED_OPT : DISABLED_OPT

# }}}

# }}}

# Platform dependencies. {{{

if host_machine.system() == 'windows'
  # Determine Windows version.
  _WIN32_WINNT = cc.get_define(
    '_WIN32_WINNT',
    prefix: '''
    #define NOGDI
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    ''',
  )
  _WIN32_WINNT = _WIN32_WINNT != '' ? cc.compute_int(_WIN32_WINNT) : 0
  if _WIN32_WINNT < 0x501
    error('only Windows XP or newer is supported')
  endif
  host_vista_or_newer = _WIN32_WINNT >= 0x600
  # For `BCryptGenRandom()`.
  sys_deps += cc.find_library(
    'bcrypt',
    required: false,
  )
  sys_deps += cc.find_library('ws2_32')
else
  host_vista_or_newer = false
endif

# For `clock_gettime`.
sys_deps += cc.find_library(
  'rt',
  required: false,
)

if host_machine.system() == 'darwin'
  sys_deps += dependency(
    'appleframeworks',
    modules: ['CoreFoundation', 'SystemConfiguration'],
  )
endif

threads_dep = dependency(
  'threads',
  required: false,
)

# }}}

# Basic checks. {{{

check_args = []
check_headers = []

_headerlist = {
  'arpa/inet.h': ['getpeername', 'getsockname', 'inet_ntop', 'inet_pton'],
  'bsd/unistd.h': ['setmode'],
  'dirent.h': ['opendir'],
  'fcntl.h': ['fcntl'],
  'fnmatch.h': ['fnmatch'],
  'ifaddrs.h': [],
  'io.h': [],
  'krb.h': [],
  'libgen.h': ['basename'],
  'linux/tcp.h': [],
  'locale.h': ['setlocale'],
  'net/if.h': ['if_nametoindex'],
  'netdb.h': ['freeaddrinfo', 'getaddrinfo', 'gethostbyname_r'],
  'netinet/in.h': [],
  'netinet/in6.h': [],
  'netinet/tcp.h': [],
  'netinet/udp.h': [],
  'pem.h': [],
  'poll.h': [],
  'process.h': [],
  'proto/bsdsocket.h': [],
  'pthread.h': [],
  'pwd.h': ['getpwuid', 'getpwuid_r'],
  'sched.h': ['sched_yield'],
  'setjmp.h': ['sigsetjmp'],
  'signal.h': ['SIGALRM', 'sigaction', 'siginterrupt', 'signal'],
  'sockio.h': [],
  'stdatomic.h': [],
  'stdbool.h': [],
  'stddef.h': [],
  'stdint.h': [],
  'stdio.h': ['_fseeki64', 'fseeko', 'snprintf'],
  'stdlib.h': ['_strtoi64', 'arc4random', 'strtoll'],
  'string.h': ['strcmpi', 'strdup', 'strerror_r', 'stricmp', 'strtok_r'],
  'strings.h': ['strcasecmp'],
  'stropts.h': [],
  'sys/eventfd.h': ['eventfd'],
  'sys/filio.h': [],
  'sys/ioctl.h': [],
  'sys/param.h': [],
  'sys/poll.h': [],
  'sys/resource.h': ['getrlimit', 'setrlimit'],
  'sys/select.h': ['select'],
  'sys/socket.h': ['sa_family_t', 'sendmsg', 'socket', 'socketpair'],
  'sys/sockio.h': [],
  'sys/stat.h': [],
  'sys/time.h': ['gettimeofday', 'utimes'],
  'sys/types.h': [],
  'sys/un.h': [],
  'sys/utime.h': [],
  'sys/utsname.h': ['uname'],
  'sys/wait.h': [],
  'sys/xattr.h': ['fsetxattr'],
  'termio.h': [],
  'termios.h': [],
  'time.h': [
    'CLOCK_MONOTONIC',
    'CLOCK_MONOTONIC_RAW',
    'clock_gettime',
    'gmtime_r',
    'strftime',
  ],
  'unistd.h': [
    'alarm',
    'ftruncate',
    'geteuid',
    'gethostname',
    'getpass_r',
    'getppid',
    'pipe',
    'setmode',
  ],
  'utime.h': ['utime'],
}

if host_machine.system() == 'darwin'
  _headerlist += {
    'mach/mach_time.h': ['mach_absolute_time'],
  }
elif host_machine.system() == 'windows'
  _headerlist += {
    'windows.h': [],
    'winsock.h': ['closesocket', 'gethostname', 'getsockname'],
    'winsock2.h': [
      'ADDRESS_FAMILY',
      'gethostname',
      'getsockname',
      'select',
      'socket',
    ],
    'ws2tcpip.h': ['freeaddrinfo', 'getaddrinfo', 'inet_ntop', 'inet_pton'],
  }
endif

foreach _header, _symlist : _headerlist
  _var = 'HAVE_' + _header.underscorify().to_upper()
  _has_header = cc.has_header(_header)
  cdata.set(_var, _has_header)
  if _has_header
    check_args += '-D' + _var
    check_headers += _header
  endif
  foreach _symbol : _symlist
    _flag = 'HAVE_' + _symbol.underscorify().to_upper()
    _available = (cdata.get(_flag, false)
or (_has_header and cc.has_header_symbol(_header, _symbol))
)
    cdata.set(_flag, _available)
  endforeach
endforeach

check_prefix = {}
foreach _name, _headerlist : {
  'stdinc': ['sys/types.h', 'stdint.h', 'stddef.h'],
  'timeinc': ['sys/time.h', 'time.h'],
}
  _prefix = []
  foreach _header : _headerlist
    if _header in check_headers
      _prefix += '#include<@0@>'.format(_header)
    endif
  endforeach
  check_prefix += {
    _name: '\n'.join(_prefix),
  }
endforeach

cdata.set('HAVE_LONGLONG', cc.has_type('long long'))

foreach _type, _prefix : {
  'int'          : check_prefix['stdinc'],
  'short'        : check_prefix['stdinc'],
  'long'         : check_prefix['stdinc'],
  'long double'  : check_prefix['stdinc'],
  '__int64'      : check_prefix['stdinc'],
  'off_t'        : check_prefix['stdinc'],
  'size_t'       : check_prefix['stdinc'],
  'ssize_t'      : check_prefix['stdinc'],
  'time_t'       : check_prefix['stdinc'],
  'curl_off_t'   : '#include"curl/system.h"',
  'curl_socket_t': '#include"curl/curl.h"',
}
  cdata.set(
    'SIZEOF_' + _type.underscorify().to_upper(),
    cc.sizeof(
      _type,
      args: check_args,
      include_directories: CURL_PUBLIC_INC,
      prefix: _prefix,
    ),
  )
endforeach

if cdata.get('SIZEOF_SIZE_T') <= 0
  cdata.set('size_t', 'unsigned int')
  cdata.set('SIZEOF_SIZE_T', 'SIZEOF_INT')
endif

if cdata.get('SIZEOF_SSIZE_T') <= 0
  foreach _type : ['long', '__int64']
    _size = cdata.get('SIZEOF_' + _type.to_upper())
    if _size == cdata.get('SIZEOF_SIZE_T')
      cdata.set('ssize_t', _type)
      cdata.set('SIZEOF_SSIZE_T', _size)
      break
    endif
  endforeach
endif

cdata.set('HAVE_DECL_FSEEKO', cdata.get('HAVE_FSEEKO'))

cdata.set(
  'HAVE_GETIFADDRS',
  cc.has_function(
    'getifaddrs',
    prefix: '''
      #include <sys/types.h>
      #include <ifaddrs.h>
      ''',
  ),
)

if not cdata.get('HAVE_GETPWUID_R') and cc.has_function('getpwuid_r')
  cdata.set('HAVE_GETPWUID_R', true)
  cdata.set('HAVE_DECL_GETPWUID_R_MISSING', true)
endif

cdata.set('HAVE_SIGNAL', cdata.get('HAVE_SIGALRM') and cdata.get('HAVE_SIGNAL'))

cdata.set(
  'HAVE_SUSECONDS_T',
  cc.has_type(
    'suseconds_t',
    prefix: '#include<sys/time.h>',
  ),
)

# }}}

# Features & protocols. {{{

# Windows. {{{

use_win32_crypto = (host_machine.system() == 'windows'
and cc.has_header_symbol(
  'wincrypt.h',
  'CryptAcquireContext',
  prefix: '#include<windows.h>',
)
)

if use_win32_crypto
  lib_deps += [cc.find_library('advapi32'), cc.find_library('crypt32')]
endif

sspi_opt = get_option('sspi').disable_auto_if(get_option('gss-api').enabled()).require(
  host_machine.system() == 'windows',
  error_message: WINDOWS_ONLY_ERROR,
)
cdata.set('USE_WINDOWS_SSPI', sspi_opt.allowed())

# }}}

# DNS. {{{

asynchdns_opt = get_option('asynchdns')
asynchdns_resolver = ''

_required = asynchdns_opt.disabled() ? asynchdns_opt : false
foreach _resolver : get_option('asynchdns-resolver')
  if _resolver == 'pthread'
    # Posix threaded resolver.
    _dep = (asynchdns_opt.allowed()
and cdata.get('HAVE_PTHREAD_H') ? threads_dep : disabler()
)
    _flag = 'USE_THREADS_POSIX'
  elif _resolver == 'win32'
    # Win32 threaded resolver.
    _dep = (asynchdns_opt.allowed()
and host_machine.system() == 'windows' ? threads_dep : disabler()
)
    _flag = 'USE_THREADS_WIN32'
  elif _resolver == 'ares'
    # Library: c-ares.
    _dep = dependency(
      'libcares',
      required: _required,
    )
    _flag = 'USE_ARES'
  else
    error('invalid Asynchronous DNS provider: ' + _provider)
  endif
  if _dep.found()
    asynchdns_resolver = _resolver
    cdata.set(_flag, true)
    lib_deps += _dep
    break
  endif
endforeach

asynchdns_opt = asynchdns_opt.require(
  asynchdns_resolver != '',
  error_message: 'no supported resolver',
)

# }}}

# Brotli. {{{

brotli_opt = get_option('brotli')
lib_deps += dependency(
  'libbrotlidec',
  required: brotli_opt,
)
brotli_opt = brotli_opt.require(lib_deps[-1].found())
cdata.set('HAVE_BROTLI', brotli_opt.allowed())

# }}}

# Form API. {{{

form_api_opt = get_option('form-api').require(
  mime_opt.allowed(),
  error_message: 'feature mime is disabled',
)

# }}}

# IPv6. {{{

ipv6_opt = get_option('ipv6').require(
  host_machine.system() == 'windows' or cc.has_members(
    'struct sockaddr_in6',
    'sin6_addr',
    'sin6_scope_id',
    prefix: '#include<netinet/in.h>',
  ),
  error_message: 'struct sockaddr_in6 not available',
)
cdata.set('USE_IPV6', ipv6_opt.allowed())

# }}}

# Large file. {{{

cdata.set('USE_WIN32_LARGE_FILES', host_machine.system() == 'windows')
largefile_opt = (cdata.get('USE_WIN32_LARGE_FILES')
or (cdata.get('SIZEOF_CURL_OFF_T') > 4
and cdata.get('SIZEOF_OFF_T') > 4)
? ENABLED_OPT : DISABLED_OPT
)

# }}}

# SSH. {{{

ssh_opt = get_option('ssh')
ssh_provider = ''

_required = ssh_opt.disabled() ? ssh_opt : false
foreach _provider : get_option('ssh-provider')
  _dep = dependency(
    _provider,
    required: _required,
  )
  if _dep.found()
    cdata.set('USE_' + _provider.to_upper(), true)
    ssh_provider = _provider
    lib_deps += _dep
    break
  endif
endforeach

ssh_opt = ssh_opt.require(
  ssh_provider != '',
  error_message: 'no supported provider',
)

scp_opt = ssh_opt
sftp_opt = ssh_opt

# }}}

# SSL. {{{

ssl_backends = []
ssl_opt = get_option('ssl')

# Windows secure channel. {{{

schannel_opt = get_option('schannel').disable_auto_if(ssl_backends.length() > 0).require(
  host_machine.system() == 'windows',
  error_message: WINDOWS_ONLY_ERROR,
).require(
  ssl_opt.allowed(),
  error_message: SSL_DISABLED_ERROR,
).require(
  sspi_opt.allowed(),
  error_message: 'feature sspi is disabled',
).require(
  use_win32_crypto,
  error_message: MISSING_LIBS_ERROR,
)

cdata.set('USE_SCHANNEL', schannel_opt.allowed())

ssl_backends += schannel_opt.allowed() ? 'schannel' : []

# }}}

# OpenSSL. {{{

openssl_opt = get_option('openssl').disable_auto_if(ssl_backends.length() > 0).require(
  ssl_opt.allowed(),
  error_message: SSL_DISABLED_ERROR,
)

lib_deps += dependency(
  'openssl',
  required: openssl_opt,
)
openssl_opt = openssl_opt.require(lib_deps[-1].found())
openssl_auto_load_config_opt = openssl_opt

cdata.set('USE_OPENSSL', openssl_opt.allowed())
ssl_backends += openssl_opt.allowed() ? 'openssl_opt' : []

_partial_dep = lib_deps[-1].partial_dependency(
  compile_args: true,
  includes: true,
)
_required = openssl_opt.disabled() ? openssl_opt : false

cdata.set(
  'HAVE_AWSLC',
  cc.has_header_symbol(
    'openssl/base.h',
    'OPENSSL_IS_AWSLC',
    dependencies: _partial_dep,
    required: _required,
  ),
)
cdata.set(
  'HAVE_OPENSSL_SRP',
  cc.has_header_symbol(
    'openssl/ssl.h',
    'SSL_CTX_set_srp_password',
    dependencies: _partial_dep,
    required: _required,
  ) and cc.has_header_symbol(
    'openssl/ssl.h',
    'SSL_CTX_set_srp_username',
    dependencies: _partial_dep,
    required: _required,
  ),
)
cdata.set(
  'HAVE_SSL_SET0_WBIO',
  cc.has_header_symbol(
    'openssl/ssl.h',
    'SSL_set0_wbio',
    dependencies: _partial_dep,
    required: _required,
  ),
)

# }}}

# Secure transport (macOS). {{{

secure_transport_opt = get_option('secure-transport').disable_auto_if(
  ssl_backends.length() > 0,
).require(
  ssl_opt.allowed(),
  error_message: SSL_DISABLED_ERROR,
).require(
  host_machine.system() == 'darwin',
  error_message: MACOS_ONLY_ERROR,
)

lib_deps += dependency(
  'appleframeworks',
  modules: 'Security',
  required: secure_transport_opt,
)
secure_transport_opt.require(lib_deps[-1].found())

cdata.set('USE_SECTRANSP', secure_transport_opt.allowed())

ssl_backends += secure_transport_opt.allowed() ? 'secure-transport' : []

if secure_transport_opt.allowed()
  warning('TLS library does not support TLS 1.3: secure-transport')
endif

# }}}

ssl_opt = ssl_opt.require(
  ssl_backends.length() > 0,
  error_message: 'no supported backend',
)

tls_srp_opt = get_option('tls-srp').require(cdata.get('HAVE_OPENSSL_SRP', false))
cdata.set('USE_TLS_SRP', tls_srp_opt.allowed())

ssl_default_backend = get_option('ssl-default-backend')
if ssl_default_backend == 'implicit'
  cdata.set('CURL_DEFAULT_SSL_BACKEND', false)
elif ssl_default_backend in ssl_backends
  cdata.set_quoted('CURL_DEFAULT_SSL_BACKEND', ssl_default_backend)
else
  error(
    'invalid default SSL backend: @0@ (available: @1@)'.format(
      ssl_default_backend,
      ', '.join(ssl_backends),
    ),
  )
endif

multissl_opt = ssl_backends.length() > 1 ? ENABLED_OPT : DISABLED_OPT
cdata.set('CURL_WITH_MULTI_SSL', multissl_opt.allowed())

# }}}

# GSASL. {{{

gsasl_opt = get_option('gsasl')
lib_deps += dependency(
  'libgsasl',
  required: gsasl_opt,
)
gsasl_opt = gsasl_opt.require(lib_deps[-1].found())
cdata.set('USE_GSASL', gsasl_opt.allowed())

# }}}

# GSS API. {{{

gss_api_opt = get_option('gss-api').require(
  not sspi_opt.allowed(),
  error_message: 'feature sspi is enabled',
)
gss_api_provider = ''

_required = gss_api_opt.disabled() ? gss_api_opt : false
foreach _provider : get_option('gss-api-provider')
  if _provider == 'gnu'
    # GNU GSS.
    _dep = dependency(
      'gss',
      disabler: true,
      required: _required,
    )
    cdata.set('HAVE_GSSGNU', _dep.found())
  elif _provider == 'mit'
    # MIT Kerberos.
    _dep = dependency(
      'mit-krb5-gssapi',
      disabler: true,
      required: _required,
    )
    _partial_dep = _dep.partial_dependency(
      compile_args: true,
      includes: true,
    )
    foreach _header : ['gssapi/gssapi.h', 'gssapi/gssapi_generic.h']
      cdata.set(
        'HAVE_' + _header.underscorify().to_upper(),
        cc.has_header(
          _header,
          dependencies: _partial_dep,
        ),
      )
    endforeach
    # Need at least <gssapi/gssapi.h>.
    if not cdata.get('HAVE_GSSAPI_GSSAPI_H', false)
      _dep = disabler()
    endif
  elif _provider == 'heimdal'
    # Heimdal.
    _dep = dependency(
      'heimdal-gssapi',
      disabler: true,
      required: _required,
    )
    _partial_dep = _dep.partial_dependency(
      compile_args: true,
      includes: true,
    )
    if not cc.has_header(
      'gssapi.h',
      dependencies: _partial_dep,
    )
      _dep = disabler()
    endif
  else
    error('invalid GSS-API provider: ' + _provider)
  endif
  if _dep.found()
    gss_api_provider = _provider
    lib_deps += _dep
    break
  endif
endforeach

gss_api_opt = gss_api_opt.require(
  gss_api_provider != '',
  error_message: 'no supported provider',
)
cdata.set('HAVE_GSSAPI', gss_api_opt.allowed())

# }}}

# HTTP. {{{

http_opt = get_option('http')

foreach _feat : ['alt-svc', 'headers-api', 'hsts', 'http2', 'http-auth', 'rtsp']
  set_variable(
    _feat.underscorify() + '_opt',
    get_option(_feat).require(
      http_opt.allowed(),
      error_message: HTTP_DISABLED_ERROR,
    ),
  )
endforeach

# HSTS support needs SSL.
hsts_opt = hsts_opt.require(
  ssl_opt.allowed(),
  error_message: SSL_DISABLED_ERROR,
)

# HTTP2.
lib_deps += dependency(
  'libnghttp2',
  required: http2_opt,
)
http2_opt = http2_opt.require(lib_deps[-1].found())
cdata.set('USE_NGHTTP2', http2_opt.allowed())

# HTTP3.
http3_opt = DISABLED_OPT.require(
  multissl_opt.disabled(),
  error_message: 'MultiSSL and HTTP/3 supports are exclusive',
)

# HTTPS proxy.
https_proxy_opt = ssl_opt.allowed() ? proxy_opt : DISABLED_OPT

# }}}

# IPFS / IPNS.
ipfs_opt = http_opt
ipns_opt = http_opt

# LDAP. {{{

ldap_opt = get_option('ldap')
ldap_provider = ''
have_ldap_ssl = false

_required = ldap_opt.disabled() ? ldap_opt : false
foreach _provider : get_option('ldap-provider')
  if _provider == 'openldap'
    _dep = dependency(
      'ldap',
      required: _required,
      version: '>= 2.4.48',
    )
    _flag = 'OPENLDAP'
  elif _provider == 'win32'
    _dep = (host_machine.system() == 'windows' ? cc.find_library(
      'wldap32',
      has_headers: 'winldap.h',
      required: _required,
    ) : disabler()
)
    _flag = 'WIN32_LDAP'
  else
    error('invalid LDAP provider: ' + _provider)
  endif
  if _dep.found()
    cdata.set('USE_' + _flag, true)
    ldap_provider = _provider
    lib_deps += _dep
    break
  endif
endforeach

ldap_opt = ldap_opt.require(
  ldap_provider != '',
  error_message: 'no supported provider',
)

ldaps_opt = get_option('ldaps').require(
  ldap_opt.allowed(),
  error_message: 'feature ldap is disabled',
).require(
  ldap_provider == 'win32' or ssl_opt.allowed(),
  error_message: 'need win32 LDAP provider or SSL support',
)
cdata.set('HAVE_LDAP_SSL', ldaps_opt.allowed())

# }}}

# NTLM. {{{

ntlm_opt = get_option('ntlm').require(
  use_win32_crypto or ssl_backends.length() > 0,
  error_message: MISSING_LIBS_ERROR,
)

# }}}

# Public Suffix List. {{{

psl_opt = get_option('psl')
lib_deps += dependency(
  'libpsl',
  required: psl_opt,
)
psl_opt = psl_opt.require(lib_deps[-1].found())
cdata.set('USE_LIBPSL', psl_opt.allowed())

# }}}

# RTMP. {{{

rtmp_opt = get_option('rtmp')
lib_deps += dependency(
  'librtmp',
  required: rtmp_opt,
)
rtmp_opt = rtmp_opt.require(lib_deps[-1].found())
cdata.set('USE_LIBRTMP', rtmp_opt.allowed())

# }}}

# Samba. {{{

smb_opt = get_option('smb').require(
  ntlm_opt.allowed(),
  error_message: 'feature ntlm is disabled',
).require(
  cdata.get('SIZEOF_CURL_OFF_T') > 4,
  error_message: 'sizeof (curl_off_t) <= 4',
)

smbs_opt = ssl_opt.allowed() ? smb_opt : DISABLED_OPT

# }}}

# Unix sockets. {{{

unixsockets_opt = get_option('unixsockets').require(
  host_machine.system() == 'windows' or cc.has_member(
    'struct sockaddr_un',
    'sun_path',
    prefix: '#include<sys/un.h>',
  ),
  error_message: 'struct sockaddr_un not available',
)

cdata.set('USE_UNIX_SOCKETS', unixsockets_opt.allowed())

# }}}

# IDN. {{{

idn_opt = get_option('idn')
idn_provider = ''

_required = idn_opt.disabled() ? idn_opt : false
foreach _provider : get_option('idn-provider')
  if _provider == 'appleidn'
    # Apple IDN.
    _dep = declare_dependency(
      dependencies: host_machine.system() == 'darwin' ? [
        cc.find_library(
          'icucore',
          disabler: true,
          has_headers: 'unicode/uidna.h',
          required: _required,
        ),
        cc.find_library(
          'iconv',
          disabler: true,
          has_headers: 'iconv.h',
          required: _required,
        ),
      ] : disabler(),
    )
    if cc.has_function(
      'uidna_openUTS46',
      dependencies: _dep,
    )
      cdata.set('USE_APPLE_IDN', true)
      idn_provider = 'appleidn'
      lib_deps += _dep
      break
    endif
  elif _provider == 'libidn2'
    # Library: libdn2.
    _dep = dependency(
      'libidn2',
      required: _required,
    )
    cdata.set('HAVE_LIBIDN2', _dep.found())
    cdata.set('HAVE_IDN2_H', _dep.found())
    if _dep.found()
      idn_provider = _provider
      lib_deps += _dep
      break
    endif
  elif _provider == 'winidn'
    # Windows IDN.
    _dep = (host_vista_or_newer ? cc.find_library(
      'normaliz',
      disabler: true,
      required: _required,
    ) : disabler()
)
    if cc.has_function(
      'IdnToUnicode',
      dependencies: _dep,
    )
      cdata.set('USE_WIN32_IDN', true)
      idn_provider = 'winidn'
      lib_deps += _dep
      break
    endif
  else
    error('invalid IDN provider: ' + _provider)
  endif
endforeach

idn_opt = idn_opt.require(
  idn_provider != '',
  error_message: 'no supported provider',
)

# }}}

# Web sockets. {{{

ws_opt = get_option('websockets').require(
  http_opt.allowed(),
  error_message: HTTP_DISABLED_ERROR,
).require(
  cdata.get('SIZEOF_CURL_OFF_T') > 4,
  error_message: 'curl_off_t is too small to enable WebSockets support',
)

cdata.set('USE_WEBSOCKETS', ws_opt.allowed())

wss_opt = ws_opt.allowed() and ssl_opt.allowed() ? ws_opt : DISABLED_OPT

# }}}

# ZLIB. {{{

libz_opt = get_option('libz')
lib_deps += dependency(
  'zlib',
  required: libz_opt,
)
libz_opt = libz_opt.require(lib_deps[-1].found())
cdata.set('HAVE_LIBZ', libz_opt.allowed())

# }}}

# ZSTD. {{{

zstd_opt = get_option('zstd')
lib_deps += dependency(
  'libzstd',
  required: zstd_opt,
  version: '>=1.0.0',
)
zstd_opt = zstd_opt.require(lib_deps[-1].found())
cdata.set('HAVE_ZSTD', zstd_opt.allowed())

# }}}

# }}}

# Advanced checks. {{{

_has_fcntl = cdata.get('HAVE_FCNTL')
_has_fsetxattr = cdata.get('HAVE_FSETXATTR')
_has_gethostbyname_r = cdata.get('HAVE_GETHOSTBYNAME_R')

# TODO: replace some of those with simpler checks.
foreach _spec : [
  ['HAVE_ATOMIC'],
  ['HAVE_BOOL_T'],
  ['HAVE_BUILTIN_AVAILABLE'],
  ['HAVE_FCNTL_O_NONBLOCK', _has_fcntl],
  ['HAVE_FSETXATTR_5', _has_fsetxattr],
  ['HAVE_FSETXATTR_6', _has_fsetxattr],
  ['HAVE_GETHOSTBYNAME_R_3', _has_gethostbyname_r],
  ['HAVE_GETHOSTBYNAME_R_3', _has_gethostbyname_r],
  ['HAVE_GETHOSTBYNAME_R_3_REENTRANT', _has_gethostbyname_r],
  ['HAVE_GETHOSTBYNAME_R_3_REENTRANT', _has_gethostbyname_r],
  ['HAVE_GETHOSTBYNAME_R_5', _has_gethostbyname_r],
  ['HAVE_GETHOSTBYNAME_R_5', _has_gethostbyname_r],
  ['HAVE_GETHOSTBYNAME_R_5_REENTRANT', _has_gethostbyname_r],
  ['HAVE_GETHOSTBYNAME_R_5_REENTRANT', _has_gethostbyname_r],
  ['HAVE_GETHOSTBYNAME_R_6', _has_gethostbyname_r],
  ['HAVE_GETHOSTBYNAME_R_6', _has_gethostbyname_r],
  ['HAVE_GETHOSTBYNAME_R_6_REENTRANT', _has_gethostbyname_r],
  ['HAVE_GETHOSTBYNAME_R_6_REENTRANT', _has_gethostbyname_r],
  ['HAVE_GLIBC_STRERROR_R'],
  ['HAVE_IN_ADDR_T'],
  ['HAVE_IOCTLSOCKET'],
  ['HAVE_IOCTLSOCKET_CAMEL'],
  ['HAVE_IOCTLSOCKET_CAMEL_FIONBIO'],
  ['HAVE_IOCTLSOCKET_FIONBIO'],
  ['HAVE_IOCTL_FIONBIO'],
  ['HAVE_IOCTL_SIOCGIFADDR'],
  ['HAVE_O_NONBLOCK'],
  ['HAVE_POSIX_STRERROR_R'],
  ['HAVE_SETSOCKOPT_SO_NONBLOCK'],
  ['STDC_HEADERS'],
]
  _test = _spec[0]
  _enabled = _spec.get(1, true)
  cdata.set(
    _test,
    _enabled and cc.links(
      files('CMake/CurlTests.c'),
      args: [check_args, '-D' + _test],
      dependencies: sys_deps,
      name: _test + ' test',
    ),
  )
endforeach

_source_epilogue = '#undef inline'

if host_machine.system() == 'windows'
  foreach _header : [
    # NOTE: do not reorder (`winsock2.h` must be included before `windows.h`).
    'winsock2.h',
    'windows.h',
  ]
    if _header in check_headers
      _source_epilogue += '\n#include<@0@>'.format(_header)
    endif
  endforeach
  _source_epilogue += '\n#ifndef WIN32_LEAN_AND_MEAN\n#define WIN32_LEAN_AND_MEAN\n#endif'
else
  foreach _header : [
    'proto/bsdsocket.h',
    'sys/types.h',
    'sys/socket.h',
    'sys/time.h',
    'time.h',
  ]
    if _header in check_headers
      _source_epilogue += '\n#include<@0@>'.format(_header)
    endif
  endforeach
endif

threadsafe_opt = (host_vista_or_newer
or cdata.get('HAVE_STDATOMIC_H')
? ENABLED_OPT : DISABLED_OPT
)

_have_clock_gettime = (cdata.get('HAVE_CLOCK_GETTIME')
and cc.has_function(
  'clock_gettime',
  dependencies: sys_deps,
)
)
cdata.set(
  'HAVE_CLOCK_GETTIME_MONOTONIC',
  _have_clock_gettime and cdata.get('HAVE_CLOCK_MONOTONIC'),
)
cdata.set(
  'HAVE_CLOCK_GETTIME_MONOTONIC_RAW',
  _have_clock_gettime and cdata.get('HAVE_CLOCK_MONOTONIC_RAW'),
)

cdata.set(
  'HAVE_GETADDRINFO_THREADSAFE',
  host_machine.system() == 'windows' or cc.compiles(
    '''
    #ifdef HAVE_SYS_SOCKET_H
    #  include<sys/socket.h>
    #endif
    #ifdef HAVE_NETDB_H
    #  include<netdb.h>
    #endif
    int main(void) {
      #ifndef h_errno
        force compilation error
      #endif
      #if defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE >= 200809L)
      #elif defined(_XOPEN_SOURCE) && (_XOPEN_SOURCE >= 700)
      #else
        force compilation error
      #endif
      h_errno = 2;
      if(0 != h_errno)
        return 1;
      return 0;
    }
    ''',
    args: check_args,
    dependencies: sys_deps,
    name: 'HAVE_GETADDRINFO_THREADSAFE test',
  ),
)

cdata.set(
  'HAVE_RECV',
  cc.links(
    _source_epilogue + '''
    int main(void) {
      recv(0, 0, 0, 0);
      return 0;
    }
    ''',
    args: check_args,
    dependencies: sys_deps,
    name: 'HAVE_RECV test',
  ),
)

cdata.set(
  'HAVE_SETMODE',
  cdata.get('HAVE_SETMODE') and cc.has_function('setmode'),
)

cdata.set(
  'HAVE_SOCKADDR_IN6_SIN6_SCOPE_ID',
  cc.compiles(
    '''
    #include <sys/types.h>
    #ifdef HAVE_WINSOCK2_H
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #else
    #include <netinet/in.h>
    #if defined (__TANDEM)
    # include <netinet/in6.h>
    #endif
    #endif
    int main(void) {
      struct sockaddr_in6 s;
      s.sin6_scope_id = 0;
      return 0;
    }
    ''',
    args: check_args,
    dependencies: sys_deps,
    name: 'HAVE_SOCKADDR_IN6_SIN6_SCOPE_ID test',
  ),
)

cdata.set(
  'HAVE_SEND',
  cc.links(
    _source_epilogue + '''
    int main(void) {
      send(0, 0, 0, 0);
      return 0;
    }
    ''',
    args: check_args,
    dependencies: sys_deps,
    name: 'HAVE_SEND test',
  ),
)

cdata.set(
  'HAVE_MSG_NOSIGNAL',
  cc.links(
    _source_epilogue + '''
    int main(void) {
      int flag = MSG_NOSIGNAL;
      (void)flag;
      return 0;
    }
    ''',
    name: 'HAVE_MSG_NOSIGNAL test',
  ),
)

cdata.set(
  'HAVE_STRUCT_TIMEVAL',
  cc.links(
    _source_epilogue + '''
    int main(void) {
      struct timeval ts;
      ts.tv_sec  = 0;
      ts.tv_usec = 0;
      (void)ts;
      return 0;
    }
    ''',
    name: 'HAVE_STRUCT_TIMEVAL test',
  ),
)

cdata.set(
  'HAVE_STRUCT_SOCKADDR_STORAGE',
  cc.sizeof(
    'struct sockaddr_storage',
    prefix: _source_epilogue,
  ) > 0,
)

if host_machine.system() != 'darwin'
  if meson.can_run_host_binaries()
    cdata.set(
      'HAVE_POLL_FINE',
      cc.run(
        '''
        #include <stdlib.h>
        #ifdef HAVE_SYS_POLL_H
        #  include <sys/poll.h>
        #elif  HAVE_POLL_H
        #  include <poll.h>
        #endif
        int main(void)
        {
          if(0 != poll(0, 0, 10)) {
            return 1; /* fail */
          }
          return 0;
        }
        ''',
        args: check_args,
        dependencies: sys_deps,
        name: 'HAVE_POLL_FINE test',
      ).returncode() == 0,
    )
  else
    cdata.set(
      'HAVE_POLL_FINE',
      cc.compiles(
        '''
        #include <stdlib.h>
        #ifdef HAVE_SYS_POLL_H
        #  include <sys/poll.h>
        #elif  HAVE_POLL_H
        #  include <poll.h>
        #endif
        int main(void)
        {
          #if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
            (void)poll(0, 0, 0);
          #else
            #error force compilation error
          #endif
          return 0;
        }
        ''',
        args: check_args,
        dependencies: sys_deps,
        name: 'HAVE_POLL_FINE test',
      ),
    )
  endif
endif

cdata.set(
  'HAVE_TIME_T_UNSIGNED',
  meson.can_run_host_binaries() and cc.run(
    '''
    #include <time.h>
    #include <limits.h>
    int main(void) {
      time_t t = -1;
      return (t < 0);
    }
    ''',
    name: 'HAVE_TIME_T_UNSIGNED test',
  ).returncode() == 0,
)

cdata.set(
  'HAVE_WRITABLE_ARGV',
  meson.can_run_host_binaries() and cc.run(
    '''
    int main(int argc, char **argv)
    {
      (void)argc;
      argv[0][0] = ' ';
      return (argv[0][0] == ' ')?0:1;
    }
    ''',
    name: 'HAVE_WRITABLE_ARGV test',
  ).returncode() == 0,
)

_required = cdata.get('HAVE_GETHOSTNAME') ? true : disabler()
foreach _arg1 : ['char *', 'unsigned char *', 'void *']
  foreach _arg2 : ['int', 'unsigned int', 'size_t']
    if (_required
and cc.compiles(
      _source_epilogue + '''
        #ifdef HAVE_WINDOWS_H
        #  ifndef WIN32_LEAN_AND_MEAN
        #    define WIN32_LEAN_AND_MEAN
        #  endif
        #  include <windows.h>
        #  if defined(HAVE_WINSOCK2_H)
        #    include <winsock2.h>
        #  elif defined(HAVE_WINSOCK_H)
        #    include <winsock.h>
        #  endif
        #endif
        #ifdef HAVE_SYS_TYPES_H
        #  include <sys/types.h>
        #endif
        #ifdef HAVE_UNISTD_H
        #  include <unistd.h>
        #endif
        #ifdef HAVE_WINDOWS_H
        #  define FUNCALLCONV __stdcall
        #else
        #  define FUNCALLCONV
        #endif
        extern int FUNCALLCONV gethostname(@0@, @1@);
        int main(void) {
          if(0 != gethostname(0, 0))
            return 1;
          return 0;
        }
        '''.format(_arg1, _arg2),
      args: check_args,
      name: 'gethostname(@0@, @1@)'.format(_arg1, _arg2),
    )
)
      cdata.set('GETHOSTNAME_TYPE_ARG2', _arg2)
      _required = disabler()
    endif
  endforeach
endforeach

# NEED_REENTRANT. {{{

_need_reentrant = false

_flaglist = [
  'HAVE_GETHOSTBYNAME_R_3',
  'HAVE_GETHOSTBYNAME_R_5',
  'HAVE_GETHOSTBYNAME_R_6',
]

foreach _flag : _flaglist
  if not cdata.get(_flag) and cdata.get(_flag + '_REENTRANT')
    _need_reentrant = true
  endif
endforeach

cdata.set('NEED_REENTRANT', _need_reentrant)

if _need_reentrant
  foreach _flag : _flaglist
    cdata.set(_flag, cdata.get(_flag + '_REENTRANT'))
  endforeach
endif

# }}}

# }}}.

# Finalize config. {{{

# OS {{{

# Some parts of the testsuite rely on a particular
# formatting, e.g. for detecting the Windows build.
if cc.get_argument_syntax() == 'gcc'
  _os = run_command(
    cc.cmd_array(),
    '-dumpmachine',
    check: true,
  ).stdout().strip()
else
  _os = '-'.join(host_machine.cpu(), host_machine.system())
endif

cdata.set_quoted('OS', _os)

# }}}

# Compile flags. {{{

# Symbols visibility.
if (cc.has_argument('-fvisibility=hidden')
and cc.has_function_attribute('visibility:default')
)
  cdata.set('CURL_EXTERN_SYMBOL', '__attribute__ ((__visibility__ ("default")))')
  curl_symbols_hiding_flags = ['-DCURL_HIDDEN_SYMBOLS']
  curl_symbols_hiding_visibility = 'hidden'
else
  cdata.set('CURL_EXTERN_SYMBOL', '')
  curl_symbols_hiding_flags = []
  curl_symbols_hiding_visibility = 'default'
endif

if get_option('default_library') == 'static'
  public_args += '-DCURL_STATICLIB'
endif

# Turn off Visual Studio CRT deprecation warnings.
if cc.get_id() == 'clang-cl'
  add_project_arguments(
    [
      '-D_CRT_NONSTDC_NO_DEPRECATE',
      '-D_CRT_SECURE_NO_DEPRECATE',
      '-D_CRT_SECURE_NO_WARNINGS',
    ],
    language: 'c',
  )
endif

# }}}

# CA. {{{

ca_bundle = get_option('ca_bundle')
if ca_bundle == 'auto'
  foreach _path : [
    '/etc/ssl/certs/ca-certificates.crt',
    '/etc/pki/tls/certs/ca-bundle.crt',
    '/usr/share/ssl/certs/ca-bundle.crt',
    '/usr/local/share/certs/ca-root-nss.crt',
    '/etc/ssl/cert.pem',
  ]
    if fs.is_file(_path)
      ca_bundle = _path
      break
    endif
  endforeach
  if ca_bundle == 'auto'
    message('CA bundle auto-detection failed')
    ca_bundle = ''
  endif
endif
if ca_bundle == ''
  cdata.set('CURL_CA_BUNDLE', false)
else
  cdata.set_quoted('CURL_CA_BUNDLE', ca_bundle)
endif

ca_path = get_option('ca_path')
if ca_path == 'auto'
  foreach _path : ['/etc/ssl/certs']
    if fs.is_dir(_path)
      ca_path = _path
      break
    endif
  endforeach
  if ca_path == 'auto'
    message('CA path auto-detection failed')
    ca_path = ''
  endif
endif
if ca_path == ''
  cdata.set('CURL_CA_PATH', false)
else
  cdata.set_quoted('CURL_CA_PATH', ca_path)
endif

cdata.set('CURL_CA_FALLBACK', get_option('ca_fallback'))

# }}}

# Protocols. {{{

# SSL variants.
foreach _proto : ['ftp', 'gopher', 'http', 'imap', 'pop3', 'smtp']
  set_variable(
    _proto + 's_opt',
    ssl_opt.allowed() and get_variable(_proto + '_opt').allowed() ? ENABLED_OPT : DISABLED_OPT,
  )
endforeach

foreach _proto : [
  'dict',
  'file',
  'ftp',
  'gopher',
  'http',
  'imap',
  'ldap',
  'ldaps',
  'mqtt',
  'pop3',
  'rtsp',
  'smb',
  'smtp',
  'telnet',
  'tftp',
]
  cdata.set(
    'CURL_DISABLE_' + _proto.to_upper(),
    get_variable(_proto + '_opt').disabled(),
  )
endforeach

# }}}

# Features. {{{

kerberos_opt = (kerberos_auth_opt.allowed()
and (gss_api_opt.allowed() or sspi_opt.allowed())
? ENABLED_OPT : DISABLED_OPT
)

spnego_opt = (negotiate_auth_opt.allowed()
and (gss_api_opt.allowed() or sspi_opt.allowed())
? ENABLED_OPT : DISABLED_OPT
)

# `CURL_DISABLE_ALTSVC` is used instead of `CURL_DISABLE_ALT_SVC`…
altsvc_opt = alt_svc_opt

foreach _feat : [
  'altsvc',
  'aws',
  'basic-auth',
  'bearer-auth',
  'bindlocal',
  'cookies',
  'digest-auth',
  'doh',
  'form-api',
  'getoptions',
  'headers-api',
  'hsts',
  'http-auth',
  'kerberos-auth',
  'libcurl-option',
  'mime',
  'negotiate-auth',
  'netrc',
  'ntlm',
  'openssl-auto-load-config',
  'parsedate',
  'progress-meter',
  'proxy',
  'sha512_256',
  'shuffle-dns',
  'socketpair',
  'verbose-strings',
]
  cdata.set(
    'CURL_DISABLE_' + _feat.underscorify().to_upper(),
    get_variable(_feat.underscorify() + '_opt').disabled(),
  )
endforeach

# }}}.

# Reported features, protocols, and SSL backends. {{{

foreach _section, _optlist : {
  'features': [
    ['alt-svc'],
    ['AsynchDNS', asynchdns_resolver != '' ? asynchdns_resolver : false],
    ['brotli'],
    ['Debug'],
    ['gsasl'],
    ['GSS-API', gss_api_provider != '' ? gss_api_provider : false],
    ['HTTP2'],
    ['HTTP3'],
    ['HTTPS-proxy'],
    ['HSTS'],
    ['IDN', idn_provider != '' ? idn_provider : false],
    ['IPv6'],
    ['Kerberos'],
    ['Largefile'],
    ['libz'],
    ['MultiSSL'],
    ['NTLM'],
    ['PSL'],
    ['SHA512/256'],
    ['SPNEGO'],
    ['SSL'],
    ['SSPI'],
    ['TrackMemory'],
    ['TLS-SRP'],
    ['threadsafe'],
    ['UnixSockets'],
    ['zstd'],
  ],
  'protocols': [
    ['DICT'],
    ['FILE'],
    ['FTP'],
    ['FTPS'],
    ['GOPHER'],
    ['GOPHERS'],
    ['HTTP'],
    ['HTTPS'],
    ['IMAP'],
    ['IMAPS'],
    ['IPFS'],
    ['IPNS'],
    ['LDAP', ldap_provider != '' ? ldap_provider : false],
    ['LDAPS'],
    ['MQTT'],
    ['POP3'],
    ['POP3S'],
    ['RTMP'],
    ['RTSP'],
    ['SCP', ssh_provider != '' ? ssh_provider : false],
    ['SFTP', ssh_provider != '' ? ssh_provider : false],
    ['SMB'],
    ['SMBS'],
    ['SMTP'],
    ['SMTPS'],
    ['TELNET'],
    ['TFTP'],
    ['WS'],
    ['WSS'],
  ],
  'ssl_backends': [['OpenSSL'], ['Schannel'], ['Secure Transport']],
}
  _summary = {}
  _enabled_list = []
  _disabled_list = []
  foreach _spec : _optlist
    _name = _spec[0]
    _opt = _name.underscorify().to_lower() + '_opt'
    _enabled = get_variable(_opt).allowed()
    _summary_value = _spec.get(1, _enabled)
    # Some features are not advertised.
    if _name not in ['SHA512/256']
      if _enabled
        _enabled_list += _name
      else
        _disabled_list += _name
      endif
    endif
    _summary += {
      _name: _summary_value,
    }
  endforeach
  set_variable(_section + '_summary', _summary)
  set_variable('enabled_' + _section, _enabled_list)
  set_variable('disabled_' + _section, _disabled_list)
endforeach

ssl_backends_summary += {
  'Default backend': ssl_default_backend,
}

# }}}

# Sanitize boolean flags.
_boolkeys = ['STDC_HEADERS']
foreach _key : cdata.keys()
  if (_key.startswith('CURL_DISABLE_')
or _key.endswith('_ENABLED')
or _key.split('_')[0] in ['ENABLE', 'HAVE', 'NEED', 'USE']
)
    _boolkeys += _key
  endif
endforeach
foreach _key : _boolkeys
  cdata.set(_key, cdata.get(_key) ? 1 : false)
endforeach

# }}}

# Other. {{{

cdata.set('DEBUGBUILD', get_option('debug'))
cdata.set('CURLDEBUG', get_option('curldebug'))

libuv_opt = get_option('libuv').require(
  get_option('debug'),
  error_message: 'using libuv without debug support enabled is useless',
)
libuv_dep = dependency(
  'libuv',
  required: libuv_opt,
)
libuv_opt = libuv_opt.require(libuv_dep.found())
cdata.set('HAVE_UV_H', libuv_opt.allowed())
cdata.set('USE_LIBUV', libuv_opt.allowed())

# }}}

# Generate `curl-config`. {{{

cdata.set('CURLVERSION', CURL_VERSION)
cdata.set('VERSIONNUM', CURL_VERSION_NUM.replace('0x', ''))

_link_args = get_option('c_link_args')
cdata.set('CC', ' '.join(cc.cmd_array()))
cdata.set(
  'LDFLAGS',
  _link_args.length() > 0 ? ('\'' + '\' \''.join(_link_args) + '\'') : '',
)
cdata.set_quoted(
  'LIBCURL_PC_CFLAGS',
  get_option('default_library') == 'static' ? '-DCURL_STATICLIB' : '',
)
cdata.set('LIBCURL_PC_LIBS_PRIVATE', '')
cdata.set_quoted(
  'ENABLE_SHARED',
  get_option('default_library') in ['both', 'shared'] ? 'yes' : 'no',
)
cdata.set_quoted(
  'ENABLE_STATIC',
  get_option('default_library') in ['both', 'static'] ? 'yes' : 'no',
)
if meson.version().version_compare('>=1.1.0')
  cdata.set_quoted('CONFIGURE_OPTIONS', meson.build_options())
else
  cdata.set('CONFIGURE_OPTIONS', '')
endif

cdata.set_quoted('prefix', get_option('prefix'))
cdata.set_quoted('exec_prefix', '${prefix}')
cdata.set_quoted('includedir', '${prefix}' / get_option('includedir'))
cdata.set_quoted('libdir', '${prefix}' / get_option('libdir'))
cdata.set_quoted('libext', 'a')

cdata.set('SUPPORT_FEATURES', ' '.join(enabled_features))
cdata.set('SUPPORT_PROTOCOLS', ' '.join(enabled_protocols))
cdata.set('SSL_BACKENDS', ' '.join(enabled_ssl_backends))

curl_config_script = configure_file(
  configuration: cdata,
  input: 'curl-config.in',
  output: 'curl-config',
  install: true,
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
  install_tag: 'devel',
)

# }}}

# }}}

# Tests. {{{

# Don't automatically enable tests on Windows,
# as the testsuite does not seem to support it.
tests_opt = get_option('tests').disable_auto_if(
  host_machine.system() == 'windows',
)

# Absolute minimum for setting up & running the testsuite.
bash_exe = find_program(
  'bash',
  required: tests_opt,
)
# NOTE: look for `gmake` first so on macOS we get the
# brew version (>=4) and not the old system one (3.xx)…
make_exe = find_program(
  ['gmake', 'make', 'mingw32-make'],
  required: tests_opt,
)
# Don't use `find_program('perl')`, as is it may yield a different perl
# than the one that is going to be used by the testsuite through bash
# (e.g. on Windows).
if bash_exe.found()
  _cmd = [bash_exe, '-c', 'perl -e ""']
  _ret = run_command(
    _cmd,
    check: false,
  )
  # message(
  #   '@0@ return @1@:\n@2@'.format(
  #     _cmd,
  #     _ret.returncode(),
  #     _ret.stderr(),
  #   ),
  # )
  perl_found = _ret.returncode() == 0
else
  perl_found = false
endif

tests_opt = tests_opt.require(
  bash_exe.found() and make_exe.found() and perl_found,
  error_message: 'running the testsuite requires a UNIX like system with bash, make and perl',
)
build_tests = tests_opt.allowed() ? declare_dependency() : disabler()

unittests_opt = get_option('unittests').require(
  tests_opt.allowed(),
  error_message: 'feature tests is disabled',
).require(
  get_option('debug'),
  error_message: 'only support debug builds',
)
build_unittests = unittests_opt.allowed() ? declare_dependency() : disabler()

# }}}

# Install. {{{

install_data(
  'docs/libcurl/libcurl.m4',
  install_dir: get_option('datadir') / 'aclocal',
  install_tag: 'devel',
)

install_data(
  'COPYING',
  install_dir: get_option('datadir') / 'licences/curl',
)

install_headers(
  [
    'include/curl/curl.h',
    'include/curl/curlver.h',
    'include/curl/easy.h',
    'include/curl/header.h',
    'include/curl/mprintf.h',
    'include/curl/multi.h',
    'include/curl/options.h',
    'include/curl/stdcheaders.h',
    'include/curl/system.h',
    'include/curl/typecheck-gcc.h',
    'include/curl/urlapi.h',
    'include/curl/websockets.h',
  ],
  subdir: 'curl',
)

# }}}

# Subdirs. {{{

subdir('lib')

if get_option('tool').allowed() or build_tests.found()
  subdir('src')
endif

if build_tests.found()
  subdir('tests')
endif

# }}}

# Summary. {{{

summary(
  {
    'OS': cdata.get_unquoted('OS'),
  },
  bool_yn: true,
  section: 'System',
)

summary(
  {
    'Path'    : cdata.get('CURL_CA_PATH'),
    'Bundle'  : cdata.get('CURL_CA_BUNDLE'),
    'Fallback': cdata.get('CURL_CA_FALLBACK'),
  },
  bool_yn: true,
  section: 'CA',
)

if get_option('debug')
  summary(
    {
      'Track memory'        : trackmemory_opt.allowed(),
      'Use libuv for events': libuv_opt.allowed(),
    },
    bool_yn: true,
    section: 'Debugging',
  )
endif

summary(
  features_summary,
  bool_yn: true,
  section: 'Features',
)

summary(
  protocols_summary,
  bool_yn: true,
  section: 'Protocols',
)

summary(
  ssl_backends_summary,
  bool_yn: true,
  section: 'SSL backends',
)

# }}}

# vim: foldmethod=marker foldlevel=0
