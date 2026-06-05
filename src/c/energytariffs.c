// Show energy tariffs on the Pebble watch.
// Copyright (C) 2026 Patrick van Beem (patrick@vanbeem.info)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include <pebble.h>

#include <ctype.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define INT_TO_FLOAT2(n) (n) / 1000, ((n) % 1000)/10
#define INT_TO_FLOAT3(n) (n) / 1000, (n) % 1000
#define MS_IN_MINUTE (SECONDS_PER_MINUTE * 1000)
#define MS_IN_HOUR (MINUTES_PER_HOUR * MS_IN_MINUTE)

#define FILLER_SIZE 10

static Window *s_window;
static TextLayer *s_info_layer;
static TextLayer *s_tariff_layer;
static Layer *s_graph_layer;

int s_top_area_height = 0;
int16_t s_bar_width = 0;
int16_t s_graph_offset = 0;
#define TEXTBUF_SIZE_INFO 15
static char s_buffer_info[TEXTBUF_SIZE_INFO];
#define TEXTBUF_SIZE_TARIFF 15
static char s_buffer_tariff[TEXTBUF_SIZE_TARIFF];

// We get max two days of data in the buffer.
#define TARIFFS_PER_DAY 24
#define STROOM_TARIEF_COUNT (TARIFFS_PER_DAY * 2)
int32_t s_tariffs_start_ymd = 0; // Date/time stamp of first tariff entry.
int s_tariffs_count = 0; // Number of entries filled in s_in_buf
int32_t s_tariffs[STROOM_TARIEF_COUNT];
int32_t s_tariff_calculated[STROOM_TARIEF_COUNT];
int32_t s_tar_min=0, s_tar_max=0, s_display_min=0;
bool s_display_today = true; // false = tomorrow.
static const char s_no_data[] = "geen\ndata";

// Persistency of data, so we don't have to communicate each time we start the app.
#define STORAGE_KEY_IN_BUF      0
#define STORAGE_KEY_TARIFF      1
#define STORAGE_KEY_SETTINGS    2

// Define our settings struct
typedef struct Settings {
  GColor BackgroundColor;
  GColor TextColor;
  GColor ForegroundColorPast;
  GColor ForegroundColorFuture;
  GColor HighlightColor;
  int32_t InkoopVergoeding;  // * 1000
  bool EnergieBelasting;
  bool BTW;
} Settings;
Settings s_settings;
bool s_settings_changed = false;  // So we know we should save it.

int s_today_ymd = 0;
int s_tomorrow_ymd = 0;
int s_hour_now = 0;
int s_highlight_hour = 0;

#define REQUEST_TARIFFS_DEFAULT_TIMEOUT_MS 5000
AppTimer* request_tariffs_timer = NULL;
uint32_t request_tariffs_timeout_ms = REQUEST_TARIFFS_DEFAULT_TIMEOUT_MS;

// Some forward declarations.
void synchronize_data();

int tm_to_int(struct tm *t) {
  return (t->tm_year+1900)*10000 + (t->tm_mon+1)*100 + t->tm_mday;
}  

void request_tariffs_timer_cancel() {
  if ( request_tariffs_timer ) {
    app_timer_cancel(request_tariffs_timer);
    request_tariffs_timer = NULL;
  }
}

void request_tariffs_timedout(void* data) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Timeout!");
  request_tariffs_timer = NULL;
  request_tariffs_timeout_ms = MIN(request_tariffs_timeout_ms * 6, MS_IN_HOUR);
  synchronize_data();
}

static void update_time() {
  time_t work_time = time(NULL);
  struct tm *t = localtime(&work_time);
  s_today_ymd = tm_to_int(t);
  s_hour_now = t->tm_hour;
  work_time +=  SECONDS_PER_DAY;
  t = localtime(&work_time);
  s_tomorrow_ymd = tm_to_int(t);
}

void request_tariffs() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Request tariffs.");
  DictionaryIterator *out_iter;
  AppMessageResult result = app_message_outbox_begin(&out_iter);
  if(result == APP_MSG_OK) {
    dict_write_int32(out_iter, MESSAGE_KEY_RequestData, 0);
    dict_write_int32(out_iter, MESSAGE_KEY_IncludeVat, s_settings.BTW);
    dict_write_int32(out_iter, MESSAGE_KEY_IncludeTax, s_settings.EnergieBelasting);
    result = app_message_outbox_send();
    if(result != APP_MSG_OK) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Error sending the outbox: %d", (int)result);
    }
  } else {
    // The outbox cannot be used right now
    APP_LOG(APP_LOG_LEVEL_ERROR, "Error preparing the outbox: %d", (int)result);
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Timer set to %lds.", request_tariffs_timeout_ms / 1000);
  request_tariffs_timer = app_timer_register(request_tariffs_timeout_ms, request_tariffs_timedout, NULL);
}

