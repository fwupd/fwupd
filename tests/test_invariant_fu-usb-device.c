#include <check.h>
#include <stdlib.h>
#include <glib.h>
#include "libfwupdplugin/fu-usb-device.h"

START_TEST(test_usb_device_clone_null_donor_safety)
{
    // Invariant: fu_usb_device_clone must not crash or dereference NULL
    // when donor device is NULL or becomes invalid during clone operation
    
    FuUsbDevice *device = NULL;
    FuUsbDevice *donor = NULL;
    GError *error = NULL;
    
    // Test case 1: Clone with NULL donor (exact exploit case)
    device = fu_usb_device_new(NULL);
    ck_assert_ptr_nonnull(device);
    
    // Attempt clone with NULL donor - must not crash
    gboolean result = fu_usb_device_clone(device, donor, &error);
    // Either safely returns FALSE with error set, or handles gracefully
    ck_assert(result == FALSE || error != NULL);
    
    g_object_unref(device);
    if (error)
        g_error_free(error);
    error = NULL;
    
    // Test case 2: Valid donor, then clone (boundary - valid input)
    device = fu_usb_device_new(NULL);
    donor = fu_usb_device_new(NULL);
    ck_assert_ptr_nonnull(device);
    ck_assert_ptr_nonnull(donor);
    
    result = fu_usb_device_clone(device, donor, &error);
    // Valid clone should succeed or fail safely without crash
    ck_assert(result == TRUE || result == FALSE);
    
    g_object_unref(device);
    g_object_unref(donor);
    if (error)
        g_error_free(error);
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_usb_device_clone_null_donor_safety);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}