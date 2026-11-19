// espnow_rx.cpp - xem espnow_rx.h.
// Nguồn: on_recv, handle_pair_req, cấu trúc RxItem chép từ espnow_rx_test/src/main.cpp
// (#26, chạy đúng ở lab 13/11). Phần đếm seq chuyển sang node_b_logic (seq_on_packet).
#include "espnow_rx.h"

#include <WiFi.h>
#include <esp_now.h>

#include "control_ext.h"
#include "espnow_packet.h"
#include "net.h"
#include "node_b_logic.h"

#define LED_PIN GPIO_NUM_48  // LED trên board, nháy mỗi gói PKT_DATA mới
#define RX_QUEUE_LEN 20
#define STATS_PERIOD_MS 10000

// Một phần tử queue: MAC người gửi + gói thô. data đủ chứa gói lớn nhất Node A gửi.
typedef struct {
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[sizeof(PktData)];
} RxItem;

static QueueHandle_t s_rx_queue;
// Đếm lỗi trong callback (chạy ở task WiFi, khác task_espnow) -> volatile.
static volatile uint32_t s_bad_pkt = 0, s_queue_full = 0;
// Chỉ task_espnow dùng:
static SeqTracker s_seq;
static TempAvg s_tavg;
static uint32_t s_pairs = 0;

// ---------- Callback nhận (chép từ espnow_rx_test) ----------
// Chạy trong task WiFi của hệ thống: phải ngắn, không printf, không chờ.
// CONTRACT 3: chỉ kiểm len, magic, version rồi đẩy queue. Gói lạ (thiết bị khác trong
// phòng lab cũng dùng ESP-NOW) bị loại ở bước magic.
// Chữ ký (const uint8_t *mac, ...) là của core 2.x (espressif32@6.9.0); core 3.x đổi
// thành (const esp_now_recv_info_t *info, ...), KHÔNG dùng.
static void on_recv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len < (int)sizeof(PktHeader) || len > (int)sizeof(PktData)) {
    s_bad_pkt++;
    return;
  }
  PktHeader h;
  memcpy(&h, data, sizeof(h));  // memcpy vì data có thể không căn lề 4 byte
  if (h.magic != ESPNOW_MAGIC || h.version != ESPNOW_VERSION) {
    s_bad_pkt++;
    return;
  }
  RxItem it;
  memcpy(it.mac, mac, 6);
  it.len = (uint8_t)len;
  memcpy(it.data, data, len);
  if (xQueueSend(s_rx_queue, &it, 0) != pdTRUE) s_queue_full++;  // đầy thì bỏ, không chờ
}

// ---------- Trả lời ghép cặp (chép từ espnow_rx_test) ----------
// Node A broadcast PKT_PAIR_REQ. Node B thêm MAC Node A làm peer (nếu chưa có) để gửi
// unicast được, rồi trả PKT_PAIR_RESP kèm channel WiFi hiện tại (= channel hotspot).
// p.channel = 0: dùng channel hiện tại của radio (tránh lệch khi hotspot đổi channel).
static void handle_pair_req(const RxItem &it) {
  if (it.len != sizeof(PktPairReq)) {
    s_bad_pkt++;
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
  s_pairs++;
  Serial.printf("[B] Node A %02X:%02X:%02X:%02X:%02X:%02X xin ghep cap -> tra loi ch=%d (lan %lu)\n",
                it.mac[0], it.mac[1], it.mac[2], it.mac[3], it.mac[4], it.mac[5], r.channel,
                (unsigned long)s_pairs);
  // #43 (sau): gửi PKT_CONFIG (empty_timeout_s) ngay sau khi ghép cặp.
}

// ---------- Xử lý 1 gói PKT_DATA ----------
// Bỏ gói trùng -> Node A khởi động lại thì tính lại nhiệt độ TB -> cập nhật control
// -> xin gửi telemetry -> nháy LED, in 1 dòng log.
static void handle_data(const RxItem &it) {
  if (it.len != sizeof(PktData)) {  // sai kích thước -> bỏ
    s_bad_pkt++;
    return;
  }
  PktData d;
  memcpy(&d, it.data, sizeof(d));  // memcpy vì it.data không căn lề cho float

  SeqResult r = seq_on_packet(&s_seq, d.seq);
  if (r == SEQ_DUP) {
    return;  // gói gửi lại, đã xử lý lần trước
  }
  if (r == SEQ_RESTART) {
    temp_avg_init(&s_tavg);
    Serial.println("[B] Node A vua khoi dong lai, dem lai");
  }
  float t = temp_avg_push(&s_tavg, d.temperature);
  control_on_node_a(&d, t, seq_loss_pct(&s_seq));
  net_request_telemetry();
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));  // nháy LED mỗi gói
  Serial.printf("[B] seq=%lu T=%.1f(avg %.1f) H=%.1f light=%u presence=%u anomaly=%u score=%.3f dht_fail=%u\n",
                (unsigned long)d.seq, d.temperature, t, d.humidity, d.light, d.presence,
                d.anomaly, d.anomaly_score, d.dht_fail);
}

// Dòng thống kê 10 s, cùng dạng espnow_rx_test để so với lab 13/11.
static void print_stats() {
  Serial.printf("[B] === ESPNOW nhan=%lu mat=%lu trung=%lu loss=%.2f%% | ghep_cap=%lu A_reset=%lu "
                "bad=%lu qfull=%lu | WiFi ch=%d\n",
                (unsigned long)s_seq.rx, (unsigned long)s_seq.lost, (unsigned long)s_seq.dup,
                seq_loss_pct(&s_seq), (unsigned long)s_pairs, (unsigned long)s_seq.a_resets,
                (unsigned long)s_bad_pkt, (unsigned long)s_queue_full, WiFi.channel());
}

bool espnow_rx_init() {
  pinMode(LED_PIN, OUTPUT);
  seq_init(&s_seq);
  temp_avg_init(&s_tavg);
  s_rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxItem));
  if (s_rx_queue == NULL) return false;
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(on_recv);
  return true;
}

// Chờ gói tối đa 1 s (để vẫn in được thống kê khi không có gói), phân loại.
void task_espnow(void *pvParameters) {
  (void)pvParameters;
  RxItem it;
  uint32_t last_stats = millis();
  while (1) {
    if (xQueueReceive(s_rx_queue, &it, pdMS_TO_TICKS(1000)) == pdTRUE) {
      PktHeader h;
      memcpy(&h, it.data, sizeof(h));
      if (h.type == PKT_PAIR_REQ) handle_pair_req(it);
      else if (h.type == PKT_DATA) handle_data(it);
      else s_bad_pkt++;
    }
    if (millis() - last_stats >= STATS_PERIOD_MS) {
      last_stats = millis();
      print_stats();
    }
  }
}
