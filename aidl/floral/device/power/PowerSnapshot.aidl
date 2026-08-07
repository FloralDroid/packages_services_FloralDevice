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
    float batteryTemperatureCelsius = 0.0f;
    float skinTemperatureCelsius = 0.0f;
    float cpuTemperatureCelsius = 0.0f;
    float gpuTemperatureCelsius = 0.0f;
    int batteryThrottling = 0;
    int skinThrottling = 0;
    int cpuThrottling = 0;
    int gpuThrottling = 0;
}
