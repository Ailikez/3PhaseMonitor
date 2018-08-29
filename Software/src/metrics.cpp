#include "Arduino.h"
#include "ATM90E36.h"
#include "metrics.h"
#include "fram.h"
#include "web.h"

enum ValueType {LSB_UNSIGNED = 1, LSB_COMPLEMENT = 2, NOLSB_UNSIGNED = 3, NOLSB_SIGNED = 4};

struct Metric
{
	// content of the name tag
	const char *name;
	// content for the phase tag, every char makes a new value.
	// ABC=individual phases, N=neutral (for calculated current and metrics that don't belong to a particular phase),
	// T=total
	const char *phases;
	// address of SPI register
	unsigned short address;
	// factor to convert the raw integer value to the proper floating point value
	// when LSB is used, a factor of 1/256 is added automatically
	double factor;
	// wether to use the additional LSB register
	enum ValueType type;
	// number of decimal places to show
	uint8_t decimals;
	// showInMain = false -> the metric is only shown on /allmetrics
	bool showInMain;
	int32_t **values;
};

struct Metric metrics[] = {
	{"voltage_rms", "ABC", UrmsA, 1./100, LSB_UNSIGNED, 2, true},

	{"current_rms", "N", IrmsN0, 1./1000, NOLSB_UNSIGNED, 3, true},
	{"current_rms", "ABC", IrmsA, 1./1000, LSB_UNSIGNED, 5, true},

	{"power_active", "T", PmeanT, 4./1000, LSB_COMPLEMENT, 5, true},
	{"power_active", "ABC", PmeanA, 1./1000, LSB_COMPLEMENT, 5, true},

	{"power_reactive", "T", QmeanT, 4./1000, LSB_COMPLEMENT, 5, false},
	{"power_reactive", "ABC", QmeanA, 1./1000, LSB_COMPLEMENT, 5, false},

	{"power_apparent", "T", SmeanT, 4./1000, LSB_COMPLEMENT, 5, false},
	{"power_apparent", "ABC", SmeanA, 1./1000, LSB_COMPLEMENT, 5, false},

	{"power_factor", "TABC", PFmeanT, 1./1000, NOLSB_SIGNED, 3, true},

	{"phase_angle_voltage", "ABC", UangleA, 1./10, NOLSB_SIGNED, 2, false},
	{"phase_angle_current", "ABC", PAngleA, 1./10, NOLSB_SIGNED, 2, false},

	{"thdn_voltage", "ABC", THDNUA, 1./100, NOLSB_UNSIGNED, 1, false},
	{"thdn_current", "ABC", THDNIA, 1./100, NOLSB_UNSIGNED, 1, false},

	{"frequency", "N", Freq, 1./100, NOLSB_UNSIGNED, 3, true},
	{"temperature", "N", Temp, 1., NOLSB_SIGNED, 0, false}
};
#define METRIC_COUNT (sizeof(metrics)/sizeof(metrics[0]))

// total energy count
int64_t total_energy[4];
// last time taken to read all metrics from the ATM90E36A (in microseconds)
unsigned long lastMetricReadTime = 0;
// fill value buffers completely before serving metrics to webpage
uint8_t webpage_wait_counter = SAMPLE_COUNT;
// index of next value to be replaced
uint8_t index_nextvalue = 0;

void initMetrics()
{
	for(uint8_t index_metric = 0; index_metric < METRIC_COUNT; index_metric++)
	{
		int8_t phasecount = strlen(metrics[index_metric].phases);

		metrics[index_metric].values = (int32_t**)malloc(phasecount * sizeof(int32_t*));

		for(uint8_t index_phase = 0; index_phase < phasecount; index_phase++)
		metrics[index_metric].values[index_phase] = (int32_t*)malloc(SAMPLE_COUNT * sizeof(int32_t));
	}

	readFram((uint8_t*)total_energy, FRAM_TOTAL, sizeof(total_energy));
}

const uint8_t total_energy_write_interval = 50;
uint8_t total_energy_countdown = total_energy_write_interval;

