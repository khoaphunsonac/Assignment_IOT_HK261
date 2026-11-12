// Bản giả của control.h cho webserver đứng riêng (issue #33).
// W2 Minh thay file này bằng module điều khiển thật của Node B, web.cpp giữ nguyên.

#include <Arduino.h>
#include "control.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static ControlState s_state = {0, false, Mode::Auto};

static SensorSnapshot s_sensors = {true, 29.0f, 65.0f, 1800, true, false, 0.1f};
static uint32_t s_last_sensor_ms = 0;

static float drift(float v, float step, float lo, float hi) {
  v += (random(-100, 101) / 100.0f) * step;
  return constrain(v, lo, hi);
}

bool control_set_fan(uint8_t level, CmdSource src) {
  if (level > 2) return false;
  bool ok = true;
  taskENTER_CRITICAL(&s_mux);
  if (src == CmdSource::Rule) {
    if (s_state.mode == Mode::Auto) s_state.fan_level = level;
    else ok = false;  // ignored_not_auto
  } else {
    s_state.fan_level = level;
    s_state.mode = Mode::Manual;  // lệnh tay chuyển sang manual (CONTRACT 6.2)
  }
  taskEXIT_CRITICAL(&s_mux);
  return ok;
}

bool control_set_light(bool on, CmdSource src) {
  bool ok = true;
  taskENTER_CRITICAL(&s_mux);
  if (src == CmdSource::Rule) {
    if (s_state.mode == Mode::Auto) s_state.light_on = on;
    else ok = false;
  } else {
    s_state.light_on = on;
    s_state.mode = Mode::Manual;
  }
  taskEXIT_CRITICAL(&s_mux);
  return ok;
}

bool control_set_mode(Mode m) {
  if (m != Mode::Auto && m != Mode::Manual) return false;  // không đặt được Offline
  taskENTER_CRITICAL(&s_mux);
  s_state.mode = m;
  taskEXIT_CRITICAL(&s_mux);
  return true;
}

ControlState control_get_state() {
  taskENTER_CRITICAL(&s_mux);
  ControlState st = s_state;
  taskEXIT_CRITICAL(&s_mux);
  return st;
}

SensorSnapshot control_get_sensors() {
  // Số giả đổi chậm, 2 giây một mẫu như Node A thật. random() gọi ngoài vùng khóa.
  uint32_t now = millis();
  SensorSnapshot next;
  taskENTER_CRITICAL(&s_mux);
  next = s_sensors;
  bool due = (now - s_last_sensor_ms >= 2000);
  taskEXIT_CRITICAL(&s_mux);
  if (!due) return next;

  next.temperature = drift(next.temperature, 0.3f, 26.0f, 35.0f);
  next.humidity = drift(next.humidity, 1.5f, 50.0f, 80.0f);
  next.light = (uint16_t)drift(next.light, 150.0f, 500.0f, 3500.0f);
  next.presence = random(100) < 80;
  next.anomaly = random(100) < 5;
  next.anomaly_score = next.anomaly ? random(700, 1000) / 1000.0f : random(0, 300) / 1000.0f;

  taskENTER_CRITICAL(&s_mux);
  s_sensors = next;
  s_last_sensor_ms = now;
  taskEXIT_CRITICAL(&s_mux);
  return next;
}

const char* mode_to_str(Mode m) {
  switch (m) {
    case Mode::Auto: return "auto";
    case Mode::Manual: return "manual";
    case Mode::Offline: return "offline";
  }
  return "auto";
}
