/*
 * Copyright (C) 2018 The Android Open Source Project
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
#include <android/binder_status.h>
#include <android/llndk-versioning.h>
#include <stdint.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

typedef enum AServiceManager_AddServiceFlag : uint32_t {
    /**
     * This allows processes with AID_ISOLATED to get the binder of the service added.
     *
     * Services with methods that perform file IO, web socket creation or ways to egress data must
     * not be added with this flag for privacy concerns.
     */
    ADD_SERVICE_ALLOW_ISOLATED = 1 << 0,
    ADD_SERVICE_DUMP_FLAG_PRIORITY_CRITICAL = 1 << 1,
    ADD_SERVICE_DUMP_FLAG_PRIORITY_HIGH = 1 << 2,
    ADD_SERVICE_DUMP_FLAG_PRIORITY_NORMAL = 1 << 3,
    ADD_SERVICE_DUMP_FLAG_PRIORITY_DEFAULT = 1 << 4,
} AServiceManager_AddServiceFlag;

/**
 * This registers the service with the default service manager under this instance name. This does
 * not take ownership of binder.
 *
 * WARNING: when using this API across an APEX boundary, do not use with unstable
 * AIDL services. TODO(b/139325195)
 *
 * \param binder object to register globally with the service manager.
 * \param instance identifier of the service. This will be used to lookup the service.
 *
 * \return EX_NONE on success.
 */
__attribute__((warn_unused_result)) binder_exception_t AServiceManager_addService(
        AIBinder* binder, const char* instance) __INTRODUCED_IN(29);

/**
 * This registers the service with the default service manager under this instance name. This does
 * not take ownership of binder.
 *
 * WARNING: when using this API across an APEX boundary, do not use with unstable
 * AIDL services. TODO(b/139325195)
 *
 * \param binder object to register globally with the service manager.
 * \param instance identifier of the service. This will be used to lookup the service.
 * \param flags an AServiceManager_AddServiceFlag enum to denote how the service should be added.
 *
 * \return EX_NONE on success.
 */
__attribute__((warn_unused_result)) binder_exception_t AServiceManager_addServiceWithFlags(
        AIBinder* binder, const char* instance, const AServiceManager_AddServiceFlag flags)
        __INTRODUCED_IN(34);

/**
 * Gets a binder object with this specific instance name. Will return nullptr immediately if the
 * service is not available This also implicitly calls AIBinder_incStrong (so the caller of this
 * function is responsible for calling AIBinder_decStrong).
 *
 * WARNING: when using this API across an APEX boundary, do not use with unstable
 * AIDL services. TODO(b/139325195)
 *
 * \param instance identifier of the service used to lookup the service.
 */
__attribute__((warn_unused_result)) AIBinder* AServiceManager_checkService(const char* instance)
        __INTRODUCED_IN(29);

/**
 * Gets a binder object with this specific instance name. Blocks for a couple of seconds waiting on
 * it. This also implicitly calls AIBinder_incStrong (so the caller of this function is responsible
 * for calling AIBinder_decStrong). This does polling. A more efficient way to make sure you
 * unblock as soon as the service is available is to use AIBinder_waitForService.
 *
 * WARNING: when using this API across an APEX boundary, do not use with unstable
 * AIDL services. TODO(b/139325195)
 *
 * WARNING: when using this API, typically, you should call it in a loop. It's dangerous to
 * assume that nullptr could mean that the service is not available. The service could just
 * be starting. Generally, whether a service exists, this information should be declared
 * externally (for instance, an Android feature might imply the existence of a service,
 * a system property, or in the case of services in the VINTF manifest, it can be checked
 * with AServiceManager_isDeclared).
 *
 * \param instance identifier of the service used to lookup the service.
 */
[[deprecated("this polls 5s, use AServiceManager_waitForService or AServiceManager_checkService")]]
__attribute__((warn_unused_result)) AIBinder* AServiceManager_getService(const char* instance)
        __INTRODUCED_IN(29);

