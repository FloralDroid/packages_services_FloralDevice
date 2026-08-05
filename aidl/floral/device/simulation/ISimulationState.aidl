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

import android.hardware.common.fmq.MQDescriptor;
import android.hardware.common.fmq.SynchronizedReadWrite;
import floral.device.simulation.GnssSnapshot;
import floral.device.simulation.ISimulationStateListener;
import floral.device.simulation.SensorDescriptor;
import floral.device.simulation.SensorSnapshot;
import floral.device.simulation.SimulationConfig;

@VintfStability
interface ISimulationState {
    SimulationConfig getConfig();
    SensorDescriptor[] getSensorCatalog();
    SensorSnapshot getSensorSnapshot();
    GnssSnapshot getGnssSnapshot();
    MQDescriptor<byte, SynchronizedReadWrite> openExternalStateStream();
    void registerListener(in ISimulationStateListener listener);
    void unregisterListener(in ISimulationStateListener listener);
    void publishSensorCatalog(in SensorDescriptor[] sensors);
    void publishSensorSnapshot(in SensorSnapshot snapshot);
    void publishGnssSnapshot(in GnssSnapshot snapshot);
}
