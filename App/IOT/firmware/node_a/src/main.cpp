// Node A (#26): đọc cảm biến 2 s/lần, gửi PKT_DATA tới Node B bằng ESP-NOW unicast.
// task_sensor --(sample_queue, 1 phần tử, luôn giữ mẫu MỚI NHẤT)--> task_espnow_tx
// TinyML làm sau (#30 xong model): W1 gửi anomaly = 0, anomaly_score = -1 (CONTRACT mục 3).
#include <Arduino.h>
#include "sensors.h"
#include "espnow_link.h"
#include "node_a_logic.h"

#define LED_PIN GPIO_NUM_48
#define SENSOR_PERIOD_MS 2000
#define PAIR_TIMEOUT_MS 1000
#define PAIR_TRIES_BEFORE_SCAN 5  // ghép cặp hụt 5 lần liền -> quét lại channel

QueueHandle_t sample_queue;

void task_sensor(void *pvParameters) {
  SensorSample s;
  while (1) {
    sensors_read(&s);
    // TODO 10: in 1 dòng Serial: T, H, light, presence, valid, dht_fail
    //          (để kiểm ở lab: hà hơi vào DHT20 -> H tăng, che cảm biến ánh sáng -> light đổi).
    xQueueOverwrite(sample_queue, &s);  // queue 1 phần tử: ghi đè mẫu cũ chưa gửi
    vTaskDelay(SENSOR_PERIOD_MS);
  }
}

void task_espnow_tx(void *pvParameters) {
  uint32_t seq = 0;
  bool paired = false;
  int pair_fail = 0;
  TxState tx;
  tx_init(&tx);
  uint32_t sent_ok = 0, resend = 0, give_up = 0, repair = 0;  // in ra để làm minh chứng PR

  while (1) {
    // TODO 11: chưa ghép cặp -> espnow_link_pair(PAIR_TIMEOUT_MS).
    //          Hụt PAIR_TRIES_BEFORE_SCAN lần liền -> espnow_link_scan_channel(), pair_fail = 0.
    //          Ghép được -> paired = true, tx_init(&tx). Xong thì continue (chưa lấy mẫu).

    // TODO 12: xQueueReceive(sample_queue, &s, portMAX_DELAY).
    //          s.valid == false -> KHÔNG gửi, KHÔNG tăng seq (Node B sẽ không đếm nhầm là mất gói).

    // TODO 13: điền PktData: hdr (magic, version, PKT_DATA), seq, temperature, humidity,
    //          light, presence, anomaly = 0, anomaly_score = -1, dht_fail.

    // TODO 14: vòng gửi:
    //   ok = espnow_link_send(&pkt); action = tx_on_result(&tx, ok, millis());
    //   TX_DONE    -> seq++, sent_ok++, đảo LED, thoát vòng
    //   TX_RESEND  -> resend++, gửi lại CÙNG pkt (không đổi seq)
    //   TX_GIVE_UP -> seq++, give_up++, thoát vòng
    //   TX_REPAIR  -> seq++, repair++, espnow_link_unpair(), paired = false, thoát vòng

    // TODO 15: mỗi ~10 s in 1 dòng: ch, paired, seq, sent_ok, resend, give_up, repair,
    //          ESP.getMinFreeHeap(). Dòng này chụp làm minh chứng PR.

    vTaskDelay(1000);  // XÓA dòng này khi làm xong TODO 12 (xQueueReceive đã chờ sẵn)
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // chờ USB CDC
  pinMode(LED_PIN, OUTPUT);
  Serial.println("===== NODE A =====");

  sensors_init();
  if (!espnow_link_init()) {
    Serial.println("[A] FAIL espnow_link_init");
    return;
  }
  espnow_link_scan_channel();

  sample_queue = xQueueCreate(1, sizeof(SensorSample));
  xTaskCreate(task_sensor, "task_sensor", 4096, NULL, 2, NULL);
  xTaskCreate(task_espnow_tx, "task_espnow_tx", 4096, NULL, 2, NULL);
}

void loop() {}