bool has_valid_data_for_selection() {
  return s_tariffs_start_ymd == s_today_ymd && (s_display_today  || s_tariffs_count > TARIFFS_PER_DAY);
}

void update_text() {
  if ( s_tariffs_count == 0 ) {
    s_buffer_info[0] = 0;
  } else {
    int ymd = s_display_today ? s_today_ymd : s_tomorrow_ymd;
    snprintf(s_buffer_info, TEXTBUF_SIZE_INFO, "%d-%d-%d", ymd % 100, (ymd/100) % 100, ymd / 10000);
    if ( has_valid_data_for_selection() ) {
      snprintf(s_buffer_tariff, TEXTBUF_SIZE_TARIFF, "%d:00\n%ld.%02ld", s_highlight_hour, INT_TO_FLOAT2(s_tariff_calculated[s_highlight_hour + (s_display_today ? 0 : TARIFFS_PER_DAY)]));
    } else {
      strcpy(s_buffer_tariff, s_no_data);
    }
  }
  text_layer_set_text(s_info_layer, s_buffer_info);
  text_layer_set_text(s_tariff_layer, s_buffer_tariff);
}

void redraw() {
  layer_mark_dirty(s_graph_layer);
  update_text();
}

void set_display_today(bool value) {
  if ( (s_display_today = value) ) {
    s_highlight_hour = s_hour_now;
  } else {
    s_highlight_hour = 12;
  }
  redraw();
}

void data_updated() {
  s_tar_min = s_tar_max = 0;
  for ( int idx=0; idx < STROOM_TARIEF_COUNT; idx++ ) {
    if ( idx < s_tariffs_count ) {
      s_tariff_calculated[idx] = s_tariffs[idx] + s_settings.InkoopVergoeding;
      if ( idx == 0 ) {
        s_tar_min = s_tariff_calculated[idx];
        s_tar_max = s_tariff_calculated[idx];
      } else {
        s_tar_min = MIN(s_tar_min, s_tariff_calculated[idx]);
        s_tar_max = MAX(s_tar_max, s_tariff_calculated[idx]);
      }
    } else {
      s_tariff_calculated[idx] = 0;
    }
  }
  s_display_min = s_tar_min > 0 ? 0 : s_tar_min;
  set_display_today(true);
}

void synchronize_data() {
  if ( s_tariffs_start_ymd != s_today_ymd || (s_tariffs_count <= TARIFFS_PER_DAY ) ) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Request tariffs because %d != %d.", s_tariffs_start_ymd, s_today_ymd);
    request_tariffs();
  }
}

void update_stroom_received(Tuple* tuple) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Tariffs received");
  
  int32_t* pbuffer = (int32_t*)tuple->value->data;
  time_t date = pbuffer[0];
  s_tariffs_count = MIN((tuple->length / 4) - 1, STROOM_TARIEF_COUNT);
  struct tm *t = localtime(&date);
  s_tariffs_start_ymd = tm_to_int(t);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Received tariffs for %d (%d), %d items.", date, s_tariffs_start_ymd, s_tariffs_count);
    
  // Process the update
  memcpy(s_tariffs, &pbuffer[1], s_tariffs_count * 4);
  // All OK, back to the default and continue synchronizing data.
  request_tariffs_timer_cancel();
  request_tariffs_timeout_ms = REQUEST_TARIFFS_DEFAULT_TIMEOUT_MS;

  // Store for next start
  persist_write_int(STORAGE_KEY_IN_BUF, s_tariffs_start_ymd);
  persist_write_data(STORAGE_KEY_TARIFF, s_tariffs, sizeof(s_tariffs[0]) * s_tariffs_count);

  data_updated();
}

