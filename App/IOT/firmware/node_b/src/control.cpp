// control.cpp: trạng thái quạt/đèn/mode + số liệu Node A, có mutex, xuất ra phần cứng.
// Thân các hàm của control.h (CONTRACT mục 12) và control_ext.h.
//
// Luồng gọi:
//   - net task (lệnh MQTT)  -> cmd.cpp -> control_apply_cmd
//   - web task (W3, Khoa)   -> control_set_fan / control_set_light / control_set_mode
//   - espnow task           -> control_on_node_a
//   - control task (main)   -> control_tick mỗi 200 ms
// Cả 4 task cùng đọc/ghi s_ctrl, s_node_a nên MỌI hàm đều khóa s_mutex.
//
// Phần cứng (giống code lab 10/11, BTL_work/lab_test_2026-11-10/2_nodeB_full, đã chạy):
//   - Quạt: PWM chân 8, ledc kênh 0, 1 kHz, 8 bit, duty mức 0/1/2 = 0/150/255.
//   - Đèn: module LED RGB 4 bóng (NeoPixel) chân 10. "Đèn bật" = 4 bóng sáng trắng.
#include "control.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "control_ext.h"
#include "net.h"

#define FAN_PIN 8
#define FAN_LEDC_CH 0
#define FAN_PWM_FREQ 1000
#define FAN_PWM_RES 8
static const uint8_t FAN_DUTY[3] = {0, 150, 255};  // lab 10/11: 3 mức đều quay

#define RGB_PIN 10
#define RGB_COUNT 4
#define RGB_BRIGHTNESS 80  // 0..255, lab 10/11 dùng 80 (đủ sáng, không chói)

static SemaphoreHandle_t s_mutex;
static CtrlLogic s_ctrl;
static NodeAData s_node_a;           // số liệu Node A mới nhất
static uint32_t s_node_a_last_ms;    // millis() lúc nhận PKT_DATA mới nhất
static Adafruit_NeoPixel s_rgb(RGB_COUNT, RGB_PIN, NEO_GRB + NEO_KHZ800);

// Trạng thái ĐÃ xuất ra chân, để chỉ ghi phần cứng khi có đổi (NeoPixel show() tốn
// ~0,1 ms và tắt ngắt, gọi 5 lần/giây khi không đổi gì là phí).
static uint8_t s_out_fan = 255;  // 255 = chưa xuất lần nào -> lần đầu chắc chắn ghi
static int8_t s_out_light = -1;

// ---------- VIẾT SẴN: khóa / mở khóa ----------
// Mutex (không phải critical section) vì bên trong có gọi NeoPixel show() và ledcWrite;
// critical section tắt ngắt + cấm đổi task, giữ lâu sẽ làm WiFi/ESP-NOW trễ.
// portMAX_DELAY: chờ tới khi lấy được. Các đoạn giữ khóa đều rất ngắn (< 1 ms).
static void lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(s_mutex); }

// ---------- VIẾT SẴN: xuất s_ctrl ra phần cứng ----------
// Gọi khi ĐANG giữ khóa (để 2 task không gọi show() chồng nhau).
// Chỉ ghi phần nào khác với lần xuất trước.
static void apply_outputs_locked() {
  if (s_ctrl.fan != s_out_fan) {
    ledcWrite(FAN_LEDC_CH, FAN_DUTY[s_ctrl.fan]);
    s_out_fan = s_ctrl.fan;
  }
  if ((int8_t)s_ctrl.light != s_out_light) {
    uint32_t c = s_ctrl.light ? s_rgb.Color(255, 255, 255) : 0;
    s_rgb.fill(c);
    s_rgb.show();
    s_out_light = (int8_t)s_ctrl.light;
  }
}

// ---------- VIẾT SẴN: so trạng thái trước/sau để biết có đổi không ----------
static bool same_state(const CtrlLogic &a, const CtrlLogic &b) {
  return a.fan == b.fan && a.light == b.light && a.mode == b.mode;
}

// ---------- VIẾT SẴN: control_init ----------
// Thứ tự: mutex trước (các hàm sau đều dùng), rồi trạng thái logic, rồi phần cứng.
// ledcSetup/ledcAttachPin là API core 2.x (espressif32@6.9.0); core 3.x đổi tên, KHÔNG dùng.
void control_init() {
  s_mutex = xSemaphoreCreateMutex();
  ctrl_init(&s_ctrl);
  memset(&s_node_a, 0, sizeof(s_node_a));
  s_node_a.online = false;
  s_node_a_last_ms = 0;

  ledcSetup(FAN_LEDC_CH, FAN_PWM_FREQ, FAN_PWM_RES);
  ledcAttachPin(FAN_PIN, FAN_LEDC_CH);
  s_rgb.begin();
  s_rgb.setBrightness(RGB_BRIGHTNESS);

  lock();
  apply_outputs_locked();  // xuất "tắt hết" ngay lúc boot
  unlock();
}

// =====================================================================================
// control.h (CONTRACT mục 12) - webserver gọi
// =====================================================================================

