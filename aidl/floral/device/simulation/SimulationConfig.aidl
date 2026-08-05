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

package floral.device.simulation;

@VintfStability
parcelable SimulationConfig {
    long generation = 1;
    int motionSource = 0;
    int motionProfile = 0;
    int gnssSource = 0;
    boolean gnssEnabled = true;
    float targetLightLux = 200.0f;
    float targetProximityCm = 5.0f;
    float targetPressureHpa = 1013.25f;
    double anchorLatitudeDegrees = 35.681236;
    double anchorLongitudeDegrees = 139.767125;
    double anchorAltitudeMeters = 20.0;
    float groundSpeedMps = 0.0f;
    float bearingDegrees = 0.0f;
    int transitionDurationMs = 1000;
}
