// Node B - khung rỗng (#24). Code thật thêm ở các issue sau.
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Node B: khung OK");
}

void loop() {
  vTaskDelay(1000);
}
