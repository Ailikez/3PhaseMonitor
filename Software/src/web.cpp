#include "Arduino.h"

#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

#include "metrics.h"
#include "ATM90E36.h"
#include "fram.h"

const char* host = "threephasemeter";
const char* update_path = "/update";
const char* update_username = "admin";
const char* update_password = "admin";

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

String message_buffer = "";

void handleStatus()
{
	message_buffer.remove(0);

	message_buffer.concat("SysStatus0 (should be 0x0000): 0x");
	unsigned short sysstatus0 = readATM90E36(SysStatus0);
	message_buffer.concat(String(sysstatus0, HEX));

	message_buffer.concat("\nLast time to read values over SPI (microseconds): ");
	message_buffer.concat(String(lastMetricReadTime));

	message_buffer.concat("\nFree Heap: (kB): ");
	message_buffer.concat(String(((float)ESP.getFreeHeap())/1024, 3));

	message_buffer.concat("\nSupply Voltage: (volt): ");
	message_buffer.concat(String(((float)ESP.getVcc())/1000, 2));

	message_buffer.concat("\ncalibration:");
	message_buffer.concat("\nUGainA: ");
	message_buffer.concat(String(readATM90E36(UgainA)));
	message_buffer.concat("\nUGainB: ");
	message_buffer.concat(String(readATM90E36(UgainB)));
	message_buffer.concat("\nUGainC: ");
	message_buffer.concat(String(readATM90E36(UgainC)));
	message_buffer.concat("\nIGainA: ");
	message_buffer.concat(String(readATM90E36(IgainA)));
	message_buffer.concat("\nIGainB: ");
	message_buffer.concat(String(readATM90E36(IgainB)));
	message_buffer.concat("\nIGainC: ");
	message_buffer.concat(String(readATM90E36(IgainC)));


	httpServer.send(200, "text/plain", message_buffer);
}

void handleReboot()
{
	ESP.restart();
}

// ain't pretty but it works
// http://192.168.2.82/set?addr=igainc&val=20037
void handleSet()
{
	String address = httpServer.arg("addr");
	address.toLowerCase();

	uint8_t finaladdr = 0;
	int64_t final_val = 0;

	if(address.length() == 6)
	{
		if(address.substring(1, 5) == "gain")
		{
			uint16_t value = httpServer.arg("val").toInt();
			uint8_t addr = FRAM_CAL;

			if(address[0] == 'i')
				addr += 3;
			else if (address[0] != 'u')
				goto fail;

			uint8_t phase = address[5] - 'a';

			if(phase > 2)
				goto fail;

			addr += phase;

			finaladdr = addr;
			final_val = value;
			writeFram((uint8_t*)&value, addr, sizeof(value));
			goto ok;
		}

		if(address.substring(0, 5) == "total")
		{
			int64_t value = httpServer.arg("val").toInt();
			uint8_t addr = 0;

			if(address[5] != 't')
			{
				uint8_t phase = address[5] - 'a';
				if(phase > 2)
					goto fail;
				addr += phase + 1;
			}

			total_energy[addr] = value;
			finaladdr = addr;
			final_val = value;
			goto ok;
		}
	}
	else
	{
		fail:
		httpServer.send(400, "text/plain", "invalid address");
		return;
	}

	ok:
	String message = "ok [";
	message += String((int)finaladdr);
	message += "] = ";
	message += String((long)final_val);
	httpServer.send(200, "text/plain", message);
}

void initWeb()
{
	message_buffer.reserve(1024);

	httpUpdater.setup(&httpServer, update_path, update_username, update_password);

	httpServer.on("/metrics", HTTP_GET, handleMetrics);
	httpServer.on("/allmetrics", HTTP_GET, handleAllMetrics);
	httpServer.on("/reboot", HTTP_GET, handleReboot);
	httpServer.on("/status", HTTP_GET, handleStatus);
	httpServer.on("/set", HTTP_GET, handleSet);
	httpServer.begin();

	MDNS.begin(host);
	MDNS.addService("http", "tcp", 80);
}
