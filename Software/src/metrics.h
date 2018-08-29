#ifndef METRICS_h
#define METRICS_h

void handleMetrics();
void handleAllMetrics();
void readMetrics();
void initMetrics();

extern int64_t total_energy[];

#define SAMPLE_COUNT 20
#define SAMPLE_INTERVAL_MS 500

extern unsigned long lastMetricReadTime;

#endif
