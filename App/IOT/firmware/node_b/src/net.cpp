// net.cpp - xem net.h. CONTRACT mục 4 (topic), 5 (telemetry), 8 (online/offline).
#include "net.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <sys/time.h>

#include "cmd.h"
#include "config_store.h"
#include "control_ext.h"
#include "node_b_logic.h"
#include "secrets.h"
#include "version.h"

#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "room1-node-b"   // CONTRACT 4: không được trùng client khác
#define MQTT_RETRY_MS 5000              // nối MQTT hụt thì 5 s sau thử lại
#define WIFI_LOST_RESTART_MS 10000      // mất WiFi > 10 s -> WiFi.begin lại (lab 10/11, B8)
#define TELEMETRY_OFFLINE_MS 5000       // CONTRACT 5: Node A mất thì 5 s gửi 1 lần
#define TELEMETRY_BUF 512               // bản đầy đủ ~330 byte

static const char *T_TELEMETRY = "room1/telemetry";
static const char *T_CMD = "room1/cmd";
static const char *T_CONFIG = "room1/config";
static const char *T_STATUS = "room1/status";
static const char *T_GW_STATUS = "room1/gateway/status";
static const char *LWT_OFFLINE = "{\"state\":\"offline\"}";

static WiFiClient s_wifi_client;
static PubSubClient s_mqtt(s_wifi_client);
static volatile bool s_tel_req = false;     // cờ "xin gửi telemetry"
static volatile bool s_gw_online = false;

// ---------- VIẾT SẴN ----------
void net_request_telemetry() { s_tel_req = true; }
bool net_gateway_online() { return s_gw_online; }

bool net_publish(const char *topic, const char *payload, bool retained) {
  if (!s_mqtt.connected()) return false;
  return s_mqtt.publish(topic, payload, retained);
}

// VIẾT SẴN: giờ NTP dạng epoch ms. Ra false nếu chưa đồng bộ (đồng hồ còn ở năm 1970).
// 1700000000 s = 11/2023: nhỏ hơn mốc này chắc chắn là chưa có NTP.
static bool now_epoch_ms(uint64_t *ms) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  if (tv.tv_sec < 1700000000) return false;
  *ms = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
  return true;
}

// =====================================================================================
// Callback MQTT: chạy BÊN TRONG s_mqtt.loop(), tức trong task_net.
// =====================================================================================
//   Vào : topic = chuỗi C có '\0'; payload/length = nội dung, KHÔNG có '\0' cuối
//         (đừng dùng strlen hay printf("%s") trên payload).
static void on_message(char *topic, uint8_t *payload, unsigned int length) {
  // TODO 14: tin MQTT tới -> chuyển cho đúng nơi xử lý.
  //   payload KHÔNG có '\0' ở cuối: luôn dùng kèm length, đừng printf("%s", payload).
  if (strcmp(topic, T_CMD) == 0) {
    cmd_handle((const char *)payload, length);
  } else if (strcmp(topic, T_CONFIG) == 0) {
    // điền: config_store_apply_json((const char *)payload, length);
  } else if (strcmp(topic, T_GW_STATUS) == 0) {
    // payload ví dụ: {"state":"online"} hoặc {"state":"offline"}
    char buf[64];
    size_t n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;  // dài quá thì cắt
    memcpy(buf, payload, n);
    buf[n] = '\0';  // thêm '\0' để dùng strstr được
    bool on = false;
    // điền: on = strstr(buf, "\"online\"") != NULL;
    if (on != s_gw_online) Serial.printf("[MQTT] gateway %s\n", on ? "online" : "offline");
    s_gw_online = on;
  }
}

// VIẾT SẴN: publish room1/status online (retained), CONTRACT 8:
//   {"state":"online","fw_version":"1.0.0","ip":"10.199.40.70"}
static void publish_online() {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"state\":\"online\",\"fw_version\":\"%s\",\"ip\":\"%s\"}",
           FW_VERSION, WiFi.localIP().toString().c_str());
  net_publish(T_STATUS, buf, true);
}

static bool mqtt_connect() {
  // TODO 15: nối broker, đăng ký topic, báo online.
  // Last Will: nhờ broker "nhắn hộ" {"state":"offline"} lên room1/status nếu Node B rớt
  // (rút điện, mất WiFi) mà không kịp nói gì. Các tham số: client id, topic, QoS, retained, nội dung.
  bool ok = false;
  // điền: ok = s_mqtt.connect(MQTT_CLIENT_ID, T_STATUS, 1, true, LWT_OFFLINE);
  if (!ok) {
    Serial.printf("FAIL rc=%d\n", s_mqtt.state());
    return false;
  }
  s_mqtt.subscribe(T_CMD, 1);
  // điền: subscribe T_CONFIG và T_GW_STATUS (QoS 1, giống dòng trên)
  publish_online();         // room1/status online (retained)
  net_request_telemetry();  // gửi ngay 1 bản telemetry
  Serial.println("OK");
  return true;
}

