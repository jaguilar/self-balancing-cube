#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace stfoc {

inline constexpr size_t kNumMotors = 3;

// Standard 11-bit CAN IDs (lower ID = higher bus arbitration priority).
// Controller commands have the highest priority; motor speed responses follow
// immediately at one priority level lower, further offset by motor index
// (0, 1, or 2).
inline constexpr uint32_t kCanIdControllerCommand = 0x100U;
inline constexpr uint32_t kCanIdDriverSpeedBase = kCanIdControllerCommand + 1U;
inline constexpr uint32_t kCanIdDriverAnnounceId = 0x200U;

constexpr uint32_t DriverSpeedCanId(uint8_t motor_index) {
  return kCanIdDriverSpeedBase + static_cast<uint32_t>(motor_index);
}

// Wire sizes (bytes):
//   Controller -> Driver: 1-byte CommandType + 3 x 4-byte little-endian floats
//   Driver -> Controller: 1-byte DriverMessageTag + 2 x 4-byte little-endian fields
inline constexpr size_t kControllerMessageSize = 1 + kNumMotors * sizeof(uint32_t);
inline constexpr size_t kDriverMessageSize = 1 + 2 * sizeof(uint32_t);

// 8-bit command sent from controller to all motor drivers.
enum class CommandType : uint8_t {
  kDisable = 0,
  kTorque = 1,
  kVelocity = 2,
  kAngle = 3,
  kVelocityOpenLoop = 4,
  kAngleOpenLoop = 5,
};

// Decoded command for a single motor driver.
struct MotorCommand {
  CommandType command = CommandType::kDisable;
  float value = 0.0f;

  bool operator==(const MotorCommand&) const = default;
};

using ControllerMessage = std::variant<MotorCommand>;

// 8-bit tag for messages sent from a motor driver to the controller.
enum class DriverMessageTag : uint8_t {
  kAnnounceId = 0,
  kSpeed = 1,
};

// Announcement message carrying the motor driver's 64-bit unique ID.
struct DriverIdMessage {
  uint64_t id = 0;

  bool operator==(const DriverIdMessage&) const = default;
};

// Telemetry response carrying the motor's current speed (rad/s).
struct DriverSpeedMessage {
  float speed = 0.0f;

  bool operator==(const DriverSpeedMessage&) const = default;
};

using DriverMessage = std::variant<DriverIdMessage, DriverSpeedMessage>;

// Encodes a controller-to-driver command with one 32-bit float parameter per motor.
// Returns false if `command` is not a valid CommandType or `out.size() < kControllerMessageSize`.
bool ControllerEncode(CommandType command,
                      float param0,
                      float param1,
                      float param2,
                      std::span<uint8_t> out);

// Decodes a controller-to-driver message for the motor at `motor_index` (0..kNumMotors-1).
// Returns false if `motor_index >= kNumMotors`, `in.size() < kControllerMessageSize`,
// or the command byte is invalid.
bool DriverDecode(std::span<const uint8_t> in,
                  uint8_t motor_index,
                  ControllerMessage& out);

// Encodes a driver-to-controller message (ID announcement or speed telemetry).
// Returns false if `out.size() < kDriverMessageSize`.
bool DriverEncode(const DriverMessage& msg, std::span<uint8_t> out);

// Decodes a driver-to-controller message.
// Returns false if `in.size() < kDriverMessageSize` or the message tag byte is invalid.
bool ControllerDecode(std::span<const uint8_t> in, DriverMessage& out);

}  // namespace stfoc
