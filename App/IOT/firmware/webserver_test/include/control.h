#pragma once
#include <stdint.h>

enum class Mode : uint8_t { Auto, Manual, Offline };
enum class CmdSource : uint8_t { Rule, Dashboard, LocalUi, Web, Serial };

struct ControlState {
  uint8_t fan_level;    // 0..2
  bool    light_on;
  Mode    mode;
};

struct SensorSnapshot {
  bool     valid;        // false nếu chưa có gói nào hoặc Node A mất
  float    temperature;
  float    humidity;
  uint16_t light;
  bool     presence;
  bool     anomaly;
  float    anomaly_score;
};

// Thread-safe (có mutex bên trong), gọi từ task nào cũng được.
// Trả false nếu params sai, hoặc lệnh Rule bị bỏ vì không ở auto.
bool control_set_fan(uint8_t level, CmdSource src);
bool control_set_light(bool on, CmdSource src);
bool control_set_mode(Mode m);              // chỉ Auto hoặc Manual
ControlState   control_get_state();
SensorSnapshot control_get_sensors();
const char*    mode_to_str(Mode m);         // "auto" / "manual" / "offline"