void readMetrics()
{
	unsigned long starttime = micros();

	if(webpage_wait_counter)
		webpage_wait_counter--;

	for(uint8_t i = 0; i < 4; i++)
		total_energy[i] += readATM90E36(APenergyT + i);
	for(uint8_t i = 0; i < 4; i++)
		total_energy[i] -= readATM90E36(ANenergyT + i);

	if(total_energy_countdown)
		total_energy_countdown--;
	else
	{
		total_energy_countdown = total_energy_write_interval;
		writeFram((uint8_t*)total_energy, FRAM_TOTAL, sizeof(total_energy));
	}

	for(uint8_t index_metric = 0; index_metric < METRIC_COUNT; index_metric++)
	{
		struct Metric metric = metrics[index_metric];

		uint8_t phasecount = strlen(metric.phases);

		for(uint8_t index_phase = 0; index_phase < phasecount; index_phase++)
		{
			int32_t value;

			if(metric.type == LSB_COMPLEMENT)
			{
				uint32_t val = readATM90E36(metric.address + index_phase);

				if(val & 0x8000)
					val |= 0xFF0000;

				uint16_t lsb = (unsigned short)readATM90E36(metric.address + index_phase + 0x10);

				val = (val << 8) + (lsb >> 8);

				value = (int32_t)val;
			}
			else if(metric.type == LSB_UNSIGNED)
			{
				value = (unsigned short)readATM90E36(metric.address + index_phase);
				uint16_t lsb = (unsigned short)readATM90E36(metric.address + index_phase + 0x10);
				value = (value << 8) + (lsb >> 8);
			}
			else if(metric.type == NOLSB_SIGNED)
			{
				value = (signed short)readATM90E36(metric.address + index_phase);
			}
			else //if (metric.type == NOLSB_UNSIGNED)
			{
				value = (unsigned short)readATM90E36(metric.address + index_phase);
			}

			metrics[index_metric].values[index_phase][index_nextvalue] = value;
		}
	}

	if(++index_nextvalue == SAMPLE_COUNT)
		index_nextvalue = 0;

	lastMetricReadTime = micros() - starttime;
}

void handleMetricsInternal(bool all)
{
	if(webpage_wait_counter)
	{
		httpServer.send(404, "text/plain", "please wait for buffers to fill");
		return;
	}

	message_buffer.remove(0);

	for(uint8_t index_metric = 0; index_metric < METRIC_COUNT; index_metric++)
	{
		struct Metric metric = metrics[index_metric];

		if((!all) && (!metric.showInMain))
			continue;

		uint8_t phasecount = strlen(metric.phases);

		for(uint8_t index_phase = 0; index_phase < phasecount; index_phase++)
		{
			int64_t valuesum = 0;

			int *valueptr = metric.values[index_phase];

			for(uint8_t i = 0; i < SAMPLE_COUNT; i++)
				valuesum += *(valueptr++);

			double value = valuesum * metric.factor / SAMPLE_COUNT;

			if(metric.type == LSB_COMPLEMENT || metric.type == LSB_UNSIGNED)
				value /= (1 << 8);

			message_buffer.concat("threephase,loc=main,phase=");
			message_buffer.concat(metric.phases[index_phase]);
			message_buffer.concat(",name=");
			message_buffer.concat(metric.name);
			message_buffer.concat(" value=");
			message_buffer.concat(String(value, metric.decimals));
			message_buffer.concat("\n");
		}
	}

	const char *phases = "TABC";
	for(uint8_t i = 0; i < 4; i++)
	{
		message_buffer.concat("threephase,loc=main,phase=");
		message_buffer.concat(phases[i]);
		message_buffer.concat(",name=total_energy value=");
		double value = total_energy[i];
		value /= (1000 * 10);
		message_buffer.concat(String(value, 4));
		message_buffer.concat("\n");
	}

	httpServer.send(200, "text/plain; version=0.0.4", message_buffer);
}

void handleMetrics()
{
	handleMetricsInternal(false);
}

void handleAllMetrics()
{
	handleMetricsInternal(true);
}
