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

package floral.device.display;

import floral.device.display.BufferRegistration;
import floral.device.display.FrameRequest;
import floral.device.display.FrameResult;
import floral.device.display.FrameStatus;
import floral.device.display.StreamState;

@VintfStability
interface IFrameConsumer {
    StreamState getStreamState(long displayId);
    FrameStatus registerBuffer(in BufferRegistration registration);
    FrameResult submitFrame(in FrameRequest request);
}
