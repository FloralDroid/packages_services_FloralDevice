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

package floral.device.wifi;

import floral.device.wifi.WifiAccessPoint;
import floral.device.wifi.WifiControlResult;
import floral.device.wifi.WifiLinkState;
import floral.device.wifi.WifiProfile;
import floral.device.wifi.WifiSample;
import floral.device.wifi.WifiSnapshot;

@VintfStability
interface IWifiState {
    WifiControlResult setEnabled(boolean enabled, long leaseDurationMs);
    WifiControlResult replaceAccessPoints(in WifiAccessPoint[] accessPoints, long leaseDurationMs);
    WifiControlResult setConnection(long accessPointId, long leaseDurationMs);
    WifiControlResult setLink(in WifiLinkState link, long leaseDurationMs);
    WifiControlResult releaseExternalControl();
    WifiControlResult pushSamples(in WifiSample[] samples, long leaseDurationMs);
    WifiControlResult setEnabledFromSystem(boolean enabled);
    WifiControlResult connectFromSystem(String ssid, String bssid, int security,
            String credential);
    WifiControlResult disconnectFromSystem();
    WifiProfile getProfile();
    WifiSnapshot getSnapshot();
    WifiAccessPoint[] getAccessPoints();
    int getCapabilityFlags();
}