int32_t str_to_int100000(const char* s) {
  bool negative = false;
  bool infraction = false;
  int32_t result = 0;
  int decimals = 5;
  while ( decimals > 0 ) {
    result *= 10;
    if ( *s == 0 ) {
      decimals--;
      continue;
    }
    if ( *s == '-' ) negative = true;
    else if ( *s == '.' || *s == ',' ) infraction = true;
    else if ( isdigit((int)*s) ) {
      result += *s - '0';
      if ( infraction ) decimals--;
    }
    s++;
  }
  return negative ? -result : result;
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple* tp = dict_read_first(iter);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Inbox received %ld", tp->key);
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_JSReady);
  if(tuple) {
    // PebbleKit JS is ready! Safe to send messages
    APP_LOG(APP_LOG_LEVEL_DEBUG, "JSReady received");
    synchronize_data();
    return;
  }
  tuple = dict_find(iter, MESSAGE_KEY_Stroom);
  if(tuple) {
    update_stroom_received(tuple);
    return;
  }
  // Settings
  tuple = dict_find(iter, MESSAGE_KEY_BackgroundColor);
  if(tuple) {
    s_settings.BackgroundColor = GColorFromHEX(tuple->value->int32);
    layer_mark_dirty(s_graph_layer);
    text_layer_set_background_color(s_info_layer, s_settings.BackgroundColor);
    text_layer_set_background_color(s_tariff_layer, s_settings.BackgroundColor);
    s_settings_changed = true;
  }
  tuple = dict_find(iter, MESSAGE_KEY_TextColor);
  if(tuple) {
    s_settings.TextColor = GColorFromHEX(tuple->value->int32);
    text_layer_set_text_color(s_info_layer, s_settings.TextColor);
    text_layer_set_text_color(s_tariff_layer, s_settings.TextColor);
    s_settings_changed = true;
  }
  tuple = dict_find(iter, MESSAGE_KEY_ForegroundColorPast);
  if(tuple) {
    s_settings.ForegroundColorPast = GColorFromHEX(tuple->value->int32);
    layer_mark_dirty(s_graph_layer);
    s_settings_changed = true;
  }
  tuple = dict_find(iter, MESSAGE_KEY_ForegroundColorFuture);
  if(tuple) {
    s_settings.ForegroundColorFuture = GColorFromHEX(tuple->value->int32);
    layer_mark_dirty(s_graph_layer);
    s_settings_changed = true;
  }
  tuple = dict_find(iter, MESSAGE_KEY_HighlightColor);
  if(tuple) {
    s_settings.HighlightColor = GColorFromHEX(tuple->value->int32);
    layer_mark_dirty(s_graph_layer);
    s_settings_changed = true;
  }
  tuple = dict_find(iter, MESSAGE_KEY_IncludeTax);
  if(tuple) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Tax %d", tuple->value->uint8);
    bool newvalue = tuple->value->uint8;
    if ( s_settings.EnergieBelasting != newvalue ) {
      s_settings.EnergieBelasting = newvalue;
      request_tariffs();
      s_settings_changed = true;
    }
  }
  tuple = dict_find(iter, MESSAGE_KEY_IncludeVat);
  if(tuple) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Vat %d", tuple->value->uint8);
    bool newvalue = tuple->value->uint8;
    if ( s_settings.BTW != newvalue ) {
      s_settings.BTW = newvalue;
      request_tariffs();
      s_settings_changed = true;
    }
  }
  tuple = dict_find(iter, MESSAGE_KEY_InkoopVergoeding);
  if(tuple) {
    s_settings.InkoopVergoeding = str_to_int100000(tuple->value->cstring);
    data_updated();
    s_settings_changed = true;
  }
}

static void prv_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  set_display_today(!s_display_today);
}

static void prv_down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if ( ++s_highlight_hour > 23 ) {
    if ( s_display_today ) {
      s_display_today = false;
      s_highlight_hour = 0;
    } else {
      s_highlight_hour--;
      return;
    }
  }
  redraw();
}

static void prv_up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if ( --s_highlight_hour < 0 ) {
    if ( !s_display_today ) {
      s_display_today = true;
      s_highlight_hour = 23;
    } else {
      s_highlight_hour++;
      return;
    }
  }
  redraw();
}

