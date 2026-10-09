#include <pebble.h>
#include <stdlib.h>
#include "i18n.h"

typedef enum {
  ACTIVITY_WALK = 0,
  ACTIVITY_RUN,
  ACTIVITY_TRAINING,
  ACTIVITY_COUNT
} Activity;

typedef enum {
  METRIC_DURATION = 0,
  METRIC_STEPS,
  METRIC_DISTANCE,
  METRIC_HEART_RATE,
  METRIC_PACE,
  METRIC_SPEED,
  METRIC_COUNT
} WorkoutMetric;

#define MINIMUM_SESSION_SECONDS 120
#define SUMMARY_SCROLL_STEP 30
// Step-counter refresh interval (and therefore distance, pace, and
// speed derived from it). Set to 30 seconds: frequent enough for a
// smooth display without polling the Health service too often.
#define STEPS_REFRESH_INTERVAL_SECONDS 30
#define HEART_RATE_REFRESH_INTERVAL_SECONDS 2
// 1 = display and color use the raw sensor reading
// (responsive, but slightly noisier). 0 = system-filtered reading
// (averaged over the last minute and potentially delayed).
// Session statistics (average, max, calories) always use the filtered reading.
#define HEART_RATE_DISPLAY_USES_RAW 1

// User profile defaults, before any settings are received from the
// phone. The active profile is stored in s_profile below.
#define DEFAULT_USER_IS_MALE 1
#define DEFAULT_USER_AGE_YEARS 35
#define DEFAULT_USER_HEIGHT_CM 175
#define DEFAULT_USER_WEIGHT_KG 75
#define DEFAULT_STRIDE_LENGTH_CM 79
#define DEFAULT_WALK_STRIDE_LENGTH_CM 75
#define DEFAULT_USE_METRIC 0
#define PROFILE_PERSIST_KEY 1

// Accent yellow used for selection and secondary screen backgrounds.
// GColorYellow is brighter/more saturated than GColorPastelYellow.
#define ACCENT_YELLOW GColorYellow
#define SECONDARY_TEXT_COLOR GColorDukeBlue  // bleu fonce, remplace le gris (jaune et blanc)

// Countdown number positioning (Emery 200x228 display)
#define COUNTDOWN_LAYER_HEIGHT 130
#define COUNTDOWN_Y_ADJUST 0
#define WORKOUT_CLOCK_Y_ADJUST (-8)
#define SUMMARY_UNIT_Y_OFFSET 6

// User profile configured from the phone (Clay page) and persisted
// on the watch.
typedef struct {
  int16_t is_male;     // 1 = homme, 0 = femme
  int16_t age_years;
  int16_t height_cm;
  int16_t weight_kg;
  int16_t stride_length_cm;
  int16_t walk_stride_length_cm;
  int16_t use_metric;
} UserProfile;

static UserProfile s_profile = {
  .is_male = DEFAULT_USER_IS_MALE,
  .age_years = DEFAULT_USER_AGE_YEARS,
  .height_cm = DEFAULT_USER_HEIGHT_CM,
  .weight_kg = DEFAULT_USER_WEIGHT_KG,
  .stride_length_cm = DEFAULT_STRIDE_LENGTH_CM,
  .walk_stride_length_cm = DEFAULT_WALK_STRIDE_LENGTH_CM,
  .use_metric = DEFAULT_USE_METRIC
};

static GFont s_roboto_condensed_extrabold_font = NULL;
static GFont s_roboto_condensed_extrabold_countdown_font = NULL;
static GFont s_summary_font = NULL;
static GFont s_emoji_font = NULL;

static Window *s_menu_window;
static Window *s_countdown_window;
static Window *s_workout_window;
static Window *s_stop_confirm_window;
static Window *s_summary_window;

static Layer *s_menu_layer;
static Layer *s_workout_layer;
static Layer *s_stop_confirm_layer;
static Layer *s_summary_layer;

static TextLayer *s_countdown_activity_layer;
static TextLayer *s_countdown_number_layer;

static Activity s_selected_activity = ACTIVITY_RUN;
static WorkoutMetric s_current_metric = METRIC_DISTANCE;

static int s_countdown_value = 3;
static int s_elapsed_seconds = 0;

static bool s_is_paused = false;
static bool s_stay_awake = false;

static AppTimer *s_countdown_timer;

static char s_countdown_text[4];
static char s_clock_text[12];
static char s_elapsed_text[12];
static char s_metric_value_text[24];
static char s_metric_unit_text[12];
static char s_metric_label_text[24];

static int s_current_bpm = 0;
static bool s_heart_rate_available = false;
static int s_seconds_since_last_bpm_read = 0;

static int s_current_steps = 0;
static int s_steps_at_start = 0;
static int s_seconds_since_last_steps_read = 0;

// Statistics accumulated over the entire session for the final summary
static long s_bpm_sum = 0;
static int s_bpm_sample_count = 0;
static int s_bpm_max = 0;

// Vertical scrolling for the summary screen
static int s_summary_scroll_offset = 0;
static int s_summary_content_height = 0;

static void draw_pause_icon(GContext *ctx, int16_t x, int16_t y, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(x, y, 4, 14), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(x + 8, y, 4, 14), 0, GCornerNone);
}

static void draw_play_icon(GContext *ctx, int16_t x, int16_t y, GColor color) {
  GPoint triangle_points[3] = {
    GPoint(x, y),
    GPoint(x, y + 14),
    GPoint(x + 12, y + 7)
  };

  GPathInfo triangle_info = {
    .num_points = 3,
    .points = triangle_points
  };

  GPath *triangle_path = gpath_create(&triangle_info);

  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, triangle_path);
  gpath_destroy(triangle_path);
}

static void draw_stop_icon(GContext *ctx, int16_t x, int16_t y, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(x, y, 14, 14), 0, GCornerNone);
}

static void draw_bulb_icon(GContext *ctx, int16_t x, int16_t y,
                             GColor color, bool filled) {
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 2);

  if (filled) {
    graphics_fill_circle(ctx, GPoint(x + 7, y + 6), 6);
  } else {
    graphics_draw_circle(ctx, GPoint(x + 7, y + 6), 6);

    graphics_draw_line(ctx, GPoint(x + 4, y + 5), GPoint(x + 7, y + 10));
    graphics_draw_line(ctx, GPoint(x + 7, y + 10), GPoint(x + 10, y + 5));
  }

  graphics_draw_line(ctx, GPoint(x + 4, y + 14), GPoint(x + 10, y + 14));
  graphics_draw_line(ctx, GPoint(x + 5, y + 17), GPoint(x + 9, y + 17));
}

