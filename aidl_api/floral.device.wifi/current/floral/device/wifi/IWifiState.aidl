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

package floral.device.wifi;
@VintfStability
interface IWifiState {
  floral.device.wifi.WifiControlResult setEnabled(boolean enabled, long leaseDurationMs);
  floral.device.wifi.WifiControlResult replaceAccessPoints(in floral.device.wifi.WifiAccessPoint[] accessPoints, long leaseDurationMs);
  floral.device.wifi.WifiControlResult setConnection(long accessPointId, long leaseDurationMs);
  floral.device.wifi.WifiControlResult setLink(in floral.device.wifi.WifiLinkState link, long leaseDurationMs);
  floral.device.wifi.WifiControlResult releaseExternalControl();
  floral.device.wifi.WifiControlResult pushSamples(in floral.device.wifi.WifiSample[] samples, long leaseDurationMs);
  floral.device.wifi.WifiControlResult setEnabledFromSystem(boolean enabled);
  floral.device.wifi.WifiControlResult connectFromSystem(String ssid, String bssid, int security, String credential);
  floral.device.wifi.WifiControlResult disconnectFromSystem();
  floral.device.wifi.WifiProfile getProfile();
  floral.device.wifi.WifiSnapshot getSnapshot();
  floral.device.wifi.WifiAccessPoint[] getAccessPoints();
  int getCapabilityFlags();
}
