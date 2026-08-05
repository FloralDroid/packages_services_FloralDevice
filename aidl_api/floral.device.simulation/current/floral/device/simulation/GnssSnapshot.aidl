/*
 * Copyright 2026 FloralDroid
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software distributed under the
 * License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND.
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

package floral.device.simulation;
@VintfStability
parcelable GnssSnapshot {
  long generation = 0;
  long elapsedRealtimeNs = 0;
  long utcTimeMs = 0;
  boolean hasFix = false;
  double latitudeDegrees = 0.000000;
  double longitudeDegrees = 0.000000;
  double altitudeMeters = 0.000000;
  float groundSpeedMps = 0.000000f;
  float bearingDegrees = 0.000000f;
  float horizontalAccuracyMeters = 0.000000f;
  float verticalAccuracyMeters = 0.000000f;
  float speedAccuracyMps = 0.000000f;
  float bearingAccuracyDegrees = 0.000000f;
  floral.device.simulation.SatelliteSnapshot[] satellites;
}
