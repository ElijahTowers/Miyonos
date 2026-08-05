#pragma once

#include <cstdint>

namespace miyonos {

// A negative value means that the hardware gauge is not available.
struct BatteryStatus {
  int percent = -1;

  bool available() const { return percent >= 0 && percent <= 100; }
};

// Decodes the AXP223 fuel-gauge byte without accessing hardware.
int battery_percent_from_gauge_register(uint8_t value);

// Reads the Miyoo Mini Plus fuel gauge at a bounded, cached interval. Desktop
// builds return unavailable unless MIYONOS_BATTERY_PERCENT is set for testing.
BatteryStatus device_battery_status();

}  // namespace miyonos