static void draw_check_icon(GContext *ctx, int16_t x, int16_t y, GColor color) {
  // Vector checkmark (two segments), used once on the top button
  // of the "Finish workout?" screen.
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 3);

  graphics_draw_line(ctx, GPoint(x, y + 6), GPoint(x + 5, y + 11));
  graphics_draw_line(ctx, GPoint(x + 5, y + 11), GPoint(x + 15, y - 1));
}

// UTF-8 emojis: walk U+1F6B6, run U+1F3C3, heart U+2665.
static void draw_activity_emoji(GContext *ctx, Activity activity,
                                GRect frame, GColor color) {
  const char *emoji;

  if (activity == ACTIVITY_WALK) {
    emoji = "\xF0\x9F\x9A\xB6";
  } else if (activity == ACTIVITY_RUN) {
    emoji = "\xF0\x9F\x8F\x83";
  } else {
    emoji = "\xE2\x99\xA5";
  }

  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(
      ctx,
      emoji,
      s_emoji_font,
      frame,
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentCenter,
      NULL);
}

static void menu_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int16_t row_height = 56;
  const int16_t first_row_y = 12;

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  for (int i = 0; i < ACTIVITY_COUNT; i++) {
    int16_t row_y = first_row_y + i * row_height;
    bool selected = (i == s_selected_activity);

    if (selected) {
      graphics_context_set_fill_color(ctx, ACCENT_YELLOW);
      graphics_fill_rect(
          ctx,
          GRect(0, row_y, bounds.size.w, row_height),
          0,
          GCornerNone);
    }

    GColor icon_color = selected ? GColorBlack : GColorDarkGray;

    draw_activity_emoji(
        ctx,
        (Activity) i,
        GRect(8, row_y + 3, 60, 50),
        icon_color);

    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(
        ctx,
        tr(STR_ACTIVITY_WALK + (i)),
        fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
        GRect(76, row_y + 14, bounds.size.w - 82, 32),
        GTextOverflowModeTrailingEllipsis,
        GTextAlignmentLeft,
        NULL);
  }
}

static void update_countdown_text(void) {
  snprintf(
      s_countdown_text,
      sizeof(s_countdown_text),
      "%d",
      s_countdown_value);

  text_layer_set_text(s_countdown_number_layer, s_countdown_text);
}

static void update_clock_text(void) {
  time_t now = time(NULL);
  struct tm *current_time = localtime(&now);

  strftime(
      s_clock_text,
      sizeof(s_clock_text),
      "%I:%M %p",
      current_time);

  if (s_clock_text[0] == '0') {
    memmove(s_clock_text, s_clock_text + 1, strlen(s_clock_text));
  }
}

static void refresh_heart_rate_if_needed(void) {
  bool should_read =
      (s_seconds_since_last_bpm_read >= HEART_RATE_REFRESH_INTERVAL_SECONDS) ||
      !s_heart_rate_available;

  if (!should_read) {
    s_seconds_since_last_bpm_read++;
    return;
  }

  s_seconds_since_last_bpm_read = 0;

  int filtered_bpm = (int) health_service_peek_current_value(
      HealthMetricHeartRateBPM);

  int raw_bpm = (int) health_service_peek_current_value(
      HealthMetricHeartRateRawBPM);

  bool raw_valid = (raw_bpm >= 30 && raw_bpm <= 230);

  int display_bpm =
      (HEART_RATE_DISPLAY_USES_RAW && raw_valid) ? raw_bpm : filtered_bpm;

  if (display_bpm <= 0) {
    s_current_bpm = 0;
    s_heart_rate_available = false;
    return;
  }

  s_current_bpm = display_bpm;
  s_heart_rate_available = true;

  if (filtered_bpm > 0) {
    s_bpm_sum += filtered_bpm;
    s_bpm_sample_count++;

    if (filtered_bpm > s_bpm_max) {
      s_bpm_max = filtered_bpm;
    }
  }
}

static void refresh_steps_if_needed(void) {
  s_seconds_since_last_steps_read++;

  if (s_seconds_since_last_steps_read < STEPS_REFRESH_INTERVAL_SECONDS) {
    return;
  }

  s_seconds_since_last_steps_read = 0;

  // health_service_peek_current_value() does not apply to cumulative
  // metrics such as steps (it always returns 0 for this type).
  // Use the current daily total and subtract the total recorded at
  // the start of the session to get steps for the current session only.
  HealthValue total_today = health_service_sum_today(HealthMetricStepCount);

  int steps_since_start = (int) total_today - s_steps_at_start;

  if (steps_since_start < 0) {
    steps_since_start = 0;
  }

  s_current_steps = steps_since_start;

}

static int get_stride_length_cm(void) {
  return s_selected_activity == ACTIVITY_WALK
      ? s_profile.walk_stride_length_cm
      : s_profile.stride_length_cm;
}

// Distance in whole meters: avoids %f, which is unsupported by the
// Pebble firmware libc and can trigger floating-point issues.
static int get_distance_meters(void) {
  int stride_length_cm = get_stride_length_cm();
  return (s_current_steps * stride_length_cm) / 100;
}

static bool uses_metric_units(void) {
  return s_profile.use_metric != 0;
}

static void format_distance(char *value, size_t value_size, char *unit, size_t unit_size) {
  int distance_meters = get_distance_meters();

  // Use ASCII '.' so the decimal sits in the large custom metric font
  // (middle-dot · was not in the font subset and fell back smaller).
  if (uses_metric_units()) {
    int hundredths_km = (distance_meters * 100 + 5) / 10;
    snprintf(value, value_size, "%d.%02d", hundredths_km / 100, hundredths_km % 100);
    snprintf(unit, unit_size, "KM");
  } else {
    int hundredths_mi = (distance_meters * 100 + 804) / 1609;
    snprintf(value, value_size, "%d.%02d", hundredths_mi / 100, hundredths_mi % 100);
    snprintf(unit, unit_size, "MI");
  }
}

