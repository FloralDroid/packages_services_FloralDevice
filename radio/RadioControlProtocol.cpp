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

#include "floral/device/radio/RadioControlProtocol.h"

#include <algorithm>
#include <array>
#include <limits>

namespace floral::device::radio {
namespace {

constexpr size_t kRegistrationRequestSize = 24;
constexpr size_t kSignalRequestSize = 32;
constexpr size_t kCellsRequestHeaderSize = 16;
constexpr size_t kCellRecordSize = 60;
constexpr size_t kSimStateRequestSize = 16;
constexpr size_t kTextRequestHeaderSize = 12;
constexpr size_t kCallStateRequestSize = 24;
constexpr size_t kSmsRequestHeaderSize = 16;
constexpr size_t kUpdateResponseSize = 24;
constexpr size_t kProfileResponseHeaderSize = 48;
constexpr size_t kSnapshotResponseSize = 80;
constexpr size_t kListResponseHeaderSize = 16;
constexpr size_t kCapabilitiesResponseSize = 32;
constexpr size_t kMaximumCells = 32;
constexpr size_t kMaximumCalls = 8;
constexpr size_t kMaximumSmsEvents = 64;

bool SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

uint16_t ReadUint16(const uint8_t* input) {
    return static_cast<uint16_t>((static_cast<uint16_t>(input[0]) << 8) | input[1]);
}

uint32_t ReadUint32(const uint8_t* input) {
    return (static_cast<uint32_t>(input[0]) << 24) | (static_cast<uint32_t>(input[1]) << 16) |
           (static_cast<uint32_t>(input[2]) << 8) | static_cast<uint32_t>(input[3]);
}

uint64_t ReadUint64(const uint8_t* input) {
    return (static_cast<uint64_t>(ReadUint32(input)) << 32) | ReadUint32(input + 4);
}

void WriteUint16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value >> 8);
    output[1] = static_cast<uint8_t>(value);
}

void WriteUint32(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value >> 24);
    output[1] = static_cast<uint8_t>(value >> 16);
    output[2] = static_cast<uint8_t>(value >> 8);
    output[3] = static_cast<uint8_t>(value);
}

void WriteUint64(uint8_t* output, uint64_t value) {
    WriteUint32(output, static_cast<uint32_t>(value >> 32));
    WriteUint32(output + 4, static_cast<uint32_t>(value));
}

bool HasEnvelope(const std::vector<uint8_t>& payload, size_t expected_size) {
    return payload.size() == expected_size && ReadUint16(payload.data()) == kRadioControlVersion &&
           ReadUint16(payload.data() + 2) == expected_size;
}

bool ValidLease(uint32_t duration_ms) {
    return duration_ms > 0 && duration_ms <= kMaximumRadioControlLeaseMs;
}

bool ValidPhoneNumber(const std::string& number) {
    if (number.empty() || number.size() > 20) {
        return false;
    }
    const size_t first_digit = number.front() == '+' ? 1 : 0;
    return first_digit < number.size() &&
           std::all_of(number.begin() + first_digit, number.end(),
                       [](char value) { return value >= '0' && value <= '9'; });
}

bool PrepareResponse(control::ControlCommandId command_id, uint32_t request_id, size_t payload_size,
                     control::ControlResponse* response, std::string* error) {
    if (response == nullptr || payload_size > control::kMaximumControlPayloadSize ||
        payload_size > std::numeric_limits<uint16_t>::max()) {
        return SetError(error, "radio response output is invalid");
    }
    response->header.command_id = static_cast<uint16_t>(command_id);
    response->header.route_kind = control::MakeFhc1RouteKind(control::ControlPacketKind::kResponse);
    response->header.request_id = request_id;
    response->header.payload_size = static_cast<uint32_t>(payload_size);
    response->payload.assign(payload_size, 0);
    response->refreshes_authority_lease = false;
    return true;
}

void WriteSignal(uint8_t* output, const aidl::floral::device::radio::RadioSignal& signal) {
    WriteUint32(output, static_cast<uint32_t>(signal.rssiDbm));
    WriteUint32(output + 4, static_cast<uint32_t>(signal.rsrpDbm));
    WriteUint32(output + 8, static_cast<uint32_t>(signal.rsrqDb));
    WriteUint32(output + 12, static_cast<uint32_t>(signal.rssnrTenthDb));
    WriteUint32(output + 16, static_cast<uint32_t>(signal.cqi));
    WriteUint32(output + 20, static_cast<uint32_t>(signal.timingAdvance));
}

