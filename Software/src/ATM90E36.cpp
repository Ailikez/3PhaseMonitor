#include "Arduino.h"
#include <SPI.h>
#include "ATM90E36.h"
#include "fram.h"

#define ATM90_CS_PIN 16

uint16_t readATM90E36(uint16_t address)
{
	SPISettings settings(500000, MSBFIRST, SPI_MODE2);
	SPI.beginTransaction(settings);

	digitalWrite(ATM90_CS_PIN, LOW);
	delayMicroseconds(1);

	SPI.transfer16(address | (1 << 15));	// R/W flags
	delayMicroseconds(4);
	uint16_t value = SPI.transfer16(0xFFFF);

	digitalWrite(ATM90_CS_PIN, HIGH);

	return value;
}

void writeATM90E36(uint16_t address, uint16_t value)
{
	SPISettings settings(500000, MSBFIRST, SPI_MODE2);
	SPI.beginTransaction(settings);

	digitalWrite(ATM90_CS_PIN, LOW);
	delayMicroseconds(1);

	SPI.transfer16(address);
	delayMicroseconds(4);
	SPI.transfer16(value);

	digitalWrite(ATM90_CS_PIN, HIGH);
}

void initATM90E36()
{
	pinMode(ATM90_CS_PIN, OUTPUT);

	writeATM90E36(SoftReset, 0x789A);   // Perform soft reset

	delay(10);

	//Set metering config values (CONFIG)
	writeATM90E36(ConfigStart, 0x5678); // Metering calibration startup
	writeATM90E36(PLconstH, 0x1AD2);    // PL Constant 1kWh = 1000CF
	writeATM90E36(PLconstL, 0x7480);    // PL Constant

	writeATM90E36(MMode0, 0x0087);      // Mode Config (50 Hz, 3P4W, 0.1CF)
	writeATM90E36(MMode1, 0x2A2A);      // All PGA x4

	//Set measurement calibration values (ADJUST)
	writeATM90E36(AdjStart, 0x5678);    // Measurement calibration

	int64_t calibration[6];
	readFram((uint8_t*)calibration, FRAM_CAL, sizeof(calibration));

	writeATM90E36(UgainA, (unsigned short)calibration[0]);      // A Voltage rms gain
	writeATM90E36(UgainB, (unsigned short)calibration[1]);      // B Voltage rms gain
	writeATM90E36(UgainC, (unsigned short)calibration[2]);      // C Voltage rms gain
	writeATM90E36(IgainA, (unsigned short)calibration[3]);      // A line current gain
	writeATM90E36(IgainB, (unsigned short)calibration[4]);      // B line current gain
	writeATM90E36(IgainC, (unsigned short)calibration[5]);      // C line current gain
}