static void format_average_pace(char *value, size_t value_size, char *unit, size_t unit_size) {
  int distance_meters = get_distance_meters();

  if (distance_meters <= 10 || s_elapsed_seconds <= 0) {
    snprintf(value, value_size, "--:--");
    unit[0] = '\0';
    return;
  }

  if (uses_metric_units()) {
    long pace_seconds_per_km = ((long)s_elapsed_seconds * 1000) / distance_meters;
    snprintf(value, value_size, "%02ld:%02ld", pace_seconds_per_km / 60, pace_seconds_per_km % 60);
    snprintf(unit, unit_size, "/KM");
  } else {
    long pace_seconds_per_mile = ((long)s_elapsed_seconds * 1609) / distance_meters;
    snprintf(value, value_size, "%02ld:%02ld", pace_seconds_per_mile / 60, pace_seconds_per_mile % 60);
    snprintf(unit, unit_size, "/MI");
  }
}

static int get_average_bpm(void) {
  if (s_bpm_sample_count == 0) {
    return 0;
  }

  return (int) (s_bpm_sum / s_bpm_sample_count);
}

// Estimates calories burned during the session using the Keytel
// heart-rate-based formula, which is more accurate than Pebble's native
// counter for short sessions. Requires a valid heart-rate sample
// (session average). Uses only 64-bit integer arithmetic to avoid
// %f (unsupported) and overflow risk during long sessions.
//
// Men: kcal/min = (-55.0969 + 0.6309*HR + 0.1988*W + 0.2017*A) / 4.184
// Women: kcal/min = (-20.4022 + 0.4472*HR - 0.1263*W + 0.074*A) / 4.184
static int get_session_calories(void) {
  int average_bpm = get_average_bpm();

  if (average_bpm <= 0 || s_elapsed_seconds <= 0) {
    return 0;
  }

  // All coefficients are multiplied by 10000 so calculations remain integer-only.
  int64_t numerator_scaled;

  if (s_profile.is_male) {
    numerator_scaled =
        (int64_t) -550969
        + (int64_t) 6309 * average_bpm
        + (int64_t) 1988 * s_profile.weight_kg
        + (int64_t) 2017 * s_profile.age_years;
  } else {
    numerator_scaled =
        (int64_t) -204022
        + (int64_t) 4472 * average_bpm
        - (int64_t) 1263 * s_profile.weight_kg
        + (int64_t) 740 * s_profile.age_years;
  }

  if (numerator_scaled < 0) {
    // Formula not applicable (effort too low / heart rate too low):
    // display 0 rather than a meaningless negative value.
    return 0;
  }

  // kcal = numerator_scaled * elapsed_seconds / (10000 * 4.184 * 60)
  //      = numerator_scaled * elapsed_seconds / 2510400
  int64_t total_kcal =
      (numerator_scaled * (int64_t) s_elapsed_seconds) / 2510400;

  return (int) total_kcal;
}

// Estimated maximum heart rate, using integer arithmetic only.
// Men: Tanaka   HRmax = 208 - 0.7 x age
// Women: Gulati HRmax = 206 - 0.88 x age
static int get_max_heart_rate(void) {
  if (s_profile.is_male) {
    return 208 - (7 * s_profile.age_years) / 10;
  }

  return 206 - (88 * s_profile.age_years + 50) / 100;
}

// Five effort zones, expressed as a percentage of maximum heart rate:
//   Zone 1: below 60% (very light)                 -> blue
//   Zone 2: 60-70%    (base endurance)             -> green
//   Zone 3: 70-80%    (aerobic / tempo)            -> yellow
//   Zone 4: 80-90%    (hard / threshold)            -> orange
//   Zone 5: 90%+      (maximum)                    -> pastel red
// Hysteresis: the color changes only after crossing the threshold
// by several bpm, preventing flicker around zone boundaries.
#define HEART_RATE_ZONE_HYSTERESIS_BPM 3

static int s_heart_rate_zone = -1;

static int get_zone_upper_limit_bpm(int zone, int max_bpm) {
  static const int percents[4] = {60, 70, 80, 90};
  return (max_bpm * percents[zone]) / 100;
}

static int get_heart_rate_zone(int bpm) {
  int max_bpm = get_max_heart_rate();
  int zone = s_heart_rate_zone;

  if (zone < 0) {
    zone = 0;
    while (zone < 4 && bpm >= get_zone_upper_limit_bpm(zone, max_bpm)) {
      zone++;
    }
  } else {
    while (zone < 4 &&
           bpm >= get_zone_upper_limit_bpm(zone, max_bpm) +
                  HEART_RATE_ZONE_HYSTERESIS_BPM) {
      zone++;
    }

    while (zone > 0 &&
           bpm < get_zone_upper_limit_bpm(zone - 1, max_bpm) -
                 HEART_RATE_ZONE_HYSTERESIS_BPM) {
      zone--;
    }
  }

  s_heart_rate_zone = zone;
  return zone;
}

static GColor get_heart_rate_color(int bpm) {
  static const GColor zone_colors[5] = {
    GColorPictonBlue,
    GColorBrightGreen,
    GColorYellow,
    GColorRajah,
    GColorSunsetOrange 
  };

  if (!s_heart_rate_available) {
    s_heart_rate_zone = -1;
    return GColorLightGray;
  }

  return zone_colors[get_heart_rate_zone(bpm)];
}

