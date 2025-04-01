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

/**
 * Function to execute a shell command.
 *
 * Available since API level 30.
 *
 * \param binder the binder executing the command
 * \param in input file descriptor, should be flushed, ownership is not passed
 * \param out output file descriptor, should be flushed, ownership is not passed
 * \param err error file descriptor, should be flushed, ownership is not passed
 * \param argv array of null-terminated strings for command (may be null if argc
 * is 0)
 * \param argc length of argv array
 *
 * \return binder_status_t result of transaction
 */
typedef binder_status_t (*AIBinder_handleShellCommand)(AIBinder* binder, int in, int out, int err,
                                                       const char** argv, uint32_t argc);

/**
 * This sets the implementation of handleShellCommand for a class.
 *
 * If this isn't set, nothing will be executed when handleShellCommand is called.
 *
 * Available since API level 30.
 *
 * \param handleShellCommand function to call when a shell transaction is
 * received
 */
__attribute__((weak)) void AIBinder_Class_setHandleShellCommand(
        AIBinder_Class* clazz, AIBinder_handleShellCommand handleShellCommand) __INTRODUCED_IN(30);

__END_DECLS