void WriteCell(uint8_t* output, const aidl::floral::device::radio::RadioCell& cell) {
    WriteUint64(output, static_cast<uint64_t>(cell.identity));
    WriteUint32(output + 8, cell.registered ? 1 : 0);
    WriteUint32(output + 12, static_cast<uint32_t>(cell.tac));
    WriteUint64(output + 16, static_cast<uint64_t>(cell.ci));
    WriteUint32(output + 24, static_cast<uint32_t>(cell.pci));
    WriteUint32(output + 28, static_cast<uint32_t>(cell.earfcn));
    WriteUint32(output + 32, static_cast<uint32_t>(cell.bandwidthKhz));
    WriteSignal(output + 36, cell.signal);
}

bool AppendRecord(const std::vector<uint8_t>& record, std::vector<uint8_t>* payload,
                  std::string* error) {
    if (payload->size() + record.size() > control::kMaximumControlPayloadSize) {
        return SetError(error, "radio list response is too large");
    }
    payload->insert(payload->end(), record.begin(), record.end());
    return true;
}

}  // namespace

bool ParseRegistrationRequest(const std::vector<uint8_t>& payload,
                              aidl::floral::device::radio::RadioRegistrationControl* request,
                              std::string* error) {
    if (request == nullptr || !HasEnvelope(payload, kRegistrationRequestSize) ||
        ReadUint32(payload.data() + 20) != 0) {
        return SetError(error, "radio registration request envelope is invalid");
    }
    const uint32_t voice = ReadUint32(payload.data() + 4);
    const uint32_t data = ReadUint32(payload.data() + 8);
    const uint32_t technology = ReadUint32(payload.data() + 12);
    const uint32_t lease = ReadUint32(payload.data() + 16);
    if (voice > 5 || data > 5 || technology > 3 || !ValidLease(lease)) {
        return SetError(error, "radio registration request contains an invalid field");
    }
    request->voiceRegistration = static_cast<int32_t>(voice);
    request->dataRegistration = static_cast<int32_t>(data);
    request->technology = static_cast<int32_t>(technology);
    request->leaseDurationMs = lease;
    return true;
}

bool ParseSignalRequest(const std::vector<uint8_t>& payload,
                        aidl::floral::device::radio::RadioSignal* signal,
                        int64_t* lease_duration_ms, std::string* error) {
    if (signal == nullptr || lease_duration_ms == nullptr ||
        !HasEnvelope(payload, kSignalRequestSize)) {
        return SetError(error, "radio signal request envelope is invalid");
    }
    signal->rssiDbm = static_cast<int32_t>(ReadUint32(payload.data() + 4));
    signal->rsrpDbm = static_cast<int32_t>(ReadUint32(payload.data() + 8));
    signal->rsrqDb = static_cast<int32_t>(ReadUint32(payload.data() + 12));
    signal->rssnrTenthDb = static_cast<int32_t>(ReadUint32(payload.data() + 16));
    signal->cqi = static_cast<int32_t>(ReadUint32(payload.data() + 20));
    signal->timingAdvance = static_cast<int32_t>(ReadUint32(payload.data() + 24));
    const uint32_t lease = ReadUint32(payload.data() + 28);
    if (signal->rssiDbm < -120 || signal->rssiDbm > -20 || signal->rsrpDbm < -140 ||
        signal->rsrpDbm > -40 || signal->rsrqDb < -30 || signal->rsrqDb > 0 ||
        signal->rssnrTenthDb < -200 || signal->rssnrTenthDb > 300 || signal->cqi < 0 ||
        signal->cqi > 15 || signal->timingAdvance < 0 || signal->timingAdvance > 1282 ||
        !ValidLease(lease)) {
        return SetError(error, "radio signal request contains an invalid field");
    }
    *lease_duration_ms = lease;
    return true;
}

