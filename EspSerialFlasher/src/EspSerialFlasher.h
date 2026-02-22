#ifndef ESP_SERIAL_FLASHER_H
#define ESP_SERIAL_FLASHER_H

#include <Arduino.h>
#include "esp_loader.h"

class EspSerialFlasher {
public:
	EspSerialFlasher(HardwareSerial& serialPort, int resetPin, int bootPin);
	void begin(unsigned long baud = 115200);
	esp_loader_error_t connect();
private:
	HardwareSerial& _serial;
	int _resetPin, _bootPin;
	unsigned long _baud;
};

#endif