static void get_metric_text(void) {
  int minutes = s_elapsed_seconds / 60;
  int seconds = s_elapsed_seconds % 60;
  int distance_meters = get_distance_meters();

  // No unit by default (duration, steps, or unavailable pace/speed).
  s_metric_unit_text[0] = '\0';

  switch (s_current_metric) {
    case METRIC_DURATION:
      snprintf(
          s_metric_value_text,
          sizeof(s_metric_value_text),
          "%02d:%02d",
          minutes,
          seconds);

      snprintf(
          s_metric_label_text,
          sizeof(s_metric_label_text),
          "%s",
          tr(STR_LABEL_DURATION));
      break;

    case METRIC_STEPS:
      // No unit displayed: the "STEPS" label is sufficient.
      snprintf(
          s_metric_value_text,
          sizeof(s_metric_value_text),
          "%d",
          s_current_steps);

      snprintf(
          s_metric_label_text,
          sizeof(s_metric_label_text),
          "%s",
          tr(STR_LABEL_STEPS));
      break;

    case METRIC_DISTANCE: {
      int whole_km = distance_meters / 1000;
      int decimal_hundredths = (distance_meters % 1000) / 10;

      snprintf(
          s_metric_value_text,
          sizeof(s_metric_value_text),
          "%d.%02d",
          whole_km,
          decimal_hundredths);

      snprintf(
          s_metric_unit_text,
          sizeof(s_metric_unit_text),
          "km");

      snprintf(
          s_metric_label_text,
          sizeof(s_metric_label_text),
          "%s",
          tr(STR_LABEL_DISTANCE));
      break;
    }

    case METRIC_HEART_RATE:
      if (s_heart_rate_available) {
        snprintf(
            s_metric_value_text,
            sizeof(s_metric_value_text),
            "%d",
            s_current_bpm);

        snprintf(
            s_metric_unit_text,
            sizeof(s_metric_unit_text),
            "bpm");
      } else {
        snprintf(
            s_metric_value_text,
            sizeof(s_metric_value_text),
            "%s",
            tr(STR_SENSOR_UNAVAILABLE));
      }

      snprintf(
          s_metric_label_text,
          sizeof(s_metric_label_text),
          "%s",
          tr(STR_LABEL_HEART_RATE));
      break;

    case METRIC_PACE:
      if (distance_meters > 10) {
        // Pace in seconds per kilometer, calculated using integers only.
        long pace_seconds_per_km =
            ((long) s_elapsed_seconds * 1000) / distance_meters;

        int pace_minutes = (int) (pace_seconds_per_km / 60);
        int pace_remaining_seconds = (int) (pace_seconds_per_km % 60);

        snprintf(
            s_metric_value_text,
            sizeof(s_metric_value_text),
            "%02d:%02d",
            pace_minutes,
            pace_remaining_seconds);

        snprintf(
            s_metric_unit_text,
            sizeof(s_metric_unit_text),
            "/km");
      } else {
        snprintf(
            s_metric_value_text,
            sizeof(s_metric_value_text),
            "--:--");

        snprintf(
            s_metric_unit_text,
            sizeof(s_metric_unit_text),
            "/km");
      }

      snprintf(
          s_metric_label_text,
          sizeof(s_metric_label_text),
          "%s",
          tr(STR_LABEL_PACE));
      break;

    case METRIC_SPEED:
      if (s_elapsed_seconds > 0) {
        // Speed in hundredths of km/h, calculated using integers only.
        long speed_hundredths_kmh =
            ((long) distance_meters * 360) / s_elapsed_seconds;

        int speed_whole = (int) (speed_hundredths_kmh / 100);
        int speed_decimal = (int) (speed_hundredths_kmh % 100) / 10;

        snprintf(
            s_metric_value_text,
            sizeof(s_metric_value_text),
            "%d.%d",
            speed_whole,
            speed_decimal);
      } else {
        snprintf(
            s_metric_value_text,
            sizeof(s_metric_value_text),
            "--");
      }

      snprintf(
          s_metric_unit_text,
          sizeof(s_metric_unit_text),
          "km/h");

      snprintf(
          s_metric_label_text,
          sizeof(s_metric_label_text),
          "%s",
          tr(STR_LABEL_SPEED));
      break;

    default:
      break;
  }
}

static void workout_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int16_t side_bar_width = 34;
  const int16_t content_width = bounds.size.w - side_bar_width;
  const int16_t split_y = bounds.size.h / 2;

  int elapsed_minutes = s_elapsed_seconds / 60;
  int elapsed_seconds = s_elapsed_seconds % 60;
  snprintf(s_elapsed_text, sizeof(s_elapsed_text), "%02d:%02d",
           elapsed_minutes, elapsed_seconds);

  update_clock_text();

  // RunZone-style split screen: zone-colored upper half, white lower half.
  GColor top_color = s_heart_rate_available
      ? get_heart_rate_color(s_current_bpm)
      : GColorLightGray;

  graphics_context_set_fill_color(ctx, top_color);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, split_y), 0, GCornerNone);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx,
      GRect(0, split_y, bounds.size.w, bounds.size.h - split_y),
      0, GCornerNone);

  // Current daytime in 12-hour format.
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_clock_text,
      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
      GRect(0, 0, content_width, 24),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Large elapsed run/workout time.
  graphics_draw_text(ctx, s_elapsed_text,
      s_roboto_condensed_extrabold_font,
      GRect(0, 25, content_width, 74),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // DOWN cycles these metrics for Walk/Run: Distance -> HR -> Avg Pace.
  // Workout is intentionally HR-only.
  if (s_selected_activity == ACTIVITY_TRAINING) {
    s_current_metric = METRIC_HEART_RATE;
  }

  char value[24];
  char unit[12];
  value[0] = '\0';
  unit[0] = '\0';

  switch (s_current_metric) {
    case METRIC_DISTANCE:
      format_distance(value, sizeof(value), unit, sizeof(unit));
      break;
    case METRIC_HEART_RATE:
      if (s_heart_rate_available) {
        snprintf(value, sizeof(value), "%d", s_current_bpm);
      } else {
        snprintf(value, sizeof(value), "--");
      }
      snprintf(unit, sizeof(unit), "BPM");
      break;
    case METRIC_PACE:
      format_average_pace(value, sizeof(value), unit, sizeof(unit));
      break;
    default:
      format_distance(value, sizeof(value), unit, sizeof(unit));
      break;
  }

  GFont metric_font = s_roboto_condensed_extrabold_font;
  if (strlen(value) >= 6) {
    metric_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  }

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, value,
      metric_font,
      GRect(0, split_y + 6, content_width, 70),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, SECONDARY_TEXT_COLOR);
  graphics_draw_text(ctx, unit,
      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
      GRect(0, split_y + 76, content_width, 28),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // RunZone-style right-side controls.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx,
      GRect(bounds.size.w - 34, 0, 34, bounds.size.h),
      0, GCornerNone);

  if (s_is_paused) {
    draw_play_icon(ctx, bounds.size.w - 23, 28, GColorWhite);
  } else {
    draw_pause_icon(ctx, bounds.size.w - 23, 28, GColorWhite);
  }

  // Middle button: flashlight while active; solid square while paused
  // (short-press then confirms ending the workout).
  if (s_is_paused) {
    draw_stop_icon(ctx, bounds.size.w - 23, bounds.size.h / 2 - 7, GColorWhite);
  } else {
    draw_bulb_icon(ctx, bounds.size.w - 23, bounds.size.h / 2 - 7, GColorWhite, s_stay_awake);
  }

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "...",
      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
      GRect(bounds.size.w - 34, bounds.size.h - 35, 34, 26),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void workout_tick_handler(struct tm *tick_time,
                                 TimeUnits units_changed) {
  if (!s_is_paused) {
    s_elapsed_seconds++;
    refresh_heart_rate_if_needed();
    refresh_steps_if_needed();
  }

  if (s_workout_layer != NULL) {
    layer_mark_dirty(s_workout_layer);
  }
}