bool ParseCellsRequest(const std::vector<uint8_t>& payload,
                       std::vector<aidl::floral::device::radio::RadioCell>* cells,
                       int64_t* lease_duration_ms, std::string* error) {
    if (cells == nullptr || lease_duration_ms == nullptr ||
        payload.size() < kCellsRequestHeaderSize ||
        ReadUint16(payload.data()) != kRadioControlVersion ||
        ReadUint16(payload.data() + 2) != payload.size()) {
        return SetError(error, "radio cells request envelope is invalid");
    }
    const uint32_t count = ReadUint32(payload.data() + 4);
    const uint32_t lease = ReadUint32(payload.data() + 8);
    if (count == 0 || count > kMaximumCells || !ValidLease(lease) ||
        ReadUint32(payload.data() + 12) != 0 ||
        payload.size() != kCellsRequestHeaderSize + count * kCellRecordSize) {
        return SetError(error, "radio cells request size or count is invalid");
    }
    cells->clear();
    cells->reserve(count);
    size_t registered_count = 0;
    for (uint32_t index = 0; index < count; ++index) {
        const uint8_t* input = payload.data() + kCellsRequestHeaderSize + index * kCellRecordSize;
        aidl::floral::device::radio::RadioCell cell;
        cell.identity = static_cast<int64_t>(ReadUint64(input));
        const uint32_t registered = ReadUint32(input + 8);
        cell.registered = registered != 0;
        cell.tac = static_cast<int32_t>(ReadUint32(input + 12));
        cell.ci = static_cast<int64_t>(ReadUint64(input + 16));
        cell.pci = static_cast<int32_t>(ReadUint32(input + 24));
        cell.earfcn = static_cast<int32_t>(ReadUint32(input + 28));
        cell.bandwidthKhz = static_cast<int32_t>(ReadUint32(input + 32));
        cell.signal.rssiDbm = static_cast<int32_t>(ReadUint32(input + 36));
        cell.signal.rsrpDbm = static_cast<int32_t>(ReadUint32(input + 40));
        cell.signal.rsrqDb = static_cast<int32_t>(ReadUint32(input + 44));
        cell.signal.rssnrTenthDb = static_cast<int32_t>(ReadUint32(input + 48));
        cell.signal.cqi = static_cast<int32_t>(ReadUint32(input + 52));
        cell.signal.timingAdvance = static_cast<int32_t>(ReadUint32(input + 56));
        if (registered > 1 || cell.identity <= 0 || cell.tac < 0 || cell.tac > 65'535 ||
            cell.ci < 0 || cell.ci > 268'435'455 || cell.pci < 0 || cell.pci > 503 ||
            cell.earfcn < 0 || cell.earfcn > 262'143 || cell.bandwidthKhz < 1'400 ||
            cell.bandwidthKhz > 20'000 || cell.signal.rssiDbm < -120 || cell.signal.rssiDbm > -20 ||
            cell.signal.rsrpDbm < -140 || cell.signal.rsrpDbm > -40 || cell.signal.rsrqDb < -30 ||
            cell.signal.rsrqDb > 0 || cell.signal.rssnrTenthDb < -200 ||
            cell.signal.rssnrTenthDb > 300 || cell.signal.cqi < 0 || cell.signal.cqi > 15 ||
            cell.signal.timingAdvance < 0 || cell.signal.timingAdvance > 1282) {
            return SetError(error, "radio cell record contains an invalid field");
        }
        registered_count += cell.registered ? 1 : 0;
        cells->push_back(std::move(cell));
    }
    if (registered_count != 1) {
        return SetError(error, "radio cells request requires one serving cell");
    }
    *lease_duration_ms = lease;
    return true;
}

bool ParseSimStateRequest(const std::vector<uint8_t>& payload, SimStateRequest* request,
                          std::string* error) {
    if (request == nullptr || !HasEnvelope(payload, kSimStateRequestSize) ||
        ReadUint32(payload.data() + 12) != 0) {
        return SetError(error, "SIM state request envelope is invalid");
    }
    const uint32_t state = ReadUint32(payload.data() + 4);
    const uint32_t lease = ReadUint32(payload.data() + 8);
    if (state > 3 || !ValidLease(lease)) {
        return SetError(error, "SIM state request contains an invalid field");
    }
    request->state = static_cast<int32_t>(state);
    request->lease_duration_ms = lease;
    return true;
}