/**
 * Registers a lazy service with the default service manager under the 'instance' name.
 * Does not take ownership of binder.
 * The service must be configured statically with init so it can be restarted with
 * ctl.interface.* messages from servicemanager.
 * AServiceManager_registerLazyService cannot safely be used with AServiceManager_addService
 * in the same process. If one service is registered with AServiceManager_registerLazyService,
 * the entire process will have its lifetime controlled by servicemanager.
 * Instead, all services in the process should be registered using
 * AServiceManager_registerLazyService.
 *
 * \param binder object to register globally with the service manager.
 * \param instance identifier of the service. This will be used to lookup the service.
 *
 * \return STATUS_OK on success.
 */
binder_status_t AServiceManager_registerLazyService(AIBinder* binder, const char* instance)
        __INTRODUCED_IN(31);

/**
 * Gets a binder object with this specific instance name. Efficiently waits for the service.
 * If the service is not ever registered, it will wait indefinitely. Requires the threadpool
 * to be started in the service.
 * This also implicitly calls AIBinder_incStrong (so the caller of this function is responsible
 * for calling AIBinder_decStrong).
 *
 * WARNING: when using this API across an APEX boundary, do not use with unstable
 * AIDL services. TODO(b/139325195)
 *
 * \param instance identifier of the service used to lookup the service.
 *
 * \return service if registered, null if not.
 */
__attribute__((warn_unused_result)) AIBinder* AServiceManager_waitForService(const char* instance)
        __INTRODUCED_IN(31);

/**
 * Function to call when a service is registered. The instance is passed as well as
 * ownership of the binder named 'registered'.
 *
 * WARNING: a lock is held when this method is called in order to prevent races with
 * AServiceManager_NotificationRegistration_delete. Do not make synchronous binder calls when
 * implementing this method to avoid deadlocks.
 *
 * \param instance instance name of service registered
 * \param registered ownership-passed instance of service registered
 * \param cookie data passed during registration for notifications
 */
typedef void (*AServiceManager_onRegister)(const char* instance, AIBinder* registered,
                                           void* cookie);

/**
 * Represents a registration to servicemanager which can be cleared anytime.
 */
typedef struct AServiceManager_NotificationRegistration AServiceManager_NotificationRegistration;

/**
 * Get notifications when a service is registered. If the service is already registered,
 * you will immediately get a notification.
 *
 * WARNING: it is strongly recommended to use AServiceManager_waitForService API instead.
 * That API will wait synchronously, which is what you usually want in cases, including
 * using some feature or during boot up. There is a history of bugs where waiting for
 * notifications like this races with service startup. Also, when this API is used, a service
 * bug will result in silent failure (rather than a debuggable deadlock). Furthermore, there
 * is a history of this API being used to know when a service is up as a proxy for whethre
 * that service should be started. This should only be used if you are intending to get
 * ahold of the service as a client. For lazy services, whether a service is registered
 * should not be used as a proxy for when it should be registered, which is only known
 * by the real client.
 *
 * WARNING: if you use this API, you must also ensure that you check missing services are
 * started and crash otherwise. If service failures are ignored, the system rots.
 *
 * \param instance name of service to wait for notifications about
 * \param onRegister callback for when service is registered
 * \param cookie data associated with this callback
 *
 * \return the token for this registration. Deleting this token will unregister.
 */
__attribute__((warn_unused_result)) AServiceManager_NotificationRegistration*
AServiceManager_registerForServiceNotifications(const char* instance,
                                                AServiceManager_onRegister onRegister, void* cookie)
        __INTRODUCED_IN(34);

/**
 * Unregister for notifications and delete the object.
 *
 * After this method is called, the callback is guaranteed to no longer be invoked. This will block
 * until any in-progress onRegister callbacks have completed. It is therefore safe to immediately
 * destroy the void* cookie that was registered when this method returns.
 *
 * \param notification object to dismiss
 */
void AServiceManager_NotificationRegistration_delete(
        AServiceManager_NotificationRegistration* notification) __INTRODUCED_IN(34);