static void countdown_timer_handler(void *context) {
  s_countdown_value--;

  if (s_countdown_value > 0) {
    update_countdown_text();

    s_countdown_timer = app_timer_register(
        1000,
        countdown_timer_handler,
        NULL);

    return;
  }

  s_countdown_timer = NULL;
  s_elapsed_seconds = 0;
  s_current_metric = (s_selected_activity == ACTIVITY_TRAINING)
      ? METRIC_HEART_RATE
      : METRIC_DISTANCE;

  // Short vibration to signal the actual start of the workout,
  // immediately after the countdown ends.
  vibes_short_pulse();

  window_stack_pop(true);
  window_stack_push(s_workout_window, true);
}

static void start_countdown(void) {
  s_countdown_value = 3;

  window_stack_push(s_countdown_window, true);

  s_countdown_timer = app_timer_register(
      1000,
      countdown_timer_handler,
      NULL);
}

static void menu_up_click_handler(ClickRecognizerRef recognizer,
                                  void *context) {
  s_selected_activity =
      (s_selected_activity + ACTIVITY_COUNT - 1) % ACTIVITY_COUNT;

  layer_mark_dirty(s_menu_layer);
}

static void menu_down_click_handler(ClickRecognizerRef recognizer,
                                    void *context) {
  s_selected_activity =
      (s_selected_activity + 1) % ACTIVITY_COUNT;

  layer_mark_dirty(s_menu_layer);
}

static void menu_select_click_handler(ClickRecognizerRef recognizer,
                                      void *context) {
  start_countdown();
}

static void menu_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, menu_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, menu_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, menu_select_click_handler);
}

static void menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, GColorWhite);

  s_menu_layer = layer_create(bounds);
  layer_set_update_proc(s_menu_layer, menu_layer_update_proc);
  layer_add_child(window_layer, s_menu_layer);
}

static void menu_window_unload(Window *window) {
  layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
}

static void countdown_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, GColorBlack);

  s_countdown_activity_layer =
      text_layer_create(GRect(0, 12, bounds.size.w, 28));

  text_layer_set_background_color(s_countdown_activity_layer, GColorClear);
  text_layer_set_text_color(s_countdown_activity_layer, ACCENT_YELLOW);
  text_layer_set_text(
      s_countdown_activity_layer,
      tr(STR_ACTIVITY_WALK + (s_selected_activity)));
  text_layer_set_text_alignment(
      s_countdown_activity_layer,
      GTextAlignmentCenter);
  text_layer_set_font(
      s_countdown_activity_layer,
      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));

  layer_add_child(
      window_layer,
      text_layer_get_layer(s_countdown_activity_layer));

  // Number layer, vertically centered on the screen. Adjust its
  // position with COUNTDOWN_Y_ADJUST (negative = higher).
  int16_t number_y =
      (bounds.size.h - COUNTDOWN_LAYER_HEIGHT) / 2 + COUNTDOWN_Y_ADJUST;

  s_countdown_number_layer = text_layer_create(
      GRect(0, number_y, bounds.size.w, COUNTDOWN_LAYER_HEIGHT));

  text_layer_set_background_color(s_countdown_number_layer, GColorClear);
  text_layer_set_text_color(s_countdown_number_layer, GColorWhite);
  text_layer_set_text_alignment(
      s_countdown_number_layer,
      GTextAlignmentCenter);
  text_layer_set_font(
      s_countdown_number_layer,
      s_roboto_condensed_extrabold_countdown_font);

  layer_add_child(
      window_layer,
      text_layer_get_layer(s_countdown_number_layer));

  update_countdown_text();
}

static void countdown_window_unload(Window *window) {
  text_layer_destroy(s_countdown_number_layer);
  s_countdown_number_layer = NULL;

  text_layer_destroy(s_countdown_activity_layer);
  s_countdown_activity_layer = NULL;
}

static void workout_up_click_handler(ClickRecognizerRef recognizer,
                                     void *context) {
  s_is_paused = !s_is_paused;

  layer_mark_dirty(s_workout_layer);
}

static void workout_select_click_handler(ClickRecognizerRef recognizer,
                                         void *context) {
  if (s_is_paused) {
    window_stack_push(s_stop_confirm_window, true);
  }
}

static bool is_metric_available(WorkoutMetric metric) {
  if (s_selected_activity == ACTIVITY_TRAINING) {
    return metric == METRIC_HEART_RATE;
  }

  return metric == METRIC_DISTANCE ||
         metric == METRIC_HEART_RATE ||
         metric == METRIC_PACE;
}

static WorkoutMetric get_next_metric(WorkoutMetric current) {
  WorkoutMetric next = current;

  do {
    next = (WorkoutMetric) ((next + 1) % METRIC_COUNT);
  } while (!is_metric_available(next));

  return next;
}

static void workout_down_click_handler(ClickRecognizerRef recognizer,
                                       void *context) {
  s_current_metric = get_next_metric(s_current_metric);

  layer_mark_dirty(s_workout_layer);
}

static void workout_back_click_handler(ClickRecognizerRef recognizer,
                                       void *context) {
  // Intentionally empty: prevents accidental exits during the session.
  // The only exit path is through pause and then the confirmation screen.
}

static void workout_back_long_click_handler(ClickRecognizerRef recognizer,
                                          void *context) {
  s_stay_awake = !s_stay_awake;
  light_enable(s_stay_awake);
  vibes_short_pulse();
}

static void workout_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, workout_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, workout_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, workout_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, workout_back_click_handler);
  window_long_click_subscribe(
      BUTTON_ID_SELECT,
      700,
      workout_back_long_click_handler,
      NULL);
}

