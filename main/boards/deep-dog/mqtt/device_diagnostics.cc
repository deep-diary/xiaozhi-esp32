#include "mqtt/device_diagnostics.h"

#include "mqtt/memory_report.h"

void DeepDogDeviceDiagnosticsAppendTasks(cJSON* root) {
    DeepDogMemoryReportAppendTasks(root);
}