/**
 * Check if a service is declared (e.g. VINTF manifest).
 *
 * \param instance identifier of the service.
 *
 * \return true on success, meaning AServiceManager_waitForService should always
 *    be able to return the service.
 */
bool AServiceManager_isDeclared(const char* instance) __INTRODUCED_IN(31);

/**
 * Returns all declared instances for a particular interface.
 *
 * For instance, if 'android.foo.IFoo/foo' is declared, and 'android.foo.IFoo' is
 * passed here, then ["foo"] would be returned.
 *
 * See also AServiceManager_isDeclared.
 *
 * \param interface interface, e.g. 'android.foo.IFoo'
 * \param context to pass to callback
 * \param callback taking instance (e.g. 'foo') and context
 */
void AServiceManager_forEachDeclaredInstance(const char* interface, void* context,
                                             void (*callback)(const char*, void*))
        __INTRODUCED_IN(31);

/**
 * Check if a service is updatable via an APEX module.
 *
 * \param instance identifier of the service
 *
 * \return whether the interface is updatable via APEX
 */
bool AServiceManager_isUpdatableViaApex(const char* instance) __INTRODUCED_IN(31);

/**
 * Returns the APEX name if a service is declared as updatable via an APEX module.
 *
 * \param instance identifier of the service
 * \param context to pass to callback
 * \param callback taking the APEX name (e.g. 'com.android.foo') and context
 */
void AServiceManager_getUpdatableApexName(const char* instance, void* context,
                                          void (*callback)(const char*, void*))
        __INTRODUCED_IN(__ANDROID_API_U__);

/**
 * Opens a declared passthrough HAL.
 *
 * \param instance identifier of the passthrough service (e.g. "mapper")
 * \param instance identifier of the implemenatation (e.g. "default")
 * \param flag passed to dlopen()
 *
 * \return the result of dlopen of the specified HAL
 */
void* AServiceManager_openDeclaredPassthroughHal(const char* interface, const char* instance,
                                                 int flag) __INTRODUCED_IN(__ANDROID_API_V__)
        __INTRODUCED_IN_LLNDK(202404);

/**
 * Prevent lazy services without client from shutting down their process
 *
 * This should only be used if it is every eventually set to false. If a
 * service needs to persist but doesn't need to dynamically shut down,
 * prefer to control it with another mechanism.
 *
 * \param persist 'true' if the process should not exit.
 */
void AServiceManager_forceLazyServicesPersist(bool persist) __INTRODUCED_IN(31);

/**
 * Set a callback that is invoked when the active service count (i.e. services with clients)
 * registered with this process drops to zero (or becomes nonzero).
 * The callback takes a boolean argument, which is 'true' if there is
 * at least one service with clients.
 *
 * \param callback function to call when the number of services
 *    with clients changes.
 * \param context opaque pointer passed back as second parameter to the
 * callback.
 *
 * The callback takes two arguments. The first is a boolean that represents if there are
 * services with clients (true) or not (false).
 * The second is the 'context' pointer passed during the registration.
 *
 * Callback return value:
 * - false: Default behavior for lazy services (shut down the process if there
 *          are no clients).
 * - true:  Don't shut down the process even if there are no clients.
 *
 * This callback gives a chance to:
 * 1 - Perform some additional operations before exiting;
 * 2 - Prevent the process from exiting by returning "true" from the callback.
 */
void AServiceManager_setActiveServicesCallback(bool (*callback)(bool, void*), void* context)
        __INTRODUCED_IN(31);

/**
 * Try to unregister all services previously registered with 'registerService'.
 *
 * \return true on success.
 */
bool AServiceManager_tryUnregister() __INTRODUCED_IN(31);

/**
 * Re-register services that were unregistered by 'tryUnregister'.
 * This method should be called in the case 'tryUnregister' fails
 * (and should be called on the same thread).
 */
void AServiceManager_reRegister() __INTRODUCED_IN(31);

__END_DECLS
