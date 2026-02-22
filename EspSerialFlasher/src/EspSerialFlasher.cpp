// EspSerialFlasher.cpp
#include "EspSerialFlasher.h"
extern Stream* loaderSerial;
extern int loaderResetPin;
extern int loaderBootPin;

EspSerialFlasher::EspSerialFlasher(HardwareSerial& serialPort, int resetPin, int bootPin)
	: _serial(serialPort), _resetPin(resetPin), _bootPin(bootPin), _baud(115200) {
}

void EspSerialFlasher::begin(unsigned long baud) {
	_baud = baud;
	loaderSerial = &_serial;
	loaderResetPin = _resetPin;
	loaderBootPin = _bootPin;
	pinMode(_resetPin, OUTPUT);
	pinMode(_bootPin, OUTPUT);
	_serial.begin(_baud);
}

esp_loader_error_t EspSerialFlasher::connect() {
	// ESP32-C5 ROM can be slow to respond; use long timeout and many trials
	esp_loader_connect_args_t args = {
		.sync_timeout = 500,
		.trials = 25
	};
	return esp_loader_connect(&args);
}
