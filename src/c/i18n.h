#pragma once

#include <pebble.h>

// Text identifiers used by the application. The first three must remain
// in activity order (walk, run, training).
typedef enum {
  STR_ACTIVITY_WALK = 0,
  STR_ACTIVITY_RUN,
  STR_ACTIVITY_TRAINING,
  STR_TOO_SHORT,
  STR_MIN_REQUIRED,
  STR_SESSION_DONE,
  STR_STOP_QUESTION,
  STR_SENSOR_UNAVAILABLE,
  STR_UNIT_STEPS,
  STR_LABEL_DURATION,
  STR_LABEL_STEPS,
  STR_LABEL_DISTANCE,
  STR_LABEL_HEART_RATE,
  STR_LABEL_PACE,
  STR_LABEL_SPEED,
  STR_SUMMARY_PACE,
  STR_SUMMARY_SPEED,
  STR_SUMMARY_HR_AVG,
  STR_SUMMARY_HR_MAX,
  STR_SUMMARY_CALORIES,
  STR_DISTANCE_UNITS,
  STR_COUNT
} StringId;

// Returns text in the watch system language (English by default).
const char *tr(StringId id);
