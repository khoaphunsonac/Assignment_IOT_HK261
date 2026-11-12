#include "sensors.h"
#include "DHT20.h"
#include "node_a_logic.h"

DHT20 dht20;
static PresenceState presence_state;
static uint16_t dht_fail_count = 0;

void sensors_init() {
  Wire.begin(PIN_SDA, PIN_SCL);  // như LAB3: Wire.begin(11, 12) rồi dht20.begin()
  if (!dht20.begin()) Serial.println("[A] DHT20 khong tra loi (kiem day I2C)");
  pinMode(PIN_PIR, INPUT);
  presence_init(&presence_state, millis());
} 

void sensors_read(SensorSample *out) {
  if(!dht20.isConnected() || dht20.read() != DHT20_OK) {
    dht_fail_count++;
    out->humidity = 0;
    out->temperature = 0;
    out->valid = false;
  } else {
    out->humidity = dht20.getHumidity();
    out->temperature = dht20.getTemperature();
    if(!reading_valid(out->temperature, out->humidity)) {
      dht_fail_count++;
      out->valid = false;
    }
    else out->valid = true;
  }

  out->light = analogRead(PIN_LIGHT);
  out->presence = presence_update(&presence_state, digitalRead(PIN_PIR) == HIGH, millis(), EMPTY_TIMEOUT_S * 1000);
  out->dht_fail = dht_fail_count;
}
