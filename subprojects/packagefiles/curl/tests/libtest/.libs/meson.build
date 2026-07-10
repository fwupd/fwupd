# `runtests.pl` expects to be able to preload `tests/libtest/.libs/libstubgss.so`.

if gss_api_opt.allowed()
  libtest_targets += shared_library('stubgss', '../stub_gssapi.c')
endif
