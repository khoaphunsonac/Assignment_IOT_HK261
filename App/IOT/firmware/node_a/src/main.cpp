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
    Serial.printf("[A] T: %.1f H: %.1f light: %u presence: %u valid: %d dht_fail: %u\n",
    s.temperature, s.humidity, s.light, s.presence, s.valid, s.dht_fail);
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
  unsigned long last = millis();

  while (1) {
    if(!paired) {
      if(!espnow_link_pair(PAIR_TIMEOUT_MS)){
        pair_fail++;
        if(pair_fail == PAIR_TRIES_BEFORE_SCAN){
          espnow_link_scan_channel();
          pair_fail = 0;
        }
      } else {
        paired = true;
        tx_init(&tx);
        pair_fail = 0;
      }
      continue;
    }

    SensorSample s;
    xQueueReceive(sample_queue, &s, portMAX_DELAY);
    if(!s.valid) continue;

    PktData pd;
    pd.hdr.magic = ESPNOW_MAGIC;
    pd.hdr.version = ESPNOW_VERSION;
    pd.hdr.type = PKT_DATA;
    pd.seq = seq;
    pd.temperature = s.temperature;
    pd.humidity = s.humidity;
    pd.light = s.light;
    pd.presence = s.presence;
    pd.anomaly= 0;
    pd.anomaly_score = -1;
    pd.dht_fail = s.dht_fail;

    while(true){
      bool ok = espnow_link_send(&pd);
      TxAction status = tx_on_result(&tx, ok, millis());
      if(status == TX_DONE) {
        seq++;
        sent_ok++;
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        break;
      } else if (status == TX_RESEND) {
        resend++;
      } else if (status == TX_GIVE_UP) {
        seq++;
        give_up++;
        break;
      } else if (status == TX_REPAIR) {
        seq++;
        repair++;
        espnow_link_unpair();
        paired = false;
        break;
      }
    }

    if (millis() - last >= 10000) {
      Serial.printf("[A] ch: %d paired: %d seq: %u sent_ok: %u resend: %u give_up: %u repair: %u free: %u\n",
      espnow_link_channel(), paired, seq, sent_ok, resend, give_up, repair, ESP.getMinFreeHeap());
      last = millis();
    }
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
