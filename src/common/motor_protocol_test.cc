#include "src/common/motor_protocol.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <variant>

#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"

using namespace stfoc;

TEST_GROUP(MotorProtocolTest) {};

TEST(MotorProtocolTest, ControllerEncodeAndDriverDecodeAllMotors) {
  std::array<uint8_t, kControllerMessageSize> buf{};
  CHECK_TRUE(ControllerEncode(CommandType::kTorque, 1.25f, -3.5f, 42.0f, buf));

  const std::array<float, kNumMotors> expected = {1.25f, -3.5f, 42.0f};
  for (uint8_t idx = 0; idx < kNumMotors; ++idx) {
    ControllerMessage decoded;
    CHECK_TRUE(DriverDecode(buf, idx, decoded));
    CHECK_TRUE(std::holds_alternative<MotorCommand>(decoded));
    const auto& cmd = std::get<MotorCommand>(decoded);
    CHECK_EQUAL(static_cast<int>(CommandType::kTorque),
                static_cast<int>(cmd.command));
    DOUBLES_EQUAL(expected[idx], cmd.value, 0.0);
  }
}

TEST(MotorProtocolTest, ControllerEncodeAllCommandTypes) {
  const std::array<CommandType, 6> types = {
      CommandType::kDisable,
      CommandType::kTorque,
      CommandType::kVelocity,
      CommandType::kAngle,
      CommandType::kVelocityOpenLoop,
      CommandType::kAngleOpenLoop,
  };

  for (CommandType type : types) {
    std::array<uint8_t, kControllerMessageSize> buf{};
    CHECK_TRUE(ControllerEncode(type, 0.0f, -0.0f, 123.456f, buf));

    ControllerMessage decoded;
    CHECK_TRUE(DriverDecode(buf, 2, decoded));
    const auto& cmd = std::get<MotorCommand>(decoded);
    CHECK_EQUAL(static_cast<int>(type), static_cast<int>(cmd.command));
    DOUBLES_EQUAL(123.456f, cmd.value, 1e-5);
  }
}

TEST(MotorProtocolTest, ControllerEncodeRejectsInvalidCommand) {
  std::array<uint8_t, kControllerMessageSize> buf{};
  CHECK_FALSE(ControllerEncode(static_cast<CommandType>(0xFF), 1.0f, 2.0f, 3.0f,
                               buf));
}

TEST(MotorProtocolTest, DriverDecodeRejectsOutOfRangeIndex) {
  std::array<uint8_t, kControllerMessageSize> buf{};
  CHECK_TRUE(ControllerEncode(CommandType::kVelocity, 1.0f, 2.0f, 3.0f, buf));

  ControllerMessage decoded;
  CHECK_FALSE(DriverDecode(buf, kNumMotors, decoded));
  CHECK_FALSE(DriverDecode(buf, 255, decoded));
}

TEST(MotorProtocolTest, DriverDecodeRejectsInvalidCommandByte) {
  std::array<uint8_t, kControllerMessageSize> buf{};
  buf[0] = 0xAA;

  ControllerMessage decoded;
  CHECK_FALSE(DriverDecode(buf, 0, decoded));
}

TEST(MotorProtocolTest, DriverEncodeAndControllerDecodeIdMessage) {
  const uint64_t expected_id = 0x0123456789ABCDEFULL;
  std::array<uint8_t, kDriverMessageSize> buf{};
  CHECK_TRUE(DriverEncode(DriverIdMessage{.id = expected_id}, buf));

  // Verify wire layout: tag + little-endian low32 + little-endian high32.
  CHECK_EQUAL(static_cast<uint8_t>(DriverMessageTag::kAnnounceId), buf[0]);
  CHECK_EQUAL(0xEF, buf[1]);
  CHECK_EQUAL(0xCD, buf[2]);
  CHECK_EQUAL(0xAB, buf[3]);
  CHECK_EQUAL(0x89, buf[4]);
  CHECK_EQUAL(0x67, buf[5]);
  CHECK_EQUAL(0x45, buf[6]);
  CHECK_EQUAL(0x23, buf[7]);
  CHECK_EQUAL(0x01, buf[8]);

  DriverMessage decoded;
  CHECK_TRUE(ControllerDecode(buf, decoded));
  CHECK_TRUE(std::holds_alternative<DriverIdMessage>(decoded));
  CHECK_TRUE(std::get<DriverIdMessage>(decoded).id == expected_id);
}

TEST(MotorProtocolTest, DriverEncodeAndControllerDecodeSpeedMessage) {
  const float expected_speed = -187.625f;
  std::array<uint8_t, kDriverMessageSize> buf{};
  CHECK_TRUE(DriverEncode(DriverSpeedMessage{.speed = expected_speed}, buf));

  CHECK_EQUAL(static_cast<uint8_t>(DriverMessageTag::kSpeed), buf[0]);
  // Reserved second 32-bit field must be zeroed.
  for (size_t i = 5; i < kDriverMessageSize; ++i) {
    CHECK_EQUAL(0, buf[i]);
  }

  DriverMessage decoded;
  CHECK_TRUE(ControllerDecode(buf, decoded));
  CHECK_TRUE(std::holds_alternative<DriverSpeedMessage>(decoded));
  DOUBLES_EQUAL(expected_speed, std::get<DriverSpeedMessage>(decoded).speed, 0.0);
}

TEST(MotorProtocolTest, ControllerDecodeRejectsInvalidTag) {
  std::array<uint8_t, kDriverMessageSize> buf{};
  buf[0] = 0x99;

  DriverMessage decoded;
  CHECK_FALSE(ControllerDecode(buf, decoded));
}

TEST(MotorProtocolTest, RejectsUndersizedBuffers) {
  std::array<uint8_t, kControllerMessageSize - 1> short_ctrl{};
  CHECK_FALSE(ControllerEncode(CommandType::kTorque, 1.0f, 2.0f, 3.0f,
                               short_ctrl));
  ControllerMessage ctrl_msg;
  CHECK_FALSE(DriverDecode(short_ctrl, 0, ctrl_msg));

  std::array<uint8_t, kDriverMessageSize - 1> short_drv{};
  CHECK_FALSE(DriverEncode(DriverSpeedMessage{.speed = 1.0f}, short_drv));
  DriverMessage drv_msg;
  CHECK_FALSE(ControllerDecode(short_drv, drv_msg));
}

TEST(MotorProtocolTest, AcceptsOversizedBuffers) {
  // e.g. 16-byte FDCAN frame buffer
  std::array<uint8_t, 16> frame{};
  CHECK_TRUE(ControllerEncode(CommandType::kAngle, 0.5f, 1.5f, 2.5f, frame));

  ControllerMessage ctrl_msg;
  CHECK_TRUE(DriverDecode(frame, 1, ctrl_msg));
  DOUBLES_EQUAL(1.5f, std::get<MotorCommand>(ctrl_msg).value, 0.0);

  CHECK_TRUE(DriverEncode(DriverSpeedMessage{.speed = 9.25f}, frame));
  DriverMessage drv_msg;
  CHECK_TRUE(ControllerDecode(frame, drv_msg));
  DOUBLES_EQUAL(9.25f, std::get<DriverSpeedMessage>(drv_msg).speed, 0.0);
}

int main(int argc, char** argv) {
  return CommandLineTestRunner::RunAllTests(argc, argv);
}
