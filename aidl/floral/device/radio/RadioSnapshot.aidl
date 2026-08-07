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

import floral.device.radio.RadioCall;
import floral.device.radio.RadioCell;
import floral.device.radio.RadioSignal;
import floral.device.radio.RadioSmsEvent;

@VintfStability
parcelable RadioSnapshot {
    long generation = 1;
    long timestampNs = 0;
    boolean externallyControlled = false;
    boolean radioOn = true;
    int simState = 1;
    int voiceRegistration = 1;
    int dataRegistration = 0;
    int technology = 3;
    RadioSignal signal;
    RadioCell[] cells;
    RadioCall[] calls;
    RadioSmsEvent[] smsEvents;
}
