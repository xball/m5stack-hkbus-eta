#include "bus_api.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <time.h>

namespace {

void kickWatchdog() {
  yield();
  delay(1);
#if defined(ESP_IDF_VERSION_MAJOR)
  esp_task_wdt_reset();
#endif
}

time_t parseIso8601(const char *iso) {
  if (iso == nullptr || iso[0] == '\0') {
    return 0;
  }
  // e.g. 2026-03-17T14:32:00+08:00
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  char sign = '+';
  int tzh = 0, tzm = 0;
  int n = sscanf(iso, "%d-%d-%dT%d:%d:%d%c%d:%d", &year, &month, &day, &hour,
                 &minute, &second, &sign, &tzh, &tzm);
  if (n < 6) {
    return 0;
  }
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = second;
  t.tm_isdst = 0;

  // Wall clock as UTC, then subtract stated offset → true UTC epoch.
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t as_utc_wall = mktime(&t);
  setenv("TZ", "HKT-8", 1);
  tzset();
  if (as_utc_wall < 0) {
    return 0;
  }
  int off = (tzh * 60 + tzm) * 60;
  if (sign == '-') {
    off = -off;
  }
  return as_utc_wall - off;
}

void sortEtas(EtaEntry *etas, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    EtaEntry key = etas[i];
    size_t j = i;
    while (j > 0 && etas[j - 1].eta_epoch > key.eta_epoch) {
      etas[j] = etas[j - 1];
      --j;
    }
    etas[j] = key;
  }
}

