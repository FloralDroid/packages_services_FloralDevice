/*
 * Copyright 2026 FloralDroid
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software distributed under the
 * License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND.
 */

package floral.device.simulation;

import floral.device.simulation.SatelliteSnapshot;

@VintfStability
parcelable GnssSnapshot {
    long generation = 0;
    long elapsedRealtimeNs = 0;
    long utcTimeMs = 0;
    boolean hasFix = false;
    double latitudeDegrees = 0.0;
    double longitudeDegrees = 0.0;
    double altitudeMeters = 0.0;
    float groundSpeedMps = 0.0f;
    float bearingDegrees = 0.0f;
    float horizontalAccuracyMeters = 0.0f;
    float verticalAccuracyMeters = 0.0f;
    float speedAccuracyMps = 0.0f;
    float bearingAccuracyDegrees = 0.0f;
    SatelliteSnapshot[] satellites;
}