static void workout_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, GColorWhite);

  s_is_paused = false;
  s_stay_awake = false;
  light_enable(false);
  s_current_bpm = 0;
  s_heart_rate_available = false;
  s_heart_rate_zone = -1;
  s_seconds_since_last_bpm_read = 0;
  s_current_steps = 0;
  s_seconds_since_last_steps_read = 0;

  HealthValue starting_steps = health_service_sum_today(HealthMetricStepCount);
  s_steps_at_start = (int) starting_steps;
  if (s_steps_at_start < 0) {
    s_steps_at_start = 0;
  }

  s_bpm_sum = 0;
  s_bpm_sample_count = 0;
  s_bpm_max = 0;

  health_service_set_heart_rate_sample_period(1);

  s_workout_layer = layer_create(bounds);
  layer_set_update_proc(s_workout_layer, workout_layer_update_proc);
  layer_add_child(window_layer, s_workout_layer);

  tick_timer_service_subscribe(
      SECOND_UNIT,
      workout_tick_handler);
}

static void workout_window_unload(Window *window) {
  tick_timer_service_unsubscribe();

  health_service_set_heart_rate_sample_period(0);

  layer_destroy(s_workout_layer);
  s_workout_layer = NULL;
}

static void stop_confirm_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int16_t side_bar_width = 34;
  int16_t content_width = bounds.size.w - side_bar_width;

  graphics_context_set_fill_color(ctx, ACCENT_YELLOW);
  graphics_fill_rect(
      ctx,
      GRect(0, 0, content_width, bounds.size.h),
      0,
      GCornerNone);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(
      ctx,
      GRect(content_width, 0, side_bar_width, bounds.size.h),
      0,
      GCornerNone);

  draw_activity_emoji(
      ctx,
      s_selected_activity,
      GRect(0, 46, content_width, 60),
      GColorBlack);

  graphics_context_set_text_color(ctx, GColorBlack);
  
graphics_draw_text(
      ctx,
      tr(STR_STOP_QUESTION),
      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(6, 118, content_width - 12, 64),
      GTextOverflowModeWordWrap,
      GTextAlignmentCenter,
      NULL);

  // Vector checkmark on the top button instead of the "v" glyph.
  draw_check_icon(ctx, content_width + 9, 48, GColorWhite);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(
      ctx,
      "x",
      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(content_width, 168, side_bar_width, 30),
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentCenter,
      NULL);
}

static void stop_confirm_up_click_handler(ClickRecognizerRef recognizer,
                                          void *context) {
  tick_timer_service_unsubscribe();
  window_stack_pop(true);
  window_stack_pop(true);
  window_stack_push(s_summary_window, true);
}

static void stop_confirm_down_click_handler(ClickRecognizerRef recognizer,
                                            void *context) {
  window_stack_pop(true);
}

static void stop_confirm_click_config_provider(void *context) {
  window_single_click_subscribe(
      BUTTON_ID_UP,
      stop_confirm_up_click_handler);

  window_single_click_subscribe(
      BUTTON_ID_DOWN,
      stop_confirm_down_click_handler);

  window_single_click_subscribe(
      BUTTON_ID_BACK,
      stop_confirm_down_click_handler);
}

static void stop_confirm_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_stop_confirm_layer = layer_create(bounds);
  layer_set_update_proc(
      s_stop_confirm_layer,
      stop_confirm_layer_update_proc);

  layer_add_child(window_layer, s_stop_confirm_layer);
}

static void stop_confirm_window_unload(Window *window) {
  layer_destroy(s_stop_confirm_layer);
  s_stop_confirm_layer = NULL;
}