static void graph_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, s_settings.BackgroundColor);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  if ( !has_valid_data_for_selection() ) return;

  const int32_t tar_per_pixel = (s_tar_max - s_display_min) / bounds.size.h;
  GRect rect = GRect(s_graph_offset, 0, s_bar_width - 1, 0);
  int32_t* data = s_display_today ? s_tariff_calculated : &s_tariff_calculated[TARIFFS_PER_DAY];
  for ( int hour=0; hour < TARIFFS_PER_DAY; hour++ ) {
    if ( hour == s_highlight_hour ) {
      graphics_context_set_fill_color(ctx, s_settings.HighlightColor);
    } else if ( !s_display_today || hour >= s_hour_now ) {
      graphics_context_set_fill_color(ctx, s_settings.ForegroundColorFuture);
    } else {
      graphics_context_set_fill_color(ctx, s_settings.ForegroundColorPast);
    }
    rect.size.h = MIN((data[hour]-s_display_min) / tar_per_pixel, bounds.size.h);
    rect.origin.y = bounds.size.h - rect.size.h;
    graphics_fill_rect(ctx, rect, 3, GCornersTop);
    rect.origin.x += s_bar_width;
  }
  // for ( int idx=1; idx < 4; idx++ ) {
  //   graphics_context_set_stroke_color(ctx, GColorBlue);
  //   const int16_t x = bounds.origin.x + s_bar_width * 6 * idx - 1;
  //   graphics_draw_line(ctx, GPoint(x, bounds.origin.y), GPoint(x,bounds.size.h));
  // }
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, prv_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, prv_down_click_handler);
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_28);
  GSize font_size = graphics_text_layout_get_content_size("1", font, bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  s_top_area_height = font_size.h;

  s_info_layer = text_layer_create(GRect(0, 0, bounds.size.w, s_top_area_height));
  text_layer_set_font(s_info_layer, font);
  text_layer_set_background_color(s_info_layer, s_settings.BackgroundColor);
  text_layer_set_text_color(s_info_layer, s_settings.TextColor);
  text_layer_set_text_alignment(s_info_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_info_layer));
  
  font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  font_size = graphics_text_layout_get_content_size("1", font, bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  
  s_tariff_layer = text_layer_create(GRect(0, s_top_area_height, bounds.size.w, font_size.h * 2 + FILLER_SIZE));
  text_layer_set_font(s_tariff_layer, font);
  text_layer_set_background_color(s_tariff_layer, s_settings.BackgroundColor);
  text_layer_set_text_color(s_tariff_layer, s_settings.TextColor);
  text_layer_set_text(s_tariff_layer, s_no_data);
  text_layer_set_text_alignment(s_tariff_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_tariff_layer));
  s_top_area_height += font_size.h * 2 + FILLER_SIZE;
  
  s_bar_width = bounds.size.w / TARIFFS_PER_DAY;
  s_graph_offset = (bounds.size.w - s_bar_width * TARIFFS_PER_DAY) / 2; // Center the graph area.
  s_graph_layer = layer_create(GRect(0, s_top_area_height, bounds.size.w, bounds.size.h - s_top_area_height));
  layer_set_update_proc(s_graph_layer, graph_update_proc);
  layer_add_child(window_layer, s_graph_layer);

  if ( s_tariffs_count != 0 ) {
    data_updated();
  }
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_info_layer);
  text_layer_destroy(s_tariff_layer);
  layer_destroy(s_graph_layer);
}

static void prv_init(void) {
  // Get data from storage
  s_settings = (struct Settings){GColorBlack, GColorWhite, PBL_IF_COLOR_ELSE(GColorDarkGreen, GColorDarkGray),
    PBL_IF_COLOR_ELSE(GColorMayGreen, GColorDarkGray), PBL_IF_COLOR_ELSE(GColorGreen, GColorWhite), 2000, true, true};
  persist_read_data(STORAGE_KEY_SETTINGS, &s_settings, sizeof(s_settings));

  if ( persist_exists(STORAGE_KEY_IN_BUF) && persist_exists(STORAGE_KEY_TARIFF) ) {
    s_tariffs_start_ymd = persist_read_int(STORAGE_KEY_IN_BUF);
    s_tariffs_count = persist_get_size(STORAGE_KEY_TARIFF);
    persist_read_data(STORAGE_KEY_TARIFF, s_tariffs, s_tariffs_count);
    s_tariffs_count /= sizeof(s_tariffs[0]);
  }
  
  update_time();

  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  const bool animated = true;
  window_stack_push(s_window, animated);
  
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(256, 256);

}

static void prv_deinit(void) {
  window_destroy(s_window);

  // Store settings before we exit.
  if ( s_settings_changed ) {
    persist_write_data(STORAGE_KEY_SETTINGS, &s_settings, sizeof(s_settings));
  }
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
