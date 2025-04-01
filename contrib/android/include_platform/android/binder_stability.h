/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <android/binder_ibinder.h>

__BEGIN_DECLS

/**
 * Indicates that this transaction is coupled w/ vendor.img
 */
constexpr binder_flags_t FLAG_PRIVATE_VENDOR = 0x10000000;

#if defined(__ANDROID_VENDOR__)

/**
 * Private addition to binder_flag_t.
 */
enum {
    FLAG_PRIVATE_LOCAL = FLAG_PRIVATE_VENDOR,
};

/**
 * This interface has the stability of the vendor image.
 */
void AIBinder_markVendorStability(AIBinder* binder);

static inline void AIBinder_markCompilationUnitStability(AIBinder* binder) {
    AIBinder_markVendorStability(binder);
}

/**
 * Given a binder interface at a certain stability, there may be some
 * requirements associated with that higher stability level. For instance, a
 * VINTF stability binder is required to be in the VINTF manifest. This API
 * can be called to use that same interface within the vendor partition.
 *
 * WARNING: you must hold on to a binder instance after this is set, while you
 * are using it. If you get a binder (e.g. `...->asBinder().get()`), you must
 * save this binder and then
 * use it. For instance:
 *
 *     auto binder = ...->asBinder();
 *     AIBinder_forceDowngradeToVendorStability(binder.get());
 *     doSomething(binder);
 */
void AIBinder_forceDowngradeToVendorStability(AIBinder* binder);

static inline void AIBinder_forceDowngradeToLocalStability(AIBinder* binder) {
    AIBinder_forceDowngradeToVendorStability(binder);
}

#else  // defined(__ANDROID_VENDOR__)

enum {
    FLAG_PRIVATE_LOCAL = 0,
};

/**
 * This interface has the stability of the system image.
 */
__attribute__((weak)) void AIBinder_markSystemStability(AIBinder* binder);

static inline void AIBinder_markCompilationUnitStability(AIBinder* binder) {
    if (AIBinder_markSystemStability == nullptr) return;

    AIBinder_markSystemStability(binder);
}

/**
 * Given a binder interface at a certain stability, there may be some
 * requirements associated with that higher stability level. For instance, a
 * VINTF stability binder is required to be in the VINTF manifest. This API
 * can be called to use that same interface within the system partition.
 *
 * WARNING: you must hold on to a binder instance after this is set, while you
 * are using it. If you get a binder (e.g. `...->asBinder().get()`), you must
 * save this binder and then
 * use it. For instance:
 *
 *     auto binder = ...->asBinder();
 *     AIBinder_forceDowngradeToSystemStability(binder.get());
 *     doSomething(binder);
 */
void AIBinder_forceDowngradeToSystemStability(AIBinder* binder);

static inline void AIBinder_forceDowngradeToLocalStability(AIBinder* binder) {
    AIBinder_forceDowngradeToSystemStability(binder);
}

#endif  // defined(__ANDROID_VENDOR__)

/**
 * WARNING: this is not expected to be used manually. When the build system has
 * versioned checks in place for an interface that prevent it being changed year
 * over year (specifically like those for @VintfStability stable AIDL
 * interfaces), this could be called. Calling this without this or equivalent
 * infrastructure will lead to de facto frozen APIs or GSI test failures.
 *
 * This interface has system<->vendor stability
 */
// b/227835797 - can't use __INTRODUCED_IN(30) because old targets load this code
#if defined(__ANDROID_MIN_SDK_VERSION__) && __ANDROID_MIN_SDK_VERSION__ < 30
__attribute__((weak))
#endif  // defined(__ANDROID_MIN_SDK_VERSION__) && __ANDROID_MIN_SDK_VERSION__ < 30
void AIBinder_markVintfStability(AIBinder* binder);

__END_DECLS
