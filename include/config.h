#pragma once

#include <stddef.h>
#include <stdint.h>

// Set your Wi-Fi here. Do not commit real passwords to public repos.

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#ifndef REFRESH_INTERVAL_MS
#define REFRESH_INTERVAL_MS 60000
#endif

enum class Operator : uint8_t { KMB = 0, CTB = 1 };

static constexpr size_t kMaxFavorites = 8;
static constexpr size_t kRouteMaxLen = 8;
static constexpr size_t kStopIdMaxLen = 20;
static constexpr size_t kLabelMaxLen = 40;

struct Favorite {
  Operator op;
  char route[kRouteMaxLen];
  char stop_id[kStopIdMaxLen];
  char label_tc[kLabelMaxLen];
  char label_en[kLabelMaxLen];
  uint8_t service_type;  // KMB; usually 1
  char dir;              // 'O' outbound / 'I' inbound / 0 = any
};
