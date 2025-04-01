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

#include <stdint.h>
#include <sys/cdefs.h>

#include <android/binder_status.h>

__BEGIN_DECLS

/**
 * This creates a threadpool for incoming binder transactions if it has not already been created,
 * spawning one thread, and allowing the kernel to lazily start threads according to the count
 * that is specified in ABinderProcess_setThreadPoolMaxThreadCount.
 *
 * For instance, if ABinderProcess_setThreadPoolMaxThreadCount(3) is called,
 * ABinderProcess_startThreadPool() is called (+1 thread) then the main thread calls
 * ABinderProcess_joinThreadPool() (+1 thread), up to *5* total threads will be started
 * (2 directly, and 3 more if the kernel starts them lazily).
 *
 * When using this, it is expected that ABinderProcess_setupPolling and
 * ABinderProcess_handlePolledCommands are not used.
 *
 * Do not use this from a library. Apps setup their own threadpools, and otherwise, the main
 * function should be responsible for configuring the threadpool for the entire application.
 */
void ABinderProcess_startThreadPool(void);
/**
 * This sets the maximum number of threads that can be started in the threadpool. By default, after
 * startThreadPool is called, this is 15. If it is called additional times, it will only prevent
 * the kernel from starting new threads and will not delete already existing threads. This should
 * be called once before startThreadPool. The number of threads can never decrease.
 *
 * This count refers to the number of threads that will be created lazily by the kernel, in
 * addition to the single threads created by ABinderProcess_startThreadPool (+1) or
 * ABinderProcess_joinThreadPool (+1). Note: ABinderProcess_startThreadPool starts a thread
 * itself, but it also enables up to the number of threads passed to this function to start.
 * This function does not start any threads itself; it only configures
 * ABinderProcess_startThreadPool.
 *
 * Do not use this from a library. Apps setup their own threadpools, and otherwise, the main
 * function should be responsible for configuring the threadpool for the entire application.
 */
bool ABinderProcess_setThreadPoolMaxThreadCount(uint32_t numThreads);
/**
 * Check if the threadpool has already been started.
 * This tells whether someone in the process has called ABinderProcess_startThreadPool. Usually,
 * you should use this in a library to abort if the threadpool is not started.
 * Programs should configure binder threadpools once at the beginning.
 */
bool ABinderProcess_isThreadPoolStarted(void);
/**
 * This adds the current thread to the threadpool. This thread will be in addition to the thread
 * configured with ABinderProcess_setThreadPoolMaxThreadCount and started with
 * ABinderProcess_startThreadPool.
 *
 * Do not use this from a library. Apps setup their own threadpools, and otherwise, the main
 * function should be responsible for configuring the threadpool for the entire application.
 */
void ABinderProcess_joinThreadPool(void);

/**
 * This gives you an fd to wait on. Whenever data is available on the fd,
 * ABinderProcess_handlePolledCommands can be called to handle binder queries.
 * This is expected to be used in a single threaded process which waits on
 * events from multiple different fds.
 *
 * When using this, it is expected ABinderProcess_startThreadPool and
 * ABinderProcess_joinThreadPool are not used.
 *
 * \param fd out param corresponding to the binder domain opened in this
 * process.
 * \return STATUS_OK on success
 */
__attribute__((weak)) binder_status_t ABinderProcess_setupPolling(int* fd) __INTRODUCED_IN(31);

/**
 * This will handle all queued binder commands in this process and then return.
 * It is expected to be called whenever there is data on the fd.
 *
 * \return STATUS_OK on success
 */
__attribute__((weak)) binder_status_t ABinderProcess_handlePolledCommands(void) __INTRODUCED_IN(31);

__END_DECLS
