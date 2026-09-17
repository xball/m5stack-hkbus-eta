#pragma once

#include <Arduino.h>
#include "config.h"

static constexpr size_t kMaxEtas = 3;
static constexpr size_t kMaxRouteStops = 40;

struct EtaEntry {
  time_t eta_epoch;
  char dest_tc[48];
  char dest_en[48];
  char remark_tc[40];
  char remark_en[40];
};

enum class FetchStatus : uint8_t {
  Ok = 0,
  WifiDown,
  HttpError,
  ParseError,
  Empty,
};

struct EtaResult {
  FetchStatus status;
  int http_code;
  char dest_tc[48];
  char dest_en[48];
  EtaEntry etas[kMaxEtas];
  size_t count;
};

struct RouteStop {
  char stop_id[kStopIdMaxLen];
  char name_tc[kLabelMaxLen];
  char name_en[kLabelMaxLen];
};

// Large — keep in global/BSS only, never allocate on stack or return by value.
struct RouteStopList {
  FetchStatus status;
  int http_code;
  char bound_label_tc[48];
  char bound_label_en[48];
  RouteStop stops[kMaxRouteStops];
  size_t count;
};

namespace BusApi {
EtaResult fetch(const Favorite &fav);
// Fills out (must be global/static). Avoids stack overflow from large structs.
void fetchRouteStops(Operator op, const char *route, const char *bound,
                     uint8_t service_type, RouteStopList &out);
bool resolveStopName(Operator op, RouteStop &stop);
}
