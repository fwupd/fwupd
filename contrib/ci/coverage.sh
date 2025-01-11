#!/bin/sh -e

# if invoked outside of CI
if [ "$CI" != "true" ]; then
    echo "Not running in CI"
    exit 1
fi

gcovr -x \
	--filter build/libfwupd \
	--filter build/libfwupdplugin \
	--filter build/plugins \
	--filter build/src \
	--exclude build/libfwupd/fwupd-context-test.c \
	--exclude build/libfwupd/fwupd-thread-test.c \
	--exclude-lines-by-pattern '^.*(G_OBJECT_WARN_INVALID|G_DEFINE_TYPE|JSON_NODE_HOLDS_OBJECT|g_autoptr|g_critical|g_warning|g_assert_cmpfloat_with_epsilon|g_assert_cmpint|g_assert_cmpstr|g_assert_cmpuint|g_assert_error|g_assert_false|g_assert_no_error|g_assert_nonnull|g_assert_not_reached|g_assert_null|g_assert_true|g_return_if_fail|g_return_val_if_fail|g_set_error|g_prefix_error|g_propagate_prefixed_error|g_propagate_error|g_return_if_fail|g_return_val_if_fail).*$' \
	-o coverage.xml
sed "s,build/,,g" coverage.xml -i
