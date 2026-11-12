#include "espnow_link.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include "secrets.h"  // WIFI_SSID (firmware/common/secrets.h, chép từ secrets.example.h)

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t node_b_mac[6];
static int cur_channel = -1;

// Callback chạy trong task WiFi: chỉ kiểm nhanh rồi đẩy vào queue, không Serial.print, không chờ.
typedef struct {
  uint8_t mac[6];
  uint8_t channel;
} PairInfo;

static QueueHandle_t send_result_queue;  // 1 phần tử bool: kết quả ACK của lần gửi unicast gần nhất
static QueueHandle_t pair_queue;         // PairInfo: Node B trả lời ghép cặp

// ---------- Callback (core 2.0.17) ----------
static void on_sent(const uint8_t *mac, esp_now_send_status_t status) {
  if (memcmp(mac, BCAST, 6) == 0) return;  // broadcast không có ACK, bỏ qua
  bool ok = (status == ESP_NOW_SEND_SUCCESS);
  xQueueOverwrite(send_result_queue, &ok);
}

static void on_recv(const uint8_t *mac, const uint8_t *data, int len) {
  PktPairResp ppr;
  if (len != sizeof(ppr)) return;
  memcpy(&ppr, data, sizeof(ppr));
  if (ppr.hdr.magic != ESPNOW_MAGIC) return;
  if (ppr.hdr.version != ESPNOW_VERSION) return;
  if (ppr.hdr.type != PKT_PAIR_RESP) return;
  PairInfo info;
  memcpy(info.mac, mac, sizeof(info.mac));
  info.channel = ppr.channel;
  xQueueSend(pair_queue, &info, 0);
}

// ---------- Hàm phụ (đã chạy ở test unicast 11/11) ----------
static void set_channel(int ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

static bool add_peer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0;  // 0 = theo channel hiện tại của radio
  p.ifidx = WIFI_IF_STA;
  p.encrypt = false;
  return esp_now_add_peer(&p) == ESP_OK;
}

static int find_hotspot_channel() {
  int n = WiFi.scanNetworks();
  int ch = -1;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == WIFI_SSID) {
      ch = WiFi.channel(i);
      break;
    }
  }
  WiFi.scanDelete();
  return ch;
}

// ---------- API ----------
bool espnow_link_init() {
  send_result_queue = xQueueCreate(1, sizeof(bool));
  pair_queue = xQueueCreate(4, sizeof(PairInfo));
  if (send_result_queue == NULL || pair_queue == NULL) return false;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.printf("[A] MAC Node A: %s\n", WiFi.macAddress().c_str());

  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_send_cb(on_sent);
  esp_now_register_recv_cb(on_recv);
  return add_peer(BCAST);
}

void espnow_link_scan_channel() {
  int ch = -1;
  while (ch < 0) {
    Serial.printf("[A] Quet tim '%s'...\n", WIFI_SSID);
    ch = find_hotspot_channel();
    if (ch < 0) vTaskDelay(2000);
  }
  cur_channel = ch;
  set_channel(ch);  // quét xong radio có thể đứng ở channel khác -> ép lại
  Serial.printf("[A] Hotspot o channel %d\n", ch);
}

bool espnow_link_pair(uint32_t timeout_ms) {
  xQueueReset(pair_queue);
  PktPairReq ppr;
  ppr.hdr.magic = ESPNOW_MAGIC;
  ppr.hdr.version = ESPNOW_VERSION;
  ppr.hdr.type = PKT_PAIR_REQ;
  esp_now_send(BCAST, (const uint8_t *)&ppr, sizeof(PktPairReq));
  PairInfo info;
  if(!xQueueReceive(pair_queue, &info, timeout_ms)) return false;
  if(info.channel != cur_channel) {
    set_channel(info.channel);
    cur_channel = info.channel;
  }
  memcpy(node_b_mac, info.mac, sizeof(node_b_mac));
  if(!add_peer(node_b_mac)) return false;
  Serial.printf("[A] GHEP CAP OK voi Node B %02X:%02X:%02X:%02X:%02X:%02X (ch %d)\n", 
  node_b_mac[0], node_b_mac[1], node_b_mac[2], node_b_mac[3], node_b_mac[4], node_b_mac[5], info.channel);
  return true;
}

bool espnow_link_send(const PktData *pkt) {
  xQueueReset(send_result_queue);
  esp_err_t status = esp_now_send(node_b_mac, (const uint8_t *)pkt, sizeof(PktData));
  if(status != ESP_OK) {
    Serial.printf("[A] esp_now_send loi: %s\n", esp_err_to_name(status));
    return false;
  }
  bool ok = false; // khởi tạo bằng false cho an toàn dù ở trên đã return rồi
  if(!xQueueReceive(send_result_queue, &ok, 100)) return false;
  return ok;
}

void espnow_link_unpair() {
  if (esp_now_is_peer_exist(node_b_mac)) esp_now_del_peer(node_b_mac);
}

int espnow_link_channel() { return cur_channel; }
