#pragma once

// WT-SC01 Plus has no dedicated ambient sensor — fall back to the ESP32-S3
// internal die temperature sensor driver.
#include "drivers/InternalDieSensor.h"
using AmbientSensor = InternalDieSensor;