bool ParseIncomingCallRequest(const std::vector<uint8_t>& payload, std::string* number,
                              std::string* error) {
    if (number == nullptr || payload.size() < kTextRequestHeaderSize ||
        ReadUint16(payload.data()) != kRadioControlVersion ||
        ReadUint16(payload.data() + 2) != payload.size() || ReadUint32(payload.data() + 8) != 0) {
        return SetError(error, "incoming call request envelope is invalid");
    }
    const uint32_t length = ReadUint32(payload.data() + 4);
    if (length == 0 || length > 20 || payload.size() != kTextRequestHeaderSize + length) {
        return SetError(error, "incoming call request length is invalid");
    }
    number->assign(reinterpret_cast<const char*>(payload.data() + kTextRequestHeaderSize), length);
    return ValidPhoneNumber(*number) || SetError(error, "incoming call number is invalid");
}

bool ParseCallStateRequest(const std::vector<uint8_t>& payload, CallStateRequest* request,
                           std::string* error) {
    if (request == nullptr || !HasEnvelope(payload, kCallStateRequestSize) ||
        ReadUint32(payload.data() + 4) != 0 || ReadUint32(payload.data() + 20) != 0) {
        return SetError(error, "call state request envelope is invalid");
    }
    const uint64_t call_id = ReadUint64(payload.data() + 8);
    const uint32_t state = ReadUint32(payload.data() + 16);
    if (call_id == 0 || call_id > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
        state > 5) {
        return SetError(error, "call state request contains an invalid field");
    }
    request->call_id = static_cast<int64_t>(call_id);
    request->state = static_cast<int32_t>(state);
    return true;
}

bool ParseIncomingSmsRequest(const std::vector<uint8_t>& payload, IncomingSmsRequest* request,
                             std::string* error) {
    if (request == nullptr || payload.size() < kSmsRequestHeaderSize ||
        ReadUint16(payload.data()) != kRadioControlVersion ||
        ReadUint16(payload.data() + 2) != payload.size() || ReadUint32(payload.data() + 12) != 0) {
        return SetError(error, "incoming SMS request envelope is invalid");
    }
    const uint32_t address_length = ReadUint32(payload.data() + 4);
    const uint32_t body_length = ReadUint32(payload.data() + 8);
    if (address_length == 0 || address_length > 20 || body_length == 0 || body_length > 1'024 ||
        payload.size() != kSmsRequestHeaderSize + address_length + body_length) {
        return SetError(error, "incoming SMS request length is invalid");
    }
    const char* text = reinterpret_cast<const char*>(payload.data() + kSmsRequestHeaderSize);
    request->address.assign(text, address_length);
    request->body.assign(text + address_length, body_length);
    return ValidPhoneNumber(request->address) || SetError(error, "incoming SMS address is invalid");
}

