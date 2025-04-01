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

#include <android/binder_parcel.h>

__BEGIN_DECLS

#if !defined(__ANDROID_APEX__) && !defined(__ANDROID_VNDK__)

/**
 * Gets whether or not FDs are allowed by this AParcel
 *
 * \return true if FDs are allowed, false if they are not. That is
 * if this returns false then AParcel_writeParcelFileDescriptor will
 * return STATUS_FDS_NOT_ALLOWED.
 */
bool AParcel_getAllowFds(const AParcel*);

#endif

/**
 * Data written to the parcel will be zero'd before being deleted or realloced.
 *
 * The main use of this is marking a parcel that will be used in a transaction
 * with FLAG_CLEAR_BUF. When FLAG_CLEAR_BUF is used, the reply parcel will
 * automatically be marked as sensitive when it is created.
 *
 * \param parcel The parcel to clear associated data from.
 */
void AParcel_markSensitive(const AParcel* parcel);

__END_DECLS
