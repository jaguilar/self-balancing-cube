#include <cstdint>
#include <cstdio>

#include "CppUTest/TestHarness.h"
#include "i2c.h"

extern I2C_HandleTypeDef hi2c1;

namespace {

// ICM-42670-P Registers (User Bank 0)
constexpr uint8_t REG_MCLK_RDY = 0x00;
constexpr uint8_t REG_SIGNAL_PATH_RESET = 0x02;
constexpr uint8_t REG_PWR_MGMT0 = 0x1F;
constexpr uint8_t REG_WHO_AM_I = 0x75;

// Expected WHO_AM_I register value for ICM-42670-P
constexpr uint8_t ICM42670P_WHO_AM_I_VAL = 0x67;

// 7-bit I2C addresses (depends on AD0 / AP_AD0 pin level)
constexpr uint8_t ICM42670P_I2C_ADDR_AD0_LOW = 0x68;
constexpr uint8_t ICM42670P_I2C_ADDR_AD0_HIGH = 0x69;

}  // namespace

TEST_GROUP(Icm42670pTest){void setup(){} void teardown(){}};

TEST(Icm42670pTest, I2C1BusScan) {
  printf("\n--- I2C1 Bus Scan ---\n");
  int found_count = 0;

  for (uint16_t addr = 0x08; addr <= 0x77; ++addr) {
    // HAL expects left-shifted 8-bit address
    HAL_StatusTypeDef status =
        HAL_I2C_IsDeviceReady(&hi2c1, static_cast<uint16_t>(addr << 1), 1, 10);
    if (status == HAL_OK) {
      printf("  Found device at 7-bit addr: 0x%02X (8-bit: 0x%02X)\n", addr,
             addr << 1);
      found_count++;
    }
  }

  printf("I2C1 Bus Scan complete: %d device(s) found.\n", found_count);
  CHECK_TEXT(found_count > 0, "No I2C devices responded on I2C1");
}

TEST(Icm42670pTest, QueryWhoAmI) {
  printf("\n--- ICM-42670-P WHO_AM_I Query ---\n");

  const uint8_t candidate_addrs[] = {
      ICM42670P_I2C_ADDR_AD0_LOW,
      ICM42670P_I2C_ADDR_AD0_HIGH,
  };

  uint8_t active_addr = 0;
  uint8_t who_am_i = 0;

  for (uint8_t addr : candidate_addrs) {
    uint8_t read_val = 0;
    HAL_StatusTypeDef status =
        HAL_I2C_Mem_Read(&hi2c1, static_cast<uint16_t>(addr << 1), REG_WHO_AM_I,
                         I2C_MEMADD_SIZE_8BIT, &read_val, 1, 100);

    if (status == HAL_OK) {
      printf(
          "  Address 0x%02X responded: WHO_AM_I = 0x%02X (expected 0x%02X)\n",
          addr, read_val, ICM42670P_WHO_AM_I_VAL);
      if (read_val == ICM42670P_WHO_AM_I_VAL) {
        active_addr = addr;
        who_am_i = read_val;
        break;
      }
    } else {
      printf("  Address 0x%02X did not respond (HAL status %d)\n", addr,
             status);
    }
  }

  CHECK_TEXT(active_addr != 0,
             "ICM-42670-P was not detected at 0x68 or 0x69 with expected "
             "WHO_AM_I 0x67");
  BYTES_EQUAL(ICM42670P_WHO_AM_I_VAL, who_am_i);
  printf("ICM-42670-P verified live at 7-bit address 0x%02X!\n", active_addr);

  // Query PWR_MGMT0 register (0x1F) to verify read access to control registers
  uint8_t pwr_mgmt0 = 0xFF;
  HAL_StatusTypeDef pwr_status =
      HAL_I2C_Mem_Read(&hi2c1, static_cast<uint16_t>(active_addr << 1),
                       REG_PWR_MGMT0, I2C_MEMADD_SIZE_8BIT, &pwr_mgmt0, 1, 100);
  if (pwr_status == HAL_OK) {
    printf("  PWR_MGMT0 register (0x1F) = 0x%02X\n", pwr_mgmt0);
  } else {
    printf("  Warning: failed to read PWR_MGMT0 (HAL status %d)\n", pwr_status);
  }
}
