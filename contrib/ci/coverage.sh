#!/bin/sh
set -e

# if invoked outside of CI
if [ "$CI" != "true" ]; then
    echo "Not running in CI"
    exit 1
fi

gcovr -x \
    ${GCOV:+--gcov-executable "${GCOV}"} \
    --filter '(^|/)libfwupd/' \
    --filter '(^|/)libfwupdplugin/' \
    --filter '(^|/)plugins/' \
    --filter '(^|/)src/' \
    --exclude '(^|/)subprojects/' \
    --exclude '.*/fwupd-context-test\.c' \
    --exclude '.*/fwupd-thread-test\.c' \
    --exclude-lines-by-pattern '^.*(G_OBJECT_WARN_INVALID|G_DEFINE_TYPE|JSON_NODE_HOLDS_OBJECT|g_autoptr|g_critical|g_warning|g_assert_cmpfloat_with_epsilon|g_assert_cmpint|g_assert_cmpstr|g_assert_cmpuint|g_assert_error|g_assert_false|g_assert_no_error|g_assert_nonnull|g_assert_not_reached|g_assert_null|g_assert_true|g_return_if_fail|g_return_val_if_fail).*$' \
    -o coverage.xml
sed "s,build/,,g" coverage.xml -i

# fail if no source files matched
if ! grep -q '<class ' coverage.xml; then
    echo "coverage.sh: no coverage data was captured -- check the gcovr filters" >&2
    exit 1
fi
