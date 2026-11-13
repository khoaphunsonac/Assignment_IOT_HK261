// Hiện thực espnow_link.h. Phần init / quét channel / add peer lấy từ firmware test
// unicast A_unicast đã chạy trên board ở lab 11/11; phần ghép cặp và gửi chờ ACK viết mới cho #26.
//
// Mô hình chung: 2 callback ESP-NOW chạy trong task WiFi của hệ thống, nên chỉ kiểm
// nhanh rồi đẩy kết quả vào queue (không Serial.print, không chờ). Các hàm API chạy
// trong task_espnow_tx và đọc kết quả từ queue đó.
#include "espnow_link.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include "secrets.h"  // WIFI_SSID (firmware/common/secrets.h, chép từ secrets.example.h)

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t node_b_mac[6];   // MAC Node B, học được lúc ghép cặp (không ghi cứng)
static int cur_channel = -1;    // channel radio đang đứng, -1 = chưa quét

// Callback chạy trong task WiFi: chỉ kiểm nhanh rồi đẩy vào queue, không Serial.print, không chờ.
typedef struct {
  uint8_t mac[6];
  uint8_t channel;
} PairInfo;

static QueueHandle_t send_result_queue;  // 1 phần tử bool: kết quả ACK của lần gửi unicast gần nhất
static QueueHandle_t pair_queue;         // PairInfo: Node B trả lời ghép cặp

// ---------- Callback (core 2.0.17) ----------
// on_sent: ESP-NOW gọi sau mỗi lần gửi, status = có ACK hay không.
//   Broadcast không có ACK (luôn báo "thành công") nên bỏ qua, chỉ đẩy kết quả unicast.
//   xQueueOverwrite: queue 1 phần tử, luôn giữ kết quả mới nhất.
static void on_sent(const uint8_t *mac, esp_now_send_status_t status) {
  if (memcmp(mac, BCAST, 6) == 0) return;  // broadcast không có ACK, bỏ qua
  bool ok = (status == ESP_NOW_SEND_SUCCESS);
  xQueueOverwrite(send_result_queue, &ok);
}

// on_recv: ESP-NOW gọi khi nhận gói bất kỳ (phòng lab có thể có board nhóm khác).
//   Chỉ nhận PKT_PAIR_RESP hợp lệ: đúng độ dài, magic, version, type. Sai là bỏ.
//   Kiểm len TRƯỚC memcpy: gói ngắn hơn mà vẫn chép 7 byte là đọc tràn ra ngoài buffer.
//   memcpy vào biến rồi mới đọc vì data có thể không căn hàng 4 byte (đọc uint32_t magic
//   ở địa chỉ lệch là undefined behavior).
//   Hợp lệ: gửi MAC người gửi (= Node B) + channel B báo về cho espnow_link_pair().
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
// set_channel: ép radio sang channel ch. Khi chưa nối WiFi, ESP-IDF chỉ cho đổi channel
// trong chế độ promiscuous, nên bật promiscuous -> đổi -> tắt.
static void set_channel(int ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

// add_peer: thêm 1 peer ESP-NOW (không mã hóa). Đã có thì thôi.
//   channel = 0: dùng channel hiện tại của radio, nên đổi channel không phải add lại.
static bool add_peer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0;  // 0 = theo channel hiện tại của radio
  p.ifidx = WIFI_IF_STA;
  p.encrypt = false;
  return esp_now_add_peer(&p) == ESP_OK;
}

// find_hotspot_channel: quét WiFi 1 lần (chặn khoảng 2-3 s), trả channel của WIFI_SSID,
// -1 nếu không thấy.
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
// Thứ tự: tạo queue (callback cần) -> WiFi STA (ESP-NOW chạy trên radio WiFi, không
// cần nối mạng) -> esp_now_init -> đăng ký callback -> thêm peer broadcast (để gửi
// PAIR_REQ). MAC Node A in ra để đối chiếu với log Node B.
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

// Quét tới khi thấy hotspot, nghỉ 2 s giữa các lần. Quét xong radio có thể đứng ở
// channel khác (channel cuối cùng được quét) nên phải set_channel lại.
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

// Ghép cặp 1 lần:
//   1. Xóa trả lời cũ còn sót trong pair_queue.
//   2. Broadcast PKT_PAIR_REQ (chưa biết MAC Node B nên phải broadcast).
//   3. Chờ on_recv đẩy PKT_PAIR_RESP vào queue, hết timeout_ms -> false.
//   4. Channel B báo khác channel đang đứng -> đổi theo B (B biết channel thật của hotspot).
//   5. Nhớ MAC B, thêm peer unicast.
//   6. In MAC + channel để đối chiếu với log Node B.
// Hàm này chỉ thử 1 lần; thử lại và quét lại channel là việc của task_espnow_tx (main.cpp).
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

// Gửi 1 gói unicast và chờ ACK:
//   1. xQueueReset TRƯỚC khi gửi: xóa kết quả cũ (callback trễ của lần trước). Nếu reset
//      SAU khi gửi thì có thể xóa mất ACK của chính lần này (callback chạy rất nhanh).
//   2. esp_now_send lỗi ngay (vd peer không tồn tại) -> in tên lỗi, false.
//   3. Chờ on_sent tối đa 100 ms (thực tế ACK về trong vài ms).
// ok khởi tạo false: luồng nào lỡ không gán thì mặc định "lỗi", an toàn hơn "thành công".
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

// Xóa peer Node B nếu đang có (khi TX_REPAIR). Kiểm tồn tại vì gọi del trên peer
// không có sẽ trả lỗi.
void espnow_link_unpair() {
  if (esp_now_is_peer_exist(node_b_mac)) esp_now_del_peer(node_b_mac);
}

int espnow_link_channel() { return cur_channel; }
