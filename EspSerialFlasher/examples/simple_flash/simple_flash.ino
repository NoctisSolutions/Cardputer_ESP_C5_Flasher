#include <EspSerialFlasher.h>

EspSerialFlasher flasher(Serial1, 14, 15);  // Serial1, reset on pin 14, boot on 15

void setup() {
  Serial.begin(115200);  // For debug
  flasher.begin(115200);
  if (flasher.connect() == ESP_LOADER_SUCCESS) {
    Serial.println("Connected to target!");
    // Add flashing code: esp_loader_flash_binary(...)
  } else {
    Serial.println("Connection failed");
  }
}

void loop() {}