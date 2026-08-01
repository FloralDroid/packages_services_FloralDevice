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

#include "floral/device/display/topology/DisplayTopologyControlProtocol.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace floral::device::display::topology {
namespace {

constexpr size_t kSnapshotHeaderSize = 8;
constexpr size_t kDisplayRecordFixedSize = 36;
constexpr size_t kTopologyResponseSize = 16;

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

size_t AlignToUint32(size_t value) {
    return (value + sizeof(uint32_t) - 1) & ~(sizeof(uint32_t) - 1);
}

void WriteUint16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value >> 8);
    output[1] = static_cast<uint8_t>(value);
}

void WriteUint32(uint8_t* output, uint32_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        output[index] = static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

void WriteUint64(uint8_t* output, uint64_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        output[index] = static_cast<uint8_t>(value >> ((sizeof(value) - index - 1) * 8));
    }
}

uint16_t ReadUint16(const uint8_t* input) {
    return static_cast<uint16_t>((static_cast<uint16_t>(input[0]) << 8) | input[1]);
}

uint32_t ReadUint32(const uint8_t* input) {
    uint32_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

uint64_t ReadUint64(const uint8_t* input) {
    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

bool IsKnownResult(TopologyUpdateResult result) {
    switch (result) {
        case TopologyUpdateResult::kApplied:
        case TopologyUpdateResult::kUnchanged:
        case TopologyUpdateResult::kInvalidDisplay:
        case TopologyUpdateResult::kDuplicateDisplayId:
        case TopologyUpdateResult::kDuplicatePort:
        case TopologyUpdateResult::kUnavailable:
            return true;
    }
    return false;
}

bool IsValidUtf8(std::string_view value) {
    size_t offset = 0;
    while (offset < value.size()) {
        const uint8_t lead = static_cast<uint8_t>(value[offset]);
        if (lead <= 0x7f) {
            if (lead == 0) {
                return false;
            }
            ++offset;
            continue;
        }

        size_t trailingBytes = 0;
        uint32_t codePoint = 0;
        uint32_t minimumCodePoint = 0;
        if ((lead & 0xe0) == 0xc0) {
            trailingBytes = 1;
            codePoint = lead & 0x1f;
            minimumCodePoint = 0x80;
        } else if ((lead & 0xf0) == 0xe0) {
            trailingBytes = 2;
            codePoint = lead & 0x0f;
            minimumCodePoint = 0x800;
        } else if ((lead & 0xf8) == 0xf0) {
            trailingBytes = 3;
            codePoint = lead & 0x07;
            minimumCodePoint = 0x10000;
        } else {
            return false;
        }
        if (trailingBytes > value.size() - offset - 1) {
            return false;
        }
        for (size_t index = 1; index <= trailingBytes; ++index) {
            const uint8_t continuation = static_cast<uint8_t>(value[offset + index]);
            if ((continuation & 0xc0) != 0x80) {
                return false;
            }
            codePoint = (codePoint << 6) | (continuation & 0x3f);
        }
        if (codePoint < minimumCodePoint || codePoint > 0x10ffff ||
            (codePoint >= 0xd800 && codePoint <= 0xdfff)) {
            return false;
        }
        offset += trailingBytes + 1;
    }
    return true;
}

}  // namespace

bool SerializeReplaceDisplayTopologyRequest(const std::vector<ManagedPhysicalDisplay>& displays,
                                            std::vector<uint8_t>* payload, std::string* error) {
    if (payload == nullptr) {
        return SetError(error, "topology request payload output is null");
    }
    if (displays.size() > kMaximumControlledExternalDisplays) {
        return SetError(error, "topology request contains too many external displays");
    }

    size_t payloadSize = kSnapshotHeaderSize;
    for (const ManagedPhysicalDisplay& display : displays) {
        if (display.supported_refresh_rates_hz.size() > kMaximumDisplayRefreshRates ||
            display.name.size() > kMaximumDisplayNameBytes ||
            display.supported_refresh_rates_hz.size() >
                    static_cast<size_t>(std::numeric_limits<uint16_t>::max()) ||
            display.name.size() > static_cast<size_t>(std::numeric_limits<uint16_t>::max())) {
            return SetError(error, "topology display metadata exceeds protocol limits");
        }
        if (!IsValidUtf8(display.name)) {
            return SetError(error, "topology display name is not valid UTF-8");
        }
        const size_t recordSize = AlignToUint32(
                kDisplayRecordFixedSize +
                display.supported_refresh_rates_hz.size() * sizeof(uint32_t) + display.name.size());
        if (recordSize > control::kMaximumControlPayloadSize ||
            payloadSize > control::kMaximumControlPayloadSize - recordSize) {
            return SetError(error, "topology request payload exceeds the protocol limit");
        }
        payloadSize += recordSize;
    }

    payload->assign(payloadSize, 0);
    WriteUint32(payload->data(), static_cast<uint32_t>(displays.size()));
    size_t offset = kSnapshotHeaderSize;
    for (const ManagedPhysicalDisplay& display : displays) {
        const size_t ratesSize = display.supported_refresh_rates_hz.size() * sizeof(uint32_t);
        const size_t recordSize =
                AlignToUint32(kDisplayRecordFixedSize + ratesSize + display.name.size());
        uint8_t* record = payload->data() + offset;
        WriteUint32(record, static_cast<uint32_t>(recordSize));
        WriteUint64(record + 4, display.display_id);
        WriteUint32(record + 12, display.port);
        WriteUint32(record + 16, display.width);
        WriteUint32(record + 20, display.height);
        WriteUint32(record + 24, display.dpi);
        WriteUint32(record + 28, display.active_refresh_rate_hz);
        WriteUint16(record + 32, static_cast<uint16_t>(display.supported_refresh_rates_hz.size()));
        WriteUint16(record + 34, static_cast<uint16_t>(display.name.size()));
        size_t variableOffset = kDisplayRecordFixedSize;
        for (uint32_t refreshRate : display.supported_refresh_rates_hz) {
            WriteUint32(record + variableOffset, refreshRate);
            variableOffset += sizeof(uint32_t);
        }
        std::copy(display.name.begin(), display.name.end(), record + variableOffset);
        offset += recordSize;
    }
    return true;
}

bool ParseReplaceDisplayTopologyRequest(const std::vector<uint8_t>& payload,
                                        std::vector<ManagedPhysicalDisplay>* displays,
                                        std::string* error) {
    if (displays == nullptr) {
        return SetError(error, "parsed topology display output is null");
    }
    if (payload.size() < kSnapshotHeaderSize) {
        return SetError(error, "topology request is shorter than its snapshot header");
    }
    if (ReadUint32(payload.data() + 4) != 0) {
        return SetError(error, "topology request reserved field is not zero");
    }
    const uint32_t displayCount = ReadUint32(payload.data());
    if (displayCount > kMaximumControlledExternalDisplays) {
        return SetError(error, "topology request contains too many external displays");
    }

    std::vector<ManagedPhysicalDisplay> parsed;
    parsed.reserve(displayCount);
    size_t offset = kSnapshotHeaderSize;
    for (uint32_t index = 0; index < displayCount; ++index) {
        if (payload.size() - offset < kDisplayRecordFixedSize) {
            return SetError(error, "topology display record is truncated");
        }
        const uint8_t* record = payload.data() + offset;
        const uint32_t recordSize = ReadUint32(record);
        const uint16_t refreshRateCount = ReadUint16(record + 32);
        const uint16_t nameLength = ReadUint16(record + 34);
        if (refreshRateCount > kMaximumDisplayRefreshRates ||
            nameLength > kMaximumDisplayNameBytes) {
            return SetError(error, "topology display metadata exceeds protocol limits");
        }
        const size_t contentSize = kDisplayRecordFixedSize +
                                   static_cast<size_t>(refreshRateCount) * sizeof(uint32_t) +
                                   nameLength;
        const size_t expectedRecordSize = AlignToUint32(contentSize);
        if (recordSize != expectedRecordSize || recordSize > payload.size() - offset) {
            return SetError(error, "topology display record size is invalid");
        }
        if (ReadUint32(record + 12) > std::numeric_limits<uint8_t>::max()) {
            return SetError(error, "topology display port is out of range");
        }
        if (std::any_of(record + contentSize, record + recordSize,
                        [](uint8_t value) { return value != 0; })) {
            return SetError(error, "topology display record padding is not zero");
        }

        ManagedPhysicalDisplay display;
        display.display_id = ReadUint64(record + 4);
        display.port = static_cast<uint8_t>(ReadUint32(record + 12));
        display.width = ReadUint32(record + 16);
        display.height = ReadUint32(record + 20);
        display.dpi = ReadUint32(record + 24);
        display.active_refresh_rate_hz = ReadUint32(record + 28);
        size_t variableOffset = kDisplayRecordFixedSize;
        display.supported_refresh_rates_hz.reserve(refreshRateCount);
        for (uint16_t rateIndex = 0; rateIndex < refreshRateCount; ++rateIndex) {
            display.supported_refresh_rates_hz.push_back(ReadUint32(record + variableOffset));
            variableOffset += sizeof(uint32_t);
        }
        const char* name = reinterpret_cast<const char*>(record + variableOffset);
        display.name.assign(name, nameLength);
        if (!IsValidUtf8(display.name)) {
            return SetError(error, "topology display name is not valid UTF-8");
        }
        parsed.push_back(std::move(display));
        offset += recordSize;
    }
    if (offset != payload.size()) {
        return SetError(error, "topology request contains trailing data");
    }
    *displays = std::move(parsed);
    return true;
}

bool SerializeReplaceDisplayTopologyResponse(const TopologyUpdate& update, uint32_t request_id,
                                             control::ControlResponse* response,
                                             std::string* error) {
    if (response == nullptr) {
        return SetError(error, "topology response output is null");
    }
    if (!IsKnownResult(update.result)) {
        return SetError(error, "topology response result is unknown");
    }

    response->payload.assign(kTopologyResponseSize, 0);
    WriteUint32(response->payload.data(), static_cast<uint32_t>(update.result));
    WriteUint64(response->payload.data() + 8, update.generation);
    response->header.message_type =
            static_cast<uint16_t>(control::ControlMessageType::kReplaceDisplayTopologyResponse);
    response->header.flags = 0;
    response->header.request_id = request_id;
    response->header.payload_size = static_cast<uint32_t>(response->payload.size());
    response->refreshes_authority_lease = update.result == TopologyUpdateResult::kApplied ||
                                          update.result == TopologyUpdateResult::kUnchanged;
    return true;
}

bool ParseReplaceDisplayTopologyResponse(const std::vector<uint8_t>& payload,
                                         TopologyUpdate* update, std::string* error) {
    if (update == nullptr) {
        return SetError(error, "parsed topology response output is null");
    }
    if (payload.size() != kTopologyResponseSize || ReadUint32(payload.data() + 4) != 0) {
        return SetError(error, "topology response payload is invalid");
    }
    TopologyUpdate parsed;
    parsed.result = static_cast<TopologyUpdateResult>(ReadUint32(payload.data()));
    parsed.generation = ReadUint64(payload.data() + 8);
    if (!IsKnownResult(parsed.result)) {
        return SetError(error, "topology response result is unknown");
    }
    *update = parsed;
    return true;
}

}  // namespace floral::device::display::topology