// VIẾT SẴN: watchdog WiFi (giống task_status của espnow_rx_test / lab 10/11 B8).
// setAutoReconnect(true) KHÔNG đủ: lab 10/11 tắt hotspot 30 s rồi bật lại, core không tự
// vào lại. Nên mất > 10 s thì tự gọi WiFi.disconnect() + WiFi.begin().
static void wifi_watchdog() {
  static uint32_t lost_at = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (lost_at == 0) {
      lost_at = millis();
      Serial.println("[WIFI] Mat ket noi");
    } else if (millis() - lost_at > WIFI_LOST_RESTART_MS) {
      Serial.println("[WIFI] Mat > 10 s -> WiFi.begin lai");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      lost_at = millis();
    }
  } else if (lost_at != 0) {
    Serial.printf("[WIFI] Ket noi lai OK IP=%s ch=%d\n", WiFi.localIP().toString().c_str(),
                  WiFi.channel());
    lost_at = 0;
  }
}

// Gom dữ liệu và publish 1 bản room1/telemetry.
static void publish_telemetry() {
  TelemetryIn in;
  memset(&in, 0, sizeof(in));
  // TODO 16: gom dữ liệu từ 3 nơi vào "in".
  // (1) Giờ NTP
  in.has_ts = now_epoch_ms(&in.ts_ms);
  // (2) Số liệu Node A
  NodeAData a = control_get_node_a();
  in.node_a_online = a.online;
  in.seq = a.seq;
  in.temperature = a.temperature;
  // điền: humidity, light, presence, anomaly, anomaly_score, dht_fail, espnow_loss (in.x = a.x)
  // (3) Trạng thái Node B
  control_get_lstate(&in.fan_level, &in.light_on, &in.mode);
  // điền: in.rssi = WiFi.RSSI();  in.fw_version = FW_VERSION;  in.uptime_s = millis() / 1000;
  // Phần dưới VIẾT SẴN: tạo JSON, publish, in lỗi nếu có.
  char buf[TELEMETRY_BUF];
  size_t n = telemetry_build(&in, buf, sizeof(buf));
  if (n == 0) {
    Serial.println("[MQTT] LOI telemetry_build (buffer nho?)");
    return;
  }
  if (!net_publish(T_TELEMETRY, buf, false)) Serial.println("[MQTT] LOI publish telemetry");
}

// VIẾT SẴN: net_init (gọi trong setup).
void net_init() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // không ngủ modem: vừa STA vừa ESP-NOW, ngủ thì rớt gói ESP-NOW
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WIFI] Dang ket noi '%s'", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\n[WIFI] OK IP=%s ch=%d MAC=%s\n", WiFi.localIP().toString().c_str(),
                  WiFi.channel(), WiFi.macAddress().c_str());
  else
    Serial.println("\n[WIFI] Chua ket noi duoc, task_net se thu lai");

  // NTP chạy nền, tự đồng bộ khi có Internet qua hotspot. Múi giờ không quan trọng vì ts
  // gửi dạng epoch (UTC). Chưa đồng bộ thì telemetry bỏ key ts (CONTRACT 2).
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
  s_mqtt.setBufferSize(1024);  // CONTRACT 4: mặc định 256 byte không đủ cho telemetry
  s_mqtt.setCallback(on_message);
}

// VIẾT SẴN: vòng lặp mạng. Thứ tự mỗi vòng (~10 ms):
//   (1) watchdog WiFi; mất WiFi thì chưa làm gì thêm.
//   (2) MQTT chưa nối -> thử nối, hụt thì 5 s sau thử lại (không chặn vòng lặp).
//   (3) s_mqtt.loop(): nhận tin (gọi on_message), giữ kết nối (keepalive).
//   (4) Gửi telemetry khi có cờ, hoặc mỗi 5 s nếu Node A mất.
void task_net(void *pvParameters) {
  (void)pvParameters;
  uint32_t last_try = 0, last_offline_tel = 0;
  while (1) {
    wifi_watchdog();
    if (WiFi.status() == WL_CONNECTED) {
      if (!s_mqtt.connected() && millis() - last_try > MQTT_RETRY_MS) {
        last_try = millis();
        Serial.printf("[MQTT] Ket noi %s:%d ... ", MQTT_HOST, MQTT_PORT);
        mqtt_connect();
      }
      s_mqtt.loop();

      bool node_a_online = control_get_node_a().online;
      if (!node_a_online && millis() - last_offline_tel >= TELEMETRY_OFFLINE_MS) {
        last_offline_tel = millis();
        s_tel_req = true;
      }
      if (s_tel_req && s_mqtt.connected()) {
        s_tel_req = false;  // xóa cờ TRƯỚC khi gửi: task khác bật lại trong lúc gửi thì vòng sau gửi tiếp
        publish_telemetry();
      }
    }
    vTaskDelay(10);
  }
}
