#pragma once

// Elecrow CrowPanel 2.1" round has no dedicated ambient sensor — fall back to
// the ESP32-S3 internal die temperature sensor driver.
#include "drivers/InternalDieSensor.h"
using AmbientSensor = InternalDieSensor;