void copyField(char *dst, size_t dst_len, JsonVariantConst v) {
  if (dst_len == 0) {
    return;
  }
  dst[0] = '\0';
  if (v.isNull()) {
    return;
  }
  const char *s = v.as<const char *>();
  if (s == nullptr) {
    return;
  }
  strncpy(dst, s, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

void copyCStr(char *dst, size_t dst_len, const char *s) {
  if (dst_len == 0) {
    return;
  }
  if (s == nullptr) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, s, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

EtaResult makeError(FetchStatus st, int code) {
  EtaResult r = {};
  r.status = st;
  r.http_code = code;
  return r;
}

void setStopError(RouteStopList &out, FetchStatus st, int code) {
  out.status = st;
  out.http_code = code;
  out.count = 0;
  out.bound_label_tc[0] = '\0';
  out.bound_label_en[0] = '\0';
}

String httpsGet(const String &url, int &http_code) {
  http_code = 0;
  Serial.printf("[HTTP] GET %s heap=%u\n", url.c_str(), ESP.getFreeHeap());
  kickWatchdog();

  WiFiClientSecure *client = new WiFiClientSecure();
  if (client == nullptr) {
    http_code = -100;
    return String();
  }
  client->setInsecure();
  client->setTimeout(20);

  HTTPClient http;
  http.setTimeout(20000);
  http.setReuse(false);
  http.setConnectTimeout(12000);

  String body;
  if (!http.begin(*client, url)) {
    Serial.println("[HTTP] begin failed");
    http_code = -1;
    http.end();
    delete client;
    return String();
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "M5HKBusETA/1.1");

  kickWatchdog();
  http_code = http.GET();
  kickWatchdog();

  if (http_code > 0) {
    body = http.getString();
  }
  http.end();
  client->stop();
  delete client;

  Serial.printf("[HTTP] -> %d len=%u heap=%u\n", http_code, body.length(),
                ESP.getFreeHeap());
  kickWatchdog();
  return body;
}

EtaResult parseEtaArray(JsonArrayConst data, const char *route_filter,
                        char dir_filter) {
  EtaResult result = {};
  result.status = FetchStatus::Empty;
  result.http_code = 200;

  EtaEntry buf[12];
  size_t n = 0;
  const time_t now = time(nullptr);

  for (JsonObjectConst item : data) {
    if (route_filter != nullptr && route_filter[0] != '\0') {
      const char *route = item["route"] | "";
      if (strcasecmp(route, route_filter) != 0) {
        continue;
      }
    }

    if (dir_filter == 'O' || dir_filter == 'I') {
      const char *dir = item["dir"] | "";
      char d = 0;
      if (dir[0] == 'O' || dir[0] == 'o') {
        d = 'O';
      } else if (dir[0] == 'I' || dir[0] == 'i') {
        d = 'I';
      }
      if (d != 0 && d != dir_filter) {
        continue;
      }
    }

    const char *eta_str = item["eta"] | "";
    time_t epoch = parseIso8601(eta_str);
    if (epoch == 0) {
      continue;
    }
    // Ignore buses already departed (apps hide these too).
    if (now > 1700000000 && epoch + 45 < now) {
      continue;
    }

    if (n >= 12) {
      break;
    }
    EtaEntry &e = buf[n];
    e.eta_epoch = epoch;
    copyField(e.dest_tc, sizeof(e.dest_tc), item["dest_tc"]);
    copyField(e.dest_en, sizeof(e.dest_en), item["dest_en"]);
    copyField(e.remark_tc, sizeof(e.remark_tc), item["rmk_tc"]);
    copyField(e.remark_en, sizeof(e.remark_en), item["rmk_en"]);
    n++;
  }

  sortEtas(buf, n);
  for (size_t i = 0; i < n && result.count < kMaxEtas; ++i) {
    if (result.count == 0) {
      copyCStr(result.dest_tc, sizeof(result.dest_tc), buf[i].dest_tc);
      copyCStr(result.dest_en, sizeof(result.dest_en), buf[i].dest_en);
    }
    result.etas[result.count++] = buf[i];
  }

  if (result.count > 0) {
    result.status = FetchStatus::Ok;
  }
  return result;
}

EtaResult parseEtaBody(const String &body, int code, const char *route,
                       char dir_filter) {
  if (code <= 0 || code != 200) {
    return makeError(FetchStatus::HttpError, code);
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return makeError(FetchStatus::ParseError, code);
  }
  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  if (data.isNull()) {
    return makeError(FetchStatus::ParseError, code);
  }
  return parseEtaArray(data, route, dir_filter);
}

EtaResult fetchKmb(const Favorite &fav) {
  char url[192];
  snprintf(url, sizeof(url),
           "https://data.etabus.gov.hk/v1/transport/kmb/eta/%s/%s/%u",
           fav.stop_id, fav.route, static_cast<unsigned>(fav.service_type));

  int code = 0;
  String body = httpsGet(String(url), code);
  EtaResult r = parseEtaBody(body, code, fav.route, fav.dir);
  if (r.status == FetchStatus::Ok) {
    return r;
  }

  snprintf(url, sizeof(url),
           "https://data.etabus.gov.hk/v1/transport/kmb/stop-eta/%s",
           fav.stop_id);
  body = httpsGet(String(url), code);
  EtaResult r2 = parseEtaBody(body, code, fav.route, fav.dir);
  if (r2.status == FetchStatus::Ok) {
    return r2;
  }
  return (r.status != FetchStatus::Empty) ? r : r2;
}

EtaResult fetchCtb(const Favorite &fav) {
  char url[192];
  snprintf(url, sizeof(url),
           "https://rt.data.gov.hk/v2/transport/citybus/eta/CTB/%s/%s",
           fav.stop_id, fav.route);

  int code = 0;
  String body = httpsGet(String(url), code);
  return parseEtaBody(body, code, fav.route, fav.dir);
}

bool fillStopNameKmb(RouteStop &stop) {
  char url[160];
  snprintf(url, sizeof(url),
           "https://data.etabus.gov.hk/v1/transport/kmb/stop/%s", stop.stop_id);
  int code = 0;
  String body = httpsGet(String(url), code);
  if (code != 200) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  JsonObjectConst data = doc["data"].as<JsonObjectConst>();
  if (data.isNull()) {
    return false;
  }
  copyField(stop.name_tc, sizeof(stop.name_tc), data["name_tc"]);
  copyField(stop.name_en, sizeof(stop.name_en), data["name_en"]);
  return stop.name_tc[0] != '\0' || stop.name_en[0] != '\0';
}

bool fillStopNameCtb(RouteStop &stop) {
  char url[160];
  snprintf(url, sizeof(url),
           "https://rt.data.gov.hk/v2/transport/citybus/stop/%s", stop.stop_id);
  int code = 0;
  String body = httpsGet(String(url), code);
  if (code != 200) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  JsonObjectConst data = doc["data"].as<JsonObjectConst>();
  if (data.isNull()) {
    return false;
  }
  copyField(stop.name_tc, sizeof(stop.name_tc), data["name_tc"]);
  copyField(stop.name_en, sizeof(stop.name_en), data["name_en"]);
  return stop.name_tc[0] != '\0' || stop.name_en[0] != '\0';
}

void fetchStopsKmb(const char *route, const char *bound, uint8_t service_type,
                   RouteStopList &out) {
  out.count = 0;
  out.bound_label_tc[0] = out.bound_label_en[0] = '\0';
  copyCStr(out.bound_label_en, sizeof(out.bound_label_en), bound);
  copyCStr(out.bound_label_tc, sizeof(out.bound_label_tc), bound);

  char url[192];
  snprintf(url, sizeof(url),
           "https://data.etabus.gov.hk/v1/transport/kmb/route-stop/%s/%s/%u",
           route, bound, static_cast<unsigned>(service_type));

  int code = 0;
  String body = httpsGet(String(url), code);
  if (code <= 0 || code != 200) {
    setStopError(out, FetchStatus::HttpError, code);
    return;
  }

  // Filter: only keep stop IDs — much less RAM than full route-stop objects.
  JsonDocument filter;
  filter["data"][0]["stop"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = String();  // free payload ASAP
  kickWatchdog();

  if (err) {
    setStopError(out, FetchStatus::ParseError, code);
    return;
  }
  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  if (data.isNull()) {
    setStopError(out, FetchStatus::Empty, code);
    return;
  }

  out.http_code = code;
  for (JsonObjectConst item : data) {
    kickWatchdog();
    if (out.count >= kMaxRouteStops) {
      break;
    }
    const char *sid = item["stop"] | "";
    if (sid[0] == '\0') {
      continue;
    }
    RouteStop &s = out.stops[out.count];
    copyCStr(s.stop_id, sizeof(s.stop_id), sid);
    s.name_tc[0] = s.name_en[0] = '\0';
    out.count++;
  }

  out.status = out.count > 0 ? FetchStatus::Ok : FetchStatus::Empty;
}

void fetchStopsCtb(const char *route, const char *bound, RouteStopList &out) {
  out.count = 0;
  copyCStr(out.bound_label_tc, sizeof(out.bound_label_tc), bound);
  copyCStr(out.bound_label_en, sizeof(out.bound_label_en), bound);

  char url[192];
  snprintf(url, sizeof(url),
           "https://rt.data.gov.hk/v2/transport/citybus/route-stop/CTB/%s/%s",
           route, bound);

  int code = 0;
  String body = httpsGet(String(url), code);
  if (code <= 0 || code != 200) {
    setStopError(out, FetchStatus::HttpError, code);
    return;
  }

  JsonDocument filter;
  filter["data"][0]["stop"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = String();
  kickWatchdog();

  if (err) {
    setStopError(out, FetchStatus::ParseError, code);
    return;
  }
  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  if (data.isNull()) {
    setStopError(out, FetchStatus::Empty, code);
    return;
  }

  out.http_code = code;
  for (JsonObjectConst item : data) {
    kickWatchdog();
    if (out.count >= kMaxRouteStops) {
      break;
    }
    const char *sid = item["stop"] | "";
    if (sid[0] == '\0') {
      continue;
    }
    RouteStop &s = out.stops[out.count];
    copyCStr(s.stop_id, sizeof(s.stop_id), sid);
    s.name_tc[0] = s.name_en[0] = '\0';
    out.count++;
  }

  out.status = out.count > 0 ? FetchStatus::Ok : FetchStatus::Empty;
}

}  // namespace

namespace BusApi {

EtaResult fetch(const Favorite &fav) {
  if (WiFi.status() != WL_CONNECTED) {
    return makeError(FetchStatus::WifiDown, 0);
  }
  Serial.printf("[ETA] op=%d route=%s stop=%s\n", static_cast<int>(fav.op),
                fav.route, fav.stop_id);
  switch (fav.op) {
    case Operator::KMB:
      return fetchKmb(fav);
    case Operator::CTB:
      return fetchCtb(fav);
  }
  return makeError(FetchStatus::HttpError, 0);
}

void fetchRouteStops(Operator op, const char *route, const char *bound,
                     uint8_t service_type, RouteStopList &out) {
  out.status = FetchStatus::Empty;
  out.http_code = 0;
  out.count = 0;

  if (WiFi.status() != WL_CONNECTED) {
    setStopError(out, FetchStatus::WifiDown, 0);
    return;
  }
  if (route == nullptr || route[0] == '\0' || bound == nullptr) {
    setStopError(out, FetchStatus::Empty, 0);
    return;
  }

  switch (op) {
    case Operator::KMB:
      fetchStopsKmb(route, bound, service_type, out);
      break;
    case Operator::CTB:
      fetchStopsCtb(route, bound, out);
      break;
  }
}

bool resolveStopName(Operator op, RouteStop &stop) {
  if (stop.name_tc[0] != '\0' || stop.name_en[0] != '\0') {
    return true;
  }
  bool ok = false;
  switch (op) {
    case Operator::KMB:
      ok = fillStopNameKmb(stop);
      break;
    case Operator::CTB:
      ok = fillStopNameCtb(stop);
      break;
  }
  if (!ok || (stop.name_tc[0] == '\0' && stop.name_en[0] == '\0')) {
    copyCStr(stop.name_tc, sizeof(stop.name_tc), stop.stop_id);
    copyCStr(stop.name_en, sizeof(stop.name_en), stop.stop_id);
  }
  kickWatchdog();
  return ok;
}

}  // namespace BusApi
