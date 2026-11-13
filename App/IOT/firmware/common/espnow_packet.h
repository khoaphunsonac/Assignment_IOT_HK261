// Gói ESP-NOW Node A <-> Node B. Chép NGUYÊN VĂN docs/CONTRACT.md mục 3.
// Muốn đổi: sửa CONTRACT trước (PR + mục 14), rồi mới sửa file này.
#pragma once
#include <stdint.h>

#define ESPNOW_MAGIC   0x31544C42u  // "BTL1": bỏ gói lạ từ thiết bị khác trong phòng lab
#define ESPNOW_VERSION 1            // tăng khi đổi struct; Node B bỏ gói sai version

enum PktType : uint8_t {
  PKT_PAIR_REQ  = 1,  // A -> B (broadcast): xin ghép cặp
  PKT_PAIR_RESP = 2,  // B -> A (unicast): trả lời, kèm channel
  PKT_DATA      = 3,  // A -> B: số đo
  PKT_CONFIG    = 4,  // B -> A: cấu hình cho Node A (làm từ W2)
};

typedef struct __attribute__((packed)) {
  uint32_t magic;    // = ESPNOW_MAGIC
  uint8_t  version;  // = ESPNOW_VERSION
  uint8_t  type;     // PktType
} PktHeader;         // 6 byte

typedef struct __attribute__((packed)) {
  PktHeader hdr;
} PktPairReq;        // 6 byte

typedef struct __attribute__((packed)) {
  PktHeader hdr;
  uint8_t   channel;  // channel WiFi Node B đang dùng (= channel hotspot)
} PktPairResp;       // 7 byte

typedef struct __attribute__((packed)) {
  PktHeader hdr;
  uint32_t seq;            // tăng 1 mỗi mẫu mới, về 0 khi Node A khởi động lại
  float    temperature;    // °C, đã kiểm hợp lệ (không bao giờ NaN)
  float    humidity;       // %
  uint16_t light;          // ADC thô 0..4095
  uint8_t  presence;       // 0/1, ĐÃ tính thời gian chờ phòng trống
  uint8_t  anomaly;        // 0/1, kết quả TinyML
  float    anomaly_score;  // 0..1; chưa chạy TinyML thì gửi -1
  uint16_t dht_fail;       // tổng số lần đọc DHT20 lỗi từ lúc khởi động
} PktData;           // 28 byte (giới hạn ESP-NOW 250 byte)

typedef struct __attribute__((packed)) {
  PktHeader hdr;
  uint16_t  empty_timeout_s;  // thời gian chờ "phòng trống" cho Node A
} PktConfig;         // 8 byte

// Kiểm kích thước lúc biên dịch: lệch là 2 node hiểu sai gói của nhau.
static_assert(sizeof(PktHeader) == 6, "PktHeader phai 6 byte");
static_assert(sizeof(PktPairReq) == 6, "PktPairReq phai 6 byte");
static_assert(sizeof(PktPairResp) == 7, "PktPairResp phai 7 byte");
static_assert(sizeof(PktData) == 28, "PktData phai 28 byte");
static_assert(sizeof(PktConfig) == 8, "PktConfig phai 8 byte");
