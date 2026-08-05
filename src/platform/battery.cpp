#include "platform/battery.h"

#include <cerrno>
#include <cstdlib>

#include "platform/clock.h"

#ifdef MIYONOS_ONIONOS
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace miyonos {

namespace {

constexpr uint64_t kCacheDurationMs = 5000;
#ifdef MIYONOS_ONIONOS
constexpr uint8_t kAxp223Address = 0x34;
constexpr uint8_t kAxp223FuelGaugeRegister = 0xb9;
#endif

int battery_percent_from_environment() {
  const char* value = std::getenv("MIYONOS_BATTERY_PERCENT");
  if (!value || value[0] == '\0') return -1;
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(value, &end, 10);
  if (errno != 0 || !end || *end != '\0' || parsed < 0 || parsed > 100)
    return -1;
  return static_cast<int>(parsed);
}

#ifdef MIYONOS_ONIONOS
int battery_percent_from_hardware() {
  const int device = open("/dev/i2c-1", O_RDWR | O_CLOEXEC);
  if (device < 0) return -1;

  uint8_t register_address = kAxp223FuelGaugeRegister;
  uint8_t value = 0;
  i2c_msg messages[2]{};
  messages[0].addr = kAxp223Address;
  messages[0].len = 1;
  messages[0].buf = &register_address;
  messages[1].addr = kAxp223Address;
  messages[1].flags = I2C_M_RD;
  messages[1].len = 1;
  messages[1].buf = &value;
  i2c_rdwr_ioctl_data request{};
  request.msgs = messages;
  request.nmsgs = 2;
  const bool read_ok = ioctl(device, I2C_RDWR, &request) >= 0;
  close(device);
  return read_ok ? battery_percent_from_gauge_register(value) : -1;
}
#endif

}  // namespace

int battery_percent_from_gauge_register(uint8_t value) {
  const int percent = value & 0x7f;
  return percent <= 100 ? percent : -1;
}

BatteryStatus device_battery_status() {
  static BatteryStatus cached;
  static uint64_t cached_at_ms = 0;
  const uint64_t now = monotonic_ms();
  if (cached_at_ms != 0 && now - cached_at_ms < kCacheDurationMs) return cached;

  cached_at_ms = now;
  cached.percent = battery_percent_from_environment();
#ifdef MIYONOS_ONIONOS
  if (!cached.available()) cached.percent = battery_percent_from_hardware();
#endif
  return cached;
}

}  // namespace miyonos
