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

package floral.device.radio;

import floral.device.radio.RadioCell;
import floral.device.radio.RadioControlResult;
import floral.device.radio.RadioProfile;
import floral.device.radio.RadioRegistrationControl;
import floral.device.radio.RadioSignal;
import floral.device.radio.RadioSnapshot;

@VintfStability
interface IRadioState {
    RadioProfile getProfile();
    RadioSnapshot getSnapshot();
    RadioControlResult setRegistration(in RadioRegistrationControl control);
    RadioControlResult setSignal(in RadioSignal signal, long leaseDurationMs);
    RadioControlResult replaceCells(in RadioCell[] cells, long leaseDurationMs);
    RadioControlResult setSimState(int state, long leaseDurationMs);
    RadioControlResult injectIncomingCall(String number);
    RadioControlResult setCallState(long callId, int state);
    RadioControlResult injectIncomingSms(String address, String body);
    RadioControlResult releaseExternalControl();
}