static void draw_summary_line(GContext *ctx, int16_t y, const char *value,
                              const char *unit, const char *label,
                              GFont value_font) {
  GSize value_size = graphics_text_layout_get_content_size(
      value,
      value_font,
      GRect(0, 0, 400, 50),
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(
      ctx,
      value,
      value_font,
      GRect(6, y, 188, 44),
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft,
      NULL);

  // The unit keeps its original size (Gothic 28 bold), to the right of the value.
  if (unit[0] != '\0') {
    graphics_draw_text(
        ctx,
        unit,
        fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
        GRect(6 + value_size.w + 6, y + SUMMARY_UNIT_Y_OFFSET, 110, 34),
        GTextOverflowModeTrailingEllipsis,
        GTextAlignmentLeft,
        NULL);
  }

  graphics_context_set_text_color(ctx, SECONDARY_TEXT_COLOR);
  graphics_draw_text(
      ctx,
      label,
      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
      GRect(6, y + 42, 188, 22),
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft,
      NULL);
}

static void summary_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, ACCENT_YELLOW);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  if (s_elapsed_seconds < MINIMUM_SESSION_SECONDS) {
    int minutes = s_elapsed_seconds / 60;
    int seconds = s_elapsed_seconds % 60;
    char duration_text[8];

    snprintf(
        duration_text,
        sizeof(duration_text),
        "%02d:%02d",
        minutes,
        seconds);

    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(
        ctx,
        tr(STR_TOO_SHORT),
        fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
        GRect(6, 24, bounds.size.w - 12, 40),
        GTextOverflowModeWordWrap,
        GTextAlignmentCenter,
        NULL);

    graphics_draw_text(
        ctx,
        duration_text,
        s_roboto_condensed_extrabold_font,
        GRect(0, 74, bounds.size.w, 80),
        GTextOverflowModeTrailingEllipsis,
        GTextAlignmentCenter,
        NULL);

    graphics_context_set_text_color(ctx, SECONDARY_TEXT_COLOR);
    graphics_draw_text(
        ctx,
        tr(STR_MIN_REQUIRED),
        fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
        GRect(6, 160, bounds.size.w - 12, 60),
        GTextOverflowModeWordWrap,
        GTextAlignmentCenter,
        NULL);


    return;
  }

  int minutes = s_elapsed_seconds / 60;
  int seconds = s_elapsed_seconds % 60;
  int distance_meters = get_distance_meters();

  char duration_text[8];
  char steps_text[16];
  char distance_text[16];
  char distance_unit[8];
  char pace_text[16];
  char pace_unit[8];
  char speed_text[16];
  char speed_unit[8];
  char bpm_avg_text[24];
  char bpm_max_text[24];
  char calories_text[16];

  snprintf(duration_text, sizeof(duration_text), "%02d:%02d", minutes, seconds);
  snprintf(steps_text, sizeof(steps_text), "%d", s_current_steps);
  format_distance(distance_text, sizeof(distance_text), distance_unit, sizeof(distance_unit));
  format_average_pace(pace_text, sizeof(pace_text), pace_unit, sizeof(pace_unit));

  if (s_elapsed_seconds > 0) {
    if (uses_metric_units()) {
      long speed_hundredths_kmh = ((long) distance_meters * 360) / s_elapsed_seconds;
      int speed_whole = (int) (speed_hundredths_kmh / 100);
      int speed_decimal = (int) (speed_hundredths_kmh % 100) / 10;
      snprintf(speed_text, sizeof(speed_text), "%d.%d", speed_whole, speed_decimal);
      snprintf(speed_unit, sizeof(speed_unit), "km/h");
    } else {
      long speed_hundredths_mph = ((long) distance_meters * 3600) / (1609L * s_elapsed_seconds);
      int speed_whole = (int) (speed_hundredths_mph / 100);
      int speed_decimal = (int) (speed_hundredths_mph % 100) / 10;
      snprintf(speed_text, sizeof(speed_text), "%d.%d", speed_whole, speed_decimal);
      snprintf(speed_unit, sizeof(speed_unit), "mph");
    }
  } else {
    snprintf(speed_text, sizeof(speed_text), "--");
    speed_unit[0] = '\0';
  }

  int average_bpm = get_average_bpm();

  if (average_bpm > 0) {
    snprintf(bpm_avg_text, sizeof(bpm_avg_text), "%d", average_bpm);
  } else {
    snprintf(bpm_avg_text, sizeof(bpm_avg_text), "%s", tr(STR_SENSOR_UNAVAILABLE));
  }

  if (s_bpm_max > 0) {
    snprintf(bpm_max_text, sizeof(bpm_max_text), "%d", s_bpm_max);
  } else {
    snprintf(bpm_max_text, sizeof(bpm_max_text), "%s", tr(STR_SENSOR_UNAVAILABLE));
  }

  int calories = get_session_calories();
  snprintf(calories_text, sizeof(calories_text), "%d", calories);

  // Training: hide values that are not measured.
  bool hide_unmeasured = (s_selected_activity == ACTIVITY_TRAINING);

  GRect scroll_frame = GRect(0, -s_summary_scroll_offset, bounds.size.w, 600);

  // Title: custom font, falling back to Gothic 28 bold if the activity
  // name is too wide for the screen.
  const char *title = tr(STR_ACTIVITY_WALK + (s_selected_activity));
  GFont title_font = s_summary_font;
  GSize title_size = graphics_text_layout_get_content_size(
      title,
      title_font,
      GRect(0, 0, 400, 50),
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft);
  if (title_size.w > bounds.size.w - 12) {
    title_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  }

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(
      ctx,
      title,
      title_font,
      GRect(6, scroll_frame.origin.y + 4, bounds.size.w - 12, 44),
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft,
      NULL);

  graphics_context_set_text_color(ctx, SECONDARY_TEXT_COLOR);
  graphics_draw_text(
      ctx,
      tr(STR_SESSION_DONE),
      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(6, scroll_frame.origin.y + 46, bounds.size.w - 12, 28),
      GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft,
      NULL);

  GFont value_font = s_summary_font;
  GFont message_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont bpm_font_avg = (average_bpm > 0) ? value_font : message_font;
  GFont bpm_font_max = (s_bpm_max > 0) ? value_font : message_font;
  const char *bpm_avg_unit = (average_bpm > 0) ? "bpm" : "";
  const char *bpm_max_unit = (s_bpm_max > 0) ? "bpm" : "";

  int16_t y = scroll_frame.origin.y + 84;
  int16_t step = 72;

  draw_summary_line(ctx, y, duration_text, "", tr(STR_LABEL_DURATION), value_font);
  y += step;

  if (s_selected_activity != ACTIVITY_TRAINING) {
    draw_summary_line(ctx, y, steps_text, tr(STR_UNIT_STEPS), tr(STR_LABEL_STEPS), value_font);
    y += step;

    draw_summary_line(ctx, y, distance_text, distance_unit, tr(STR_LABEL_DISTANCE), value_font);
    y += step;

    draw_summary_line(ctx, y, pace_text, pace_unit, tr(STR_SUMMARY_PACE), value_font);
    y += step;

  }

  if (!(hide_unmeasured && average_bpm <= 0)) {
    draw_summary_line(
        ctx, y, bpm_avg_text, bpm_avg_unit, tr(STR_SUMMARY_HR_AVG), bpm_font_avg);
    y += step;
  }

  if (!(hide_unmeasured && s_bpm_max <= 0)) {
    draw_summary_line(
        ctx, y, bpm_max_text, bpm_max_unit, tr(STR_SUMMARY_HR_MAX), bpm_font_max);
    y += step;
  }

  if (!(hide_unmeasured && calories <= 0)) {
    draw_summary_line(ctx, y, calories_text, "kcal", tr(STR_SUMMARY_CALORIES), value_font);
    y += step;
  }

  s_summary_content_height = (y + 12) - scroll_frame.origin.y;
}

static void summary_up_click_handler(ClickRecognizerRef recognizer,
                                     void *context) {
  s_summary_scroll_offset -= SUMMARY_SCROLL_STEP;

  if (s_summary_scroll_offset < 0) {
    s_summary_scroll_offset = 0;
  }

  layer_mark_dirty(s_summary_layer);
}

static void summary_down_click_handler(ClickRecognizerRef recognizer,
                                       void *context) {
  GRect bounds = layer_get_bounds(s_summary_layer);
  int max_offset = s_summary_content_height - bounds.size.h;

  if (max_offset < 0) {
    max_offset = 0;
  }

  s_summary_scroll_offset += SUMMARY_SCROLL_STEP;

  if (s_summary_scroll_offset > max_offset) {
    s_summary_scroll_offset = max_offset;
  }

  layer_mark_dirty(s_summary_layer);
}

static void summary_select_click_handler(ClickRecognizerRef recognizer,
                                         void *context) {
  // The menu is the first window pushed in init() and remains at the
  // bottom of the stack: pop everything else to return to it,
  // without ever closing the application.
  while (window_stack_get_top_window() != s_menu_window) {
    window_stack_pop(true);
  }
}

static void summary_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, summary_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, summary_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, summary_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, summary_select_click_handler);
}

