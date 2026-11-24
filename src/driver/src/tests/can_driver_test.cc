#include "can_driver.h"

#include <array>
#include <cstdint>
#include <cstdio>

#include "CppUTest/TestHarness.h"
#include "board_config.h"
#include "src/common/motor_protocol.h"
#include "stm32_hal.h"

namespace stfoc {

class CanDriverTestPeer {
 public:
  static uint64_t ReadDeviceUniqueId64() {
    return StCanDriver::ReadDeviceUniqueId64();
  }
  static std::optional<DriverSpeedMessage> last_received_speed(
      const StCanDriver& can) {
    if (can.received_speed_count_.load(std::memory_order_acquire) == 0U) {
      return std::nullopt;
    }
    const uint8_t idx = can.speed_active_idx_.load(std::memory_order_acquire);
    return can.received_speed_buf_[idx];
  }
  static uint32_t last_received_speed_can_id(const StCanDriver& can) {
    const uint8_t idx = can.speed_active_idx_.load(std::memory_order_acquire);
    return can.received_speed_can_id_buf_[idx];
  }
  static StCanDriver::Error TransmitFrame(StCanDriver& can, uint32_t std_id,
                                          std::span<const uint8_t> data) {
    return can.TransmitFrame(std_id, data);
  }
};

}  // namespace stfoc

namespace {

using Peer = stfoc::CanDriverTestPeer;

template <typename Predicate>
bool WaitUntil(Predicate pred, uint32_t timeout_ms = 50U) {
  const uint32_t t_start = HAL_GetTick();
  while ((HAL_GetTick() - t_start) <= timeout_ms) {
    if (pred()) {
      return true;
    }
  }
  return pred();
}

void SendAnnounceId(stfoc::StCanDriver& can, uint64_t id) {
  std::array<uint8_t, stfoc::kDriverMessageSize> buf{};
  CHECK_TRUE(stfoc::DriverEncode(stfoc::DriverIdMessage{.id = id}, buf));
  CHECK_TRUE(Peer::TransmitFrame(can, stfoc::kCanIdDriverAnnounceId, buf) ==
             stfoc::StCanDriver::Error::kOk);
}

void SendControllerCommand(stfoc::StCanDriver& can, stfoc::CommandType cmd,
                           float p0, float p1, float p2) {
  std::array<uint8_t, stfoc::kControllerMessageSize> buf{};
  CHECK_TRUE(stfoc::ControllerEncode(cmd, p0, p1, p2, buf));
  CHECK_TRUE(Peer::TransmitFrame(can, stfoc::kCanIdControllerCommand, buf) ==
             stfoc::StCanDriver::Error::kOk);
}

}  // namespace

TEST_GROUP(CanDriver){};

TEST(CanDriver, HardwareUniqueIdAndInvalidConfig) {
  const uint64_t uid_a = Peer::ReadDeviceUniqueId64();
  const uint64_t uid_b = Peer::ReadDeviceUniqueId64();
  std::printf("[CanDriver] Hardware UID64=0x%08lX%08lX\r\n",
              static_cast<unsigned long>(uid_a >> 32U),
              static_cast<unsigned long>(uid_a & 0xFFFFFFFFUL));
  CHECK_TRUE(uid_a != 0U);
  CHECK_TRUE(uid_a == uid_b);

  auto bad_cfg = board::GetCanDriverConfig(HAL_FDCAN_MODE_INTERNAL_LOOPBACK);
  bad_cfg.num_motors = 0U;
  stfoc::StCanDriver bad_can(bad_cfg);
  CHECK_TRUE(bad_can.Init() == stfoc::StCanDriver::Error::kInvalidConfig);
  CHECK_TRUE(bad_can.state() == stfoc::StCanDriver::State::kError);
  CHECK_TRUE(bad_can.error() == stfoc::StCanDriver::Error::kInvalidConfig);

  auto cfg = board::GetCanDriverConfig(HAL_FDCAN_MODE_INTERNAL_LOOPBACK);
  stfoc::StCanDriver can(cfg);
  CHECK_TRUE(can.Init() == stfoc::StCanDriver::Error::kOk);
  CHECK_TRUE(can.error() == stfoc::StCanDriver::Error::kOk);
  CHECK_TRUE(can.state() == stfoc::StCanDriver::State::kInitializing);
}