// TODO 10: 3 hàm webserver gọi. Cả 3 cùng 1 khuôn 8 dòng, khác nhau đúng 1 dòng "điền".
// Khuôn: khóa -> nhớ trạng thái cũ -> gọi hàm logic -> xuất ra quạt/đèn -> so cũ/mới -> mở khóa
//        -> có đổi thì xin gửi telemetry -> trả true nếu lệnh thành công.
bool control_set_fan(uint8_t level, CmdSource src) {
  lock();
  CtrlLogic before = s_ctrl;
  CmdErr e = CMD_BAD_PARAMS;
  e = ctrl_set_fan(&s_ctrl, level, src == CmdSource::Rule, millis());
  apply_outputs_locked();
  bool changed = !same_state(before, s_ctrl);
  unlock();
  if (changed) net_request_telemetry();
  return e == CMD_OK;
}

bool control_set_light(bool on, CmdSource src) {
  lock();
  CtrlLogic before = s_ctrl;
  CmdErr e = CMD_BAD_PARAMS;
  e = ctrl_set_light(&s_ctrl, on, src == CmdSource::Rule, millis());
  apply_outputs_locked();
  bool changed = !same_state(before, s_ctrl);
  unlock();
  if (changed) net_request_telemetry();
  return e == CMD_OK;
}

bool control_set_mode(Mode m) {
  lock();
  CtrlLogic before = s_ctrl;
  CmdErr e = CMD_BAD_PARAMS;
  e = ctrl_set_mode(&s_ctrl, static_cast<LMode>(m), millis());
  apply_outputs_locked();
  bool changed = !same_state(before, s_ctrl);
  unlock();
  if (changed) net_request_telemetry();
  return e == CMD_OK;
}

// VIẾT SẴN: trả bản sao trạng thái (copy trong khóa, dùng ngoài khóa).
ControlState control_get_state() {
  lock();
  ControlState st = {s_ctrl.fan, s_ctrl.light, static_cast<Mode>(s_ctrl.mode)};
  unlock();
  return st;
}

// VIẾT SẴN: số liệu cảm biến cho webserver. valid = false khi Node A mất / chưa có gói.
SensorSnapshot control_get_sensors() {
  lock();
  SensorSnapshot s;
  s.valid = s_node_a.online;
  s.temperature = s_node_a.temperature;
  s.humidity = s_node_a.humidity;
  s.light = s_node_a.light;
  s.presence = s_node_a.presence;
  s.anomaly = s_node_a.anomaly;
  s.anomaly_score = s_node_a.anomaly_score;
  unlock();
  return s;
}

// VIẾT SẴN
const char *mode_to_str(Mode m) { return lmode_str(static_cast<LMode>(m)); }

// =====================================================================================
// control_ext.h - code Node B gọi
// =====================================================================================

void control_on_node_a(const PktData *d, float temp_avg, float loss_pct) {
  lock();
  s_node_a.seq = d->seq;
  s_node_a.temperature = temp_avg;  // lấy số TRUNG BÌNH, không lấy d->temperature
  s_node_a.humidity = d->humidity;
  s_node_a.light = d->light;
  s_node_a.anomaly_score = d->anomaly_score;
  s_node_a.dht_fail = d->dht_fail;
  s_node_a.presence = d->presence != 0;
  s_node_a.anomaly = d->anomaly != 0;
  s_node_a.espnow_loss = loss_pct;
  s_node_a.online = true;
  s_node_a_last_ms = millis();  // mốc để control_tick biết Node A còn sống không
  unlock();
}

// VIẾT SẴN
NodeAData control_get_node_a() {
  lock();
  NodeAData n = s_node_a;
  unlock();
  return n;
}

// VIẾT SẴN
void control_get_lstate(uint8_t *fan, bool *light, LMode *mode) {
  lock();
  *fan = s_ctrl.fan;
  *light = s_ctrl.light;
  *mode = s_ctrl.mode;
  unlock();
}

CmdErr control_apply_cmd(const ParsedCmd *c) {
  uint32_t now = millis();
  lock();
  CtrlLogic before = s_ctrl;
  CmdErr e;
  switch (c->method) {
    case M_SET_FAN:  e = ctrl_set_fan(&s_ctrl, c->level, false, now); break;  // lệnh tay
    case M_AUTO_FAN: e = ctrl_set_fan(&s_ctrl, c->level, true, now); break;   // lệnh rule
    case M_SET_LIGHT: e = ctrl_set_light(&s_ctrl, c->on, false, now); break;
    case M_AUTO_LIGHT: e = ctrl_set_light(&s_ctrl, c->on, true, now); break;
    case M_SET_MODE: e = ctrl_set_mode(&s_ctrl, c->mode, now); break;
    case M_GET_STATE: e = CMD_OK; break;
    default:         e = CMD_UNKNOWN_METHOD; break;
  }
  apply_outputs_locked();
  bool changed = !same_state(before, s_ctrl);
  unlock();
  if (changed) net_request_telemetry();
  return e;
}

bool control_tick(uint32_t manual_timeout_ms) {
  uint32_t now = millis();
  bool lost = false;  // Node A vừa bị coi là mất trong lần gọi này?
  lock();
  // Bước 1: Node A im quá 10 s -> mất
  if (s_node_a.online && now - s_node_a_last_ms > NODE_A_TIMEOUT_MS) {
    s_node_a.online = false; 
    lost = true;
  }
  // Bước 2: áp luật phòng trống / hết giờ manual
  CtrlLogic before = s_ctrl;
  ctrl_tick(&s_ctrl, s_node_a.online, s_node_a.presence, now, manual_timeout_ms);
  apply_outputs_locked();
  bool changed = lost || !same_state(before, s_ctrl);
  unlock();
  if (lost) Serial.println("[B] node_a_online = false");  // in SAU unlock (Serial chậm)
  return changed;
}
