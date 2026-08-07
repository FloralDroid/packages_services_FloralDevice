/*
 * Copyright 2026 FloralDroid
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
///////////////////////////////////////////////////////////////////////////////
// THIS FILE IS IMMUTABLE. DO NOT EDIT IN ANY CASE.                          //
///////////////////////////////////////////////////////////////////////////////

// This file is a snapshot of an AIDL file. Do not edit it manually. There are
// two cases:
// 1). this is a frozen version file - do not edit this in any case.
// 2). this is a 'current' file. If you make a backwards compatible change to
//     the interface (from the latest frozen version), the build system will
//     prompt you to update this file with `m <name>-update-api`.
//
// You must not make a backward incompatible change to any AIDL file built
// with the aidl_interface module type with versions property set. The module
// type is used to build AIDL files in a way that they can be used across
// independently updatable components of the system. If a device is shipped
// with such a backward incompatible change, it has a high risk of breaking
// later when a module using the interface is updated, e.g., Mainline modules.

package floral.device.power;
@VintfStability
parcelable PowerSnapshot {
  long generation = 0;
  long timestampNs = 0;
  int controlMode = 0;
  boolean externallyControlled = false;
  boolean chargerOnline = false;
  int batteryStatus = 0;
  int level = 0;
  int voltageMv = 0;
  int currentUa = 0;
  int currentAverageUa = 0;
  int chargeCounterUah = 0;
  int fullChargeUah = 0;
  long chargeTimeToFullSeconds = -1;
  float batteryTemperatureCelsius = 0.000000f;
  float skinTemperatureCelsius = 0.000000f;
  float cpuTemperatureCelsius = 0.000000f;
  float gpuTemperatureCelsius = 0.000000f;
  int batteryThrottling = 0;
  int skinThrottling = 0;
  int cpuThrottling = 0;
  int gpuThrottling = 0;
}