static void summary_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  window_set_background_color(window, ACCENT_YELLOW);

  s_summary_scroll_offset = 0;

  s_summary_layer = layer_create(bounds);
  layer_set_update_proc(s_summary_layer, summary_layer_update_proc);
  layer_add_child(window_layer, s_summary_layer);
}

static void summary_window_unload(Window *window) {
  layer_destroy(s_summary_layer);
  s_summary_layer = NULL;
}

static void profile_sanitize(void) {
  s_profile.is_male = s_profile.is_male ? 1 : 0;

  if (s_profile.age_years < 10) s_profile.age_years = 10;
  if (s_profile.age_years > 100) s_profile.age_years = 100;

  if (s_profile.height_cm < 120) s_profile.height_cm = 120;
  if (s_profile.height_cm > 230) s_profile.height_cm = 230;

  if (s_profile.weight_kg < 30) s_profile.weight_kg = 30;
  if (s_profile.weight_kg > 250) s_profile.weight_kg = 250;

  if (s_profile.stride_length_cm < 40) s_profile.stride_length_cm = 40;
  if (s_profile.stride_length_cm > 200) s_profile.stride_length_cm = 200;

  if (s_profile.walk_stride_length_cm < 40) s_profile.walk_stride_length_cm = 40;
  if (s_profile.walk_stride_length_cm > 200) s_profile.walk_stride_length_cm = 200;

  s_profile.use_metric = s_profile.use_metric ? 1 : 0;
}

static void profile_load(void) {
  if (persist_exists(PROFILE_PERSIST_KEY)) {
    size_t stored_size = persist_get_size(PROFILE_PERSIST_KEY);

    if (stored_size == sizeof(s_profile)) {
      persist_read_data(PROFILE_PERSIST_KEY, &s_profile, sizeof(s_profile));
    } else if (stored_size == sizeof(s_profile) - sizeof(int16_t)) {
      persist_read_data(PROFILE_PERSIST_KEY, &s_profile, stored_size);
      s_profile.use_metric = DEFAULT_USE_METRIC;
    }
  }

  profile_sanitize();
}

static void profile_save(void) {
  persist_write_data(PROFILE_PERSIST_KEY, &s_profile, sizeof(s_profile));
}

// Clay sends sliders as numbers and the sex choice as text;
// accept both forms.
static int tuple_to_int(const Tuple *tuple) {
  if (tuple->type == TUPLE_CSTRING) {
    return atoi(tuple->value->cstring);
  }

  return (int) tuple->value->int32;
}

static void profile_inbox_received_handler(DictionaryIterator *iter,
                                           void *context) {
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_Sex);
  if (tuple) {
    s_profile.is_male = (tuple_to_int(tuple) == 1) ? 1 : 0;
  }

  tuple = dict_find(iter, MESSAGE_KEY_Age);
  if (tuple) {
    s_profile.age_years = (int16_t) tuple_to_int(tuple);
  }

  tuple = dict_find(iter, MESSAGE_KEY_Height);
  if (tuple) {
    s_profile.height_cm = (int16_t) tuple_to_int(tuple);
  }

  tuple = dict_find(iter, MESSAGE_KEY_Weight);
  if (tuple) {
    s_profile.weight_kg = (int16_t) tuple_to_int(tuple);
  }

  tuple = dict_find(iter, MESSAGE_KEY_Stride);
  if (tuple) {
    s_profile.stride_length_cm = (int16_t) tuple_to_int(tuple);
  }

  tuple = dict_find(iter, MESSAGE_KEY_WalkStride);
  if (tuple) {
    s_profile.walk_stride_length_cm = (int16_t) tuple_to_int(tuple);
  }

  tuple = dict_find(iter, MESSAGE_KEY_UnitSystem);
  if (tuple) {
    s_profile.use_metric = (int16_t) tuple_to_int(tuple);
  }

  profile_sanitize();
  profile_save();

}

static void init(void) {
  profile_load();
  app_message_register_inbox_received(profile_inbox_received_handler);
  app_message_open(128, 128);

  s_roboto_condensed_extrabold_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_EXTRABOLD_56));

  s_roboto_condensed_extrabold_countdown_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_EXTRABOLD_100));

  s_summary_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_ROBOTO_CONDENSED_EXTRABOLD_34));

  s_emoji_font = fonts_load_custom_font(
      resource_get_handle(RESOURCE_ID_FONT_NOTO_EMOJI_40));

  s_menu_window = window_create();
  window_set_click_config_provider(
      s_menu_window,
      menu_click_config_provider);
  window_set_window_handlers(
      s_menu_window,
      (WindowHandlers) {
          .load = menu_window_load,
          .unload = menu_window_unload,
      });

  s_countdown_window = window_create();
  window_set_window_handlers(
      s_countdown_window,
      (WindowHandlers) {
          .load = countdown_window_load,
          .unload = countdown_window_unload,
      });

  s_workout_window = window_create();
  window_set_click_config_provider(
      s_workout_window,
      workout_click_config_provider);
  window_set_window_handlers(
      s_workout_window,
      (WindowHandlers) {
          .load = workout_window_load,
          .unload = workout_window_unload,
      });

  s_stop_confirm_window = window_create();
  window_set_click_config_provider(
      s_stop_confirm_window,
      stop_confirm_click_config_provider);
  window_set_window_handlers(
      s_stop_confirm_window,
      (WindowHandlers) {
          .load = stop_confirm_window_load,
          .unload = stop_confirm_window_unload,
      });

  s_summary_window = window_create();
  window_set_click_config_provider(
      s_summary_window,
      summary_click_config_provider);
  window_set_window_handlers(
      s_summary_window,
      (WindowHandlers) {
          .load = summary_window_load,
          .unload = summary_window_unload,
      });

  window_stack_push(s_menu_window, true);
}

static void deinit(void) {
  if (s_countdown_timer != NULL) {
    app_timer_cancel(s_countdown_timer);
    s_countdown_timer = NULL;
  }

  tick_timer_service_unsubscribe();

  window_destroy(s_summary_window);
  window_destroy(s_stop_confirm_window);
  window_destroy(s_workout_window);
  window_destroy(s_countdown_window);
  window_destroy(s_menu_window);

  fonts_unload_custom_font(s_roboto_condensed_extrabold_font);
  fonts_unload_custom_font(s_roboto_condensed_extrabold_countdown_font);
  fonts_unload_custom_font(s_summary_font);
  fonts_unload_custom_font(s_emoji_font);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
