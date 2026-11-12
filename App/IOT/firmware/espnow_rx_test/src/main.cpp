// espnow_rx_test: Node B giả, chỉ nhận ESP-NOW từ Node A (#26).
// Quy tắc nhận theo CONTRACT mục 3: callback chỉ kiểm len/magic/version rồi đẩy vào queue;
// seq trùng -> bỏ; seq nhảy cóc -> cộng mất; seq nhỏ hơn -> Node A vừa khởi động lại, đếm lại;
// không nhận PKT_DATA quá 10 s -> node_a_online = false.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "espnow_packet.h"
#include "secrets.h"

#define LED_PIN GPIO_NUM_48
#define NODE_A_TIMEOUT_MS 10000

typedef struct {
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[sizeof(PktData)];  // gói lớn nhất Node A gửi
} RxItem;

QueueHandle_t rx_queue;
volatile uint32_t bad_pkt = 0, queue_full = 0;

uint32_t rx = 0, lost = 0, dup = 0, pairs = 0, a_resets = 0;
int64_t expected_seq = -1;
uint32_t last_data_ms = 0;
bool node_a_online = false;
PktData last = {};

// ---------- Callback: chỉ kiểm nhanh rồi đẩy queue ----------
void on_recv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len < (int)sizeof(PktHeader) || len > (int)sizeof(PktData)) {
    bad_pkt++;
    return;
  }
  PktHeader h;
  memcpy(&h, data, sizeof(h));
  if (h.magic != ESPNOW_MAGIC || h.version != ESPNOW_VERSION) {
    bad_pkt++;
    return;
  }
  RxItem it;
  memcpy(it.mac, mac, 6);
  it.len = (uint8_t)len;
  memcpy(it.data, data, len);
  if (xQueueSend(rx_queue, &it, 0) != pdTRUE) queue_full++;
}

void handle_pair_req(const RxItem &it) {
  if (it.len != sizeof(PktPairReq)) {
    bad_pkt++;
    return;
  }
  if (!esp_now_is_peer_exist(it.mac)) {
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, it.mac, 6);
    p.channel = 0;
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    if (esp_now_add_peer(&p) != ESP_OK) {
      Serial.println("[B] LOI esp_now_add_peer");
      return;
    }
  }
  PktPairResp r = {};
  r.hdr.magic = ESPNOW_MAGIC;
  r.hdr.version = ESPNOW_VERSION;
  r.hdr.type = PKT_PAIR_RESP;
  r.channel = WiFi.channel();
  esp_now_send(it.mac, (const uint8_t *)&r, sizeof(r));
  pairs++;
  Serial.printf("[B] Node A %02X:%02X:%02X:%02X:%02X:%02X xin ghep cap -> tra loi ch=%d (lan %lu)\n",
                it.mac[0], it.mac[1], it.mac[2], it.mac[3], it.mac[4], it.mac[5], r.channel,
                (unsigned long)pairs);
}

void handle_data(const RxItem &it) {
  if (it.len != sizeof(PktData)) {
    bad_pkt++;
    return;
  }
  PktData d;
  memcpy(&d, it.data, sizeof(d));
  int64_t s = d.seq;

  if (expected_seq >= 0 && s == expected_seq - 1) {
    dup++;  // gói gửi lại (Node A mất ACK nhưng B đã nhận)
    return;
  }
  if (expected_seq >= 0 && s < expected_seq - 1) {
    Serial.printf("[B] seq %lld < truoc -> Node A vua khoi dong lai, dem lai\n", (long long)s);
    rx = lost = dup = 0;
    a_resets++;
  } else if (expected_seq >= 0 && s > expected_seq) {
    lost += (uint32_t)(s - expected_seq);
  }
  expected_seq = s + 1;
  rx++;
  last = d;
  last_data_ms = millis();
  if (!node_a_online) Serial.println("[B] node_a_online = true");
  node_a_online = true;
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  Serial.printf("[B] seq=%lu T=%.1f H=%.1f light=%u presence=%u anomaly=%u score=%.3f dht_fail=%u\n",
                (unsigned long)d.seq, d.temperature, d.humidity, d.light, d.presence, d.anomaly,
                d.anomaly_score, d.dht_fail);
}

void task_process(void *pvParameters) {
  RxItem it;
  while (1) {
    if (xQueueReceive(rx_queue, &it, portMAX_DELAY) != pdTRUE) continue;
    PktHeader h;
    memcpy(&h, it.data, sizeof(h));
    if (h.type == PKT_PAIR_REQ) handle_pair_req(it);
    else if (h.type == PKT_DATA) handle_data(it);
    else bad_pkt++;
  }
}

// ---------- WiFi watchdog + in thống kê ----------
void task_status(void *pvParameters) {
  uint32_t wifi_lost_at = 0, last_print = 0;
  while (1) {
    if (WiFi.status() != WL_CONNECTED) {
      if (wifi_lost_at == 0) {
        wifi_lost_at = millis();
      } else if (millis() - wifi_lost_at > 10000) {
        Serial.println("[B] Mat WiFi >10s -> WiFi.begin lai");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        wifi_lost_at = millis();
      }
    } else if (wifi_lost_at != 0) {
      Serial.printf("[B] WiFi ket noi lai OK ch=%d\n", WiFi.channel());
      wifi_lost_at = 0;
    }

    if (node_a_online && millis() - last_data_ms > NODE_A_TIMEOUT_MS) {
      node_a_online = false;
      Serial.println("[B] node_a_online = false (qua 10 s khong co PKT_DATA)");
    }

    if (millis() - last_print > 10000) {
      last_print = millis();
      uint32_t total = rx + lost;
      Serial.printf("[B] === WiFi %s ch=%d | nhan=%lu mat=%lu trung=%lu loss=%.2f%% | ghep_cap=%lu "
                    "A_reset=%lu bad=%lu qfull=%lu | node_a_online=%d\n",
                    WiFi.status() == WL_CONNECTED ? "OK" : "MAT", WiFi.channel(), (unsigned long)rx,
                    (unsigned long)lost, (unsigned long)dup, total ? 100.0f * lost / total : 0.0f,
                    (unsigned long)pairs, (unsigned long)a_resets, (unsigned long)bad_pkt,
                    (unsigned long)queue_full, node_a_online);
    }
    vTaskDelay(50);
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  pinMode(LED_PIN, OUTPUT);
  Serial.println("===== ESPNOW RX TEST (Node B gia) =====");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // không ngủ modem, tránh rớt gói ESP-NOW
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[B] Dang ket noi '%s'", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[B] WiFi OK ch=%d MAC=%s\n", WiFi.channel(), WiFi.macAddress().c_str());

  rx_queue = xQueueCreate(20, sizeof(RxItem));
  if (esp_now_init() != ESP_OK) {
    Serial.println("[B] FAIL esp_now_init");
    return;
  }
  esp_now_register_recv_cb(on_recv);

  xTaskCreate(task_process, "task_process", 4096, NULL, 3, NULL);
  xTaskCreate(task_status, "task_status", 4096, NULL, 1, NULL);
}

void loop() {}