bool SerializeRadioUpdateResponse(control::ControlCommandId command_id, uint32_t request_id,
                                  const aidl::floral::device::radio::RadioControlResult& update,
                                  control::ControlResponse* response, std::string* error) {
    if (!PrepareResponse(command_id, request_id, kUpdateResponseSize, response, error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kRadioControlVersion);
    WriteUint16(response->payload.data() + 2, kUpdateResponseSize);
    WriteUint32(response->payload.data() + 4, static_cast<uint32_t>(update.result));
    WriteUint64(response->payload.data() + 8, static_cast<uint64_t>(update.generation));
    WriteUint64(response->payload.data() + 16, static_cast<uint64_t>(update.objectId));
    response->refreshes_authority_lease = update.result == 0 || update.result == 1;
    return true;
}

bool SerializeRadioProfileResponse(uint32_t request_id,
                                   const aidl::floral::device::radio::RadioProfile& profile,
                                   control::ControlResponse* response, std::string* error) {
    const std::array<const std::string*, 10> strings = {&profile.operatorLongName,
                                                        &profile.operatorShortName,
                                                        &profile.mcc,
                                                        &profile.mnc,
                                                        &profile.imei,
                                                        &profile.imeisv,
                                                        &profile.imsi,
                                                        &profile.iccid,
                                                        &profile.msisdn,
                                                        &profile.basebandVersion};
    size_t payload_size = kProfileResponseHeaderSize;
    for (const std::string* value : strings) {
        payload_size += value->size();
    }
    if (!PrepareResponse(control::ControlCommandId::kGetRadioProfile, request_id, payload_size,
                         response, error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kRadioControlVersion);
    WriteUint16(response->payload.data() + 2, static_cast<uint16_t>(payload_size));
    WriteUint32(response->payload.data() + 4, 1);
    size_t text_offset = kProfileResponseHeaderSize;
    for (size_t index = 0; index < strings.size(); ++index) {
        WriteUint32(response->payload.data() + 8 + index * 4,
                    static_cast<uint32_t>(strings[index]->size()));
        std::copy(strings[index]->begin(), strings[index]->end(),
                  response->payload.begin() + text_offset);
        text_offset += strings[index]->size();
    }
    return true;
}

bool SerializeRadioSnapshotResponse(uint32_t request_id,
                                    const aidl::floral::device::radio::RadioSnapshot& snapshot,
                                    control::ControlResponse* response, std::string* error) {
    if (!PrepareResponse(control::ControlCommandId::kGetRadioSnapshot, request_id,
                         kSnapshotResponseSize, response, error)) {
        return false;
    }
    const uint32_t flags = (snapshot.externallyControlled ? 1U : 0U) | (snapshot.radioOn ? 2U : 0U);
    WriteUint16(response->payload.data(), kRadioControlVersion);
    WriteUint16(response->payload.data() + 2, kSnapshotResponseSize);
    WriteUint32(response->payload.data() + 4, flags);
    WriteUint64(response->payload.data() + 8, static_cast<uint64_t>(snapshot.generation));
    WriteUint64(response->payload.data() + 16, static_cast<uint64_t>(snapshot.timestampNs));
    WriteUint32(response->payload.data() + 24, static_cast<uint32_t>(snapshot.simState));
    WriteUint32(response->payload.data() + 28, static_cast<uint32_t>(snapshot.voiceRegistration));
    WriteUint32(response->payload.data() + 32, static_cast<uint32_t>(snapshot.dataRegistration));
    WriteUint32(response->payload.data() + 36, static_cast<uint32_t>(snapshot.technology));
    WriteSignal(response->payload.data() + 40, snapshot.signal);
    WriteUint32(response->payload.data() + 64, static_cast<uint32_t>(snapshot.cells.size()));
    WriteUint32(response->payload.data() + 68, static_cast<uint32_t>(snapshot.calls.size()));
    WriteUint32(response->payload.data() + 72, static_cast<uint32_t>(snapshot.smsEvents.size()));
    return true;
}

bool SerializeRadioCellsResponse(uint32_t request_id,
                                 const std::vector<aidl::floral::device::radio::RadioCell>& cells,
                                 control::ControlResponse* response, std::string* error) {
    const size_t payload_size = kListResponseHeaderSize + cells.size() * kCellRecordSize;
    if (!PrepareResponse(control::ControlCommandId::kListRadioCells, request_id, payload_size,
                         response, error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kRadioControlVersion);
    WriteUint16(response->payload.data() + 2, static_cast<uint16_t>(payload_size));
    WriteUint32(response->payload.data() + 4, static_cast<uint32_t>(cells.size()));
    for (size_t index = 0; index < cells.size(); ++index) {
        WriteCell(response->payload.data() + kListResponseHeaderSize + index * kCellRecordSize,
                  cells[index]);
    }
    return true;
}

bool SerializeRadioCallsResponse(uint32_t request_id,
                                 const std::vector<aidl::floral::device::radio::RadioCall>& calls,
                                 control::ControlResponse* response, std::string* error) {
    if (!PrepareResponse(control::ControlCommandId::kListRadioCalls, request_id,
                         kListResponseHeaderSize, response, error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kRadioControlVersion);
    WriteUint32(response->payload.data() + 4, static_cast<uint32_t>(calls.size()));
    for (const auto& call : calls) {
        const size_t record_size = 24 + call.number.size();
        std::vector<uint8_t> record(record_size, 0);
        WriteUint32(record.data(), static_cast<uint32_t>(record_size));
        WriteUint32(record.data() + 4, static_cast<uint32_t>(call.state));
        WriteUint64(record.data() + 8, static_cast<uint64_t>(call.id));
        WriteUint32(record.data() + 16, (call.incoming ? 1U : 0U) | (call.multiparty ? 2U : 0U));
        WriteUint32(record.data() + 20, static_cast<uint32_t>(call.number.size()));
        std::copy(call.number.begin(), call.number.end(), record.begin() + 24);
        if (!AppendRecord(record, &response->payload, error)) {
            return false;
        }
    }
    WriteUint16(response->payload.data() + 2, static_cast<uint16_t>(response->payload.size()));
    response->header.payload_size = static_cast<uint32_t>(response->payload.size());
    return true;
}

bool SerializeRadioSmsEventsResponse(
        uint32_t request_id, const std::vector<aidl::floral::device::radio::RadioSmsEvent>& events,
        control::ControlResponse* response, std::string* error) {
    if (!PrepareResponse(control::ControlCommandId::kListRadioSmsEvents, request_id,
                         kListResponseHeaderSize, response, error)) {
        return false;
    }
    WriteUint16(response->payload.data(), kRadioControlVersion);
    size_t first_event = events.size();
    size_t payload_size = kListResponseHeaderSize;
    while (first_event > 0) {
        const auto& event = events[first_event - 1];
        const size_t record_size = 32 + event.address.size() + event.body.size();
        if (payload_size + record_size > std::numeric_limits<uint16_t>::max() ||
            payload_size + record_size > control::kMaximumControlPayloadSize) {
            break;
        }
        payload_size += record_size;
        --first_event;
    }
    WriteUint32(response->payload.data() + 4, static_cast<uint32_t>(events.size() - first_event));
    for (size_t index = first_event; index < events.size(); ++index) {
        const auto& event = events[index];
        const size_t record_size = 32 + event.address.size() + event.body.size();
        std::vector<uint8_t> record(record_size, 0);
        WriteUint32(record.data(), static_cast<uint32_t>(record_size));
        WriteUint32(record.data() + 4, event.incoming ? 1U : 0U);
        WriteUint64(record.data() + 8, static_cast<uint64_t>(event.sequence));
        WriteUint64(record.data() + 16, static_cast<uint64_t>(event.timestampNs));
        WriteUint32(record.data() + 24, static_cast<uint32_t>(event.address.size()));
        WriteUint32(record.data() + 28, static_cast<uint32_t>(event.body.size()));
        std::copy(event.address.begin(), event.address.end(), record.begin() + 32);
        std::copy(event.body.begin(), event.body.end(), record.begin() + 32 + event.address.size());
        if (!AppendRecord(record, &response->payload, error)) {
            return false;
        }
    }
    WriteUint16(response->payload.data() + 2, static_cast<uint16_t>(response->payload.size()));
    response->header.payload_size = static_cast<uint32_t>(response->payload.size());
    return true;
}

bool SerializeRadioCapabilitiesResponse(uint32_t request_id, control::ControlResponse* response,
                                        std::string* error) {
    if (!PrepareResponse(control::ControlCommandId::kGetRadioCapabilities, request_id,
                         kCapabilitiesResponseSize, response, error)) {
        return false;
    }
    constexpr uint32_t kFlags = 0x0d;         // Autonomous, entropy, and runtime leases.
    constexpr uint64_t kTechnologies = 0x0e;  // GSM, WCDMA, and LTE; no NR claim.
    WriteUint16(response->payload.data(), kRadioControlVersion);
    WriteUint16(response->payload.data() + 2, kCapabilitiesResponseSize);
    WriteUint32(response->payload.data() + 4, kFlags);
    WriteUint32(response->payload.data() + 8, kMaximumCells);
    WriteUint32(response->payload.data() + 12, kMaximumCalls);
    WriteUint32(response->payload.data() + 16, kMaximumSmsEvents);
    WriteUint32(response->payload.data() + 20, static_cast<uint32_t>(kMaximumRadioControlLeaseMs));
    WriteUint64(response->payload.data() + 24, kTechnologies);
    return true;
}

}  // namespace floral::device::radio
