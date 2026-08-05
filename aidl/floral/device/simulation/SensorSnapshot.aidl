/*
 * Copyright 2026 FloralDroid
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software distributed under the
 * License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND.
 */

package floral.device.simulation;

import floral.device.simulation.SensorReading;

@VintfStability
parcelable SensorSnapshot {
    long generation = 0;
    long timestampNs = 0;
    SensorReading[] readings;
}
