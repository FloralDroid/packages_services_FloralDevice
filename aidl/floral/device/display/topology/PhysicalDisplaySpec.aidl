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

package floral.device.display.topology;

@VintfStability
parcelable PhysicalDisplaySpec {
    long displayId = 0;
    int port = 0;
    int width = 0;
    int height = 0;
    int dpi = 0;
    int[] supportedRefreshRatesHz;
    int activeRefreshRateHz = 0;
    String name;
}
