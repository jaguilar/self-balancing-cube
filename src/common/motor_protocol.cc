#include "src/common/motor_protocol.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace stfoc {
namespace {

bool IsValidCommandType(uint8_t raw) {
  switch (static_cast<CommandType>(raw)) {
    case CommandType::kDisable:
    case CommandType::kTorque:
    case CommandType::kVelocity:
    case CommandType::kAngle:
    case CommandType::kVelocityOpenLoop:
    case CommandType::kAngleOpenLoop:
      return true;
  }
  return false;
}

void WriteU32LE(uint8_t* dst, uint32_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFu);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

uint32_t ReadU32LE(const uint8_t* src) {
  return static_cast<uint32_t>(src[0]) |
         (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) |
         (static_cast<uint32_t>(src[3]) << 24);
}

void WriteF32LE(uint8_t* dst, float value) {
  WriteU32LE(dst, std::bit_cast<uint32_t>(value));
}

float ReadF32LE(const uint8_t* src) {
  return std::bit_cast<float>(ReadU32LE(src));
}

}  // namespace

bool ControllerEncode(CommandType command,
                      float param0,
                      float param1,
                      float param2,
                      std::span<uint8_t> out) {
  if (out.size() < kControllerMessageSize) {
    return false;
  }

  const auto raw_cmd = static_cast<uint8_t>(command);
  if (!IsValidCommandType(raw_cmd)) {
    return false;
  }

  out[0] = raw_cmd;
  WriteF32LE(&out[1], param0);
  WriteF32LE(&out[5], param1);
  WriteF32LE(&out[9], param2);
  return true;
}

bool DriverDecode(std::span<const uint8_t> in,
                  uint8_t motor_index,
                  ControllerMessage& out) {
  if (motor_index >= kNumMotors || in.size() < kControllerMessageSize) {
    return false;
  }

  const uint8_t raw_cmd = in[0];
  if (!IsValidCommandType(raw_cmd)) {
    return false;
  }

  const size_t offset = 1 + static_cast<size_t>(motor_index) * sizeof(uint32_t);
  out = MotorCommand{
      .command = static_cast<CommandType>(raw_cmd),
      .value = ReadF32LE(&in[offset]),
  };
  return true;
}

bool DriverEncode(const DriverMessage& msg, std::span<uint8_t> out) {
  if (out.size() < kDriverMessageSize) {
    return false;
  }

  std::visit(
      [&out](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, DriverIdMessage>) {
          out[0] = static_cast<uint8_t>(DriverMessageTag::kAnnounceId);
          WriteU32LE(&out[1], static_cast<uint32_t>(m.id & 0xFFFFFFFFu));
          WriteU32LE(&out[5], static_cast<uint32_t>((m.id >> 32) & 0xFFFFFFFFu));
        } else if constexpr (std::is_same_v<T, DriverSpeedMessage>) {
          out[0] = static_cast<uint8_t>(DriverMessageTag::kSpeed);
          WriteF32LE(&out[1], m.speed);
          WriteU32LE(&out[5], 0u);
        }
      },
      msg);
  return true;
}

bool ControllerDecode(std::span<const uint8_t> in, DriverMessage& out) {
  if (in.size() < kDriverMessageSize) {
    return false;
  }

  switch (static_cast<DriverMessageTag>(in[0])) {
    case DriverMessageTag::kAnnounceId: {
      const uint64_t lo = ReadU32LE(&in[1]);
      const uint64_t hi = ReadU32LE(&in[5]);
      out = DriverIdMessage{.id = lo | (hi << 32)};
      return true;
    }
    case DriverMessageTag::kSpeed: {
      out = DriverSpeedMessage{.speed = ReadF32LE(&in[1])};
      return true;
    }
  }
  return false;
}

}  // namespace stfoc
