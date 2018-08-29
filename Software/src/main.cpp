#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <SPI.h>

#include "metrics.h"
#include "ATM90E36.h"
#include "fram.h"
#include "web.h"

ADC_MODE(ADC_VCC);

const char* wifi_ssid = WIFI_SSID;
const char* wifi_psk = WIFI_PSK;



void setup(void)
{
	Serial.begin(115200);

	Serial.println("\n\nBooting Sketch...");

	SPI.begin();
	initATM90E36();
	initFRAM();
	initMetrics();

	Serial.println("Initializing WiFi");

	WiFi.mode(WIFI_STA);
	WiFi.begin(wifi_ssid, wifi_psk);

	IPAddress ip(192,168,2,82);
	IPAddress gateway(192,168,2,1);
	IPAddress subnet(255,255,255,0);

	WiFi.config(ip, gateway, subnet);
	WiFi.hostname(host);

	while(WiFi.waitForConnectResult() != WL_CONNECTED){
		WiFi.begin(wifi_ssid, wifi_psk);
		Serial.println("WiFi failed, retrying.");
	}

	initWeb();

	Serial.println("setup finished");
}

void loop(void)
{
	static unsigned long last_value_get = millis();

	httpServer.handleClient();

	if(abs(millis() - last_value_get) > SAMPLE_INTERVAL_MS)
	{
		last_value_get += SAMPLE_INTERVAL_MS;

		readMetrics();
	}
}