TEST(CanDriver, SingleMotorConfigurationIsImmediatelyReady) {
  auto cfg = board::GetCanDriverConfig(HAL_FDCAN_MODE_INTERNAL_LOOPBACK);
  cfg.num_motors = 1U;
  stfoc::StCanDriver can(cfg);
  CHECK_TRUE(can.Init() == stfoc::StCanDriver::Error::kOk);
  CHECK_TRUE(can.state() == stfoc::StCanDriver::State::kReady);
  CHECK_TRUE(can.error() == stfoc::StCanDriver::Error::kOk);
  CHECK_TRUE(can.motor_index().has_value());
  CHECK_EQUAL(0U, *can.motor_index());

  can.SetCurrentSpeed(7.5f);
  SendControllerCommand(can, stfoc::CommandType::kTorque, 2.25f, 0.0f, 0.0f);
  CHECK_TRUE(WaitUntil([&] {
    return can.last_command().has_value() &&
           Peer::last_received_speed(can).has_value();
  }));
  CHECK_EQUAL(static_cast<int>(stfoc::CommandType::kTorque),
              static_cast<int>(can.last_command()->command));
  DOUBLES_EQUAL(2.25f, can.last_command()->value, 1e-5);
  DOUBLES_EQUAL(7.5f, Peer::last_received_speed(can)->speed, 1e-5);
  CHECK_EQUAL(stfoc::kCanIdControllerCommand + 1U,
              Peer::last_received_speed_can_id(can));
}

TEST(CanDriver, LoopbackInterruptIdDiscoveryAndIndexAssignment) {
  auto cfg = board::GetCanDriverConfig(HAL_FDCAN_MODE_INTERNAL_LOOPBACK);
  cfg.device_id_override = 0x0000000000002000ULL;
  stfoc::StCanDriver can(cfg);
  CHECK_TRUE(can.Init() == stfoc::StCanDriver::Error::kOk);
  CHECK_TRUE(can.state() == stfoc::StCanDriver::State::kInitializing);
  CHECK_FALSE(can.motor_index().has_value());

  // Peer with higher ID (still waiting for 3rd motor).
  SendAnnounceId(can, 0x0000000000003000ULL);
  HAL_Delay(5U);
  CHECK_TRUE(can.state() == stfoc::StCanDriver::State::kInitializing);
  CHECK_FALSE(can.motor_index().has_value());

  // Peer with lower ID completes discovery -> self (0x2000) becomes index 1.
  SendAnnounceId(can, 0x0000000000001000ULL);
  CHECK_TRUE(
      WaitUntil([&] { return can.state() == stfoc::StCanDriver::State::kReady; }));
  CHECK_TRUE(can.motor_index().has_value());
  CHECK_EQUAL(1U, *can.motor_index());
}

TEST(CanDriver, LoopbackInterruptCommandAndSpeedResponsePriorityForAllIndices) {
  constexpr std::array<uint64_t, 3> kPeerIds = {
      0x1000000000000001ULL,
      0x2000000000000002ULL,
      0x3000000000000003ULL,
  };
  constexpr std::array<float, 3> kCommandValues = {1.25f, -3.5f, 42.0f};
  constexpr std::array<float, 3> kSpeedValues = {11.5f, -22.25f, 33.75f};

  for (uint8_t expected_idx = 0U; expected_idx < stfoc::kNumMotors;
       ++expected_idx) {
    auto cfg = board::GetCanDriverConfig(HAL_FDCAN_MODE_INTERNAL_LOOPBACK);
    cfg.device_id_override = kPeerIds[expected_idx];
    stfoc::StCanDriver can(cfg);
    CHECK_TRUE(can.Init() == stfoc::StCanDriver::Error::kOk);
    can.SetCurrentSpeed(kSpeedValues[expected_idx]);

    for (uint8_t other = 0U; other < stfoc::kNumMotors; ++other) {
      if (other != expected_idx) {
        SendAnnounceId(can, kPeerIds[other]);
      }
    }
    CHECK_TRUE(WaitUntil(
        [&] { return can.state() == stfoc::StCanDriver::State::kReady; }));
    CHECK_TRUE(can.motor_index().has_value());
    CHECK_EQUAL(expected_idx, *can.motor_index());

    SendControllerCommand(can, stfoc::CommandType::kVelocity, kCommandValues[0],
                          kCommandValues[1], kCommandValues[2]);
    CHECK_TRUE(WaitUntil([&] {
      return can.last_command().has_value() &&
             Peer::last_received_speed(can).has_value();
    }));

    CHECK_EQUAL(static_cast<int>(stfoc::CommandType::kVelocity),
                static_cast<int>(can.last_command()->command));
    DOUBLES_EQUAL(kCommandValues[expected_idx], can.last_command()->value,
                  1e-5);

    DOUBLES_EQUAL(kSpeedValues[expected_idx],
                  Peer::last_received_speed(can)->speed, 1e-5);
    const uint32_t expected_can_id =
        stfoc::kCanIdControllerCommand + 1U + expected_idx;
    CHECK_EQUAL(expected_can_id, Peer::last_received_speed_can_id(can));
  }
}
