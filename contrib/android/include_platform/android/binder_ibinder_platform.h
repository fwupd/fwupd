/*
 * Copyright (C) 2020 The Android Open Source Project
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

// platform values for binder_flags_t
enum {
    /**
     * The transaction and reply will be cleared by the kernel in read-only
     * binder buffers storing transactions.
     *
     * Introduced in API level 31.
     */
    FLAG_CLEAR_BUF = 0x20,
};

/**
 * Makes calls to AIBinder_getCallingSid work if the kernel supports it. This
 * must be called on a local binder server before it is sent out to any othe
 * process. If this is a remote binder, it will abort. If the kernel doesn't
 * support this feature, you'll always get null from AIBinder_getCallingSid.
 *
 * \param binder local server binder to request security contexts on
 */
__attribute__((weak)) void AIBinder_setRequestingSid(AIBinder* binder, bool requestingSid)
        __INTRODUCED_IN(31);

/**
 * Returns the selinux context of the callee.
 *
 * In order for this to work, the following conditions must be met:
 * - The kernel must be new enough to support this feature.
 * - The server must have called AIBinder_setRequestingSid.
 * - The callee must be a remote process.
 *
 * \return security context or null if unavailable. The lifetime of this context
 * is the lifetime of the transaction.
 */
__attribute__((weak, warn_unused_result)) const char* AIBinder_getCallingSid() __INTRODUCED_IN(31);

/**
 * Sets a minimum scheduler policy for all transactions coming into this
 * AIBinder.
 *
 * This must be called before the object is sent to another process.
 * Aborts on invalid values. Not thread safe.
 *
 * \param binder local server binder to set the policy for
 * \param policy scheduler policy as defined in linux UAPI
 * \param priority priority. [-20..19] for SCHED_NORMAL, [1..99] for RT
 */
void AIBinder_setMinSchedulerPolicy(AIBinder* binder, int policy, int priority) __INTRODUCED_IN(33);

/**
 * Allow the binder to inherit realtime scheduling policies from its caller.
 *
 * This must be called before the object is sent to another process. Not thread
 * safe.
 *
 * \param binder local server binder to set the policy for
 * \param inheritRt whether to inherit realtime scheduling policies (default is
 *     false).
 */
void AIBinder_setInheritRt(AIBinder* binder, bool inheritRt) __INTRODUCED_IN(33);

__END_DECLS
