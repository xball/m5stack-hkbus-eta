#include <M5Unified.h>
#include <WiFi.h>
#include <time.h>
#include <string.h>
#include <ctype.h>

#include "bus_api.h"
#include "config.h"
#include "favorites.h"
#include "ui.h"

namespace {

enum class Mode : uint8_t {
  Eta = 0,
  Menu,
  AddOp,
  AddRoute,
  AddBound,
  AddStop,
  Message,
};

Mode g_mode = Mode::Eta;
size_t g_fav_index = 0;
bool g_lang_en = false;
EtaResult g_result = {};
uint32_t g_last_fetch_ms = 0;
bool g_wifi_ok = false;

int g_menu_sel = 0;
Operator g_add_op = Operator::KMB;
char g_route[kRouteMaxLen] = {};
size_t g_route_cursor = 0;
int g_bound_sel = 0;  // 0 outbound, 1 inbound
char g_out_tc[48] = {};
char g_out_en[48] = {};
char g_in_tc[48] = {};
char g_in_en[48] = {};
RouteStopList g_stops = {};
size_t g_stop_index = 0;
char g_msg_title[32] = {};
char g_msg_body[64] = {};

// Route charset: 0-9 A-Z
constexpr char kCharset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr size_t kCharsetLen = sizeof(kCharset) - 1;

bool connectWifi(uint32_t timeout_ms = 20000) {
  if (WIFI_SSID[0] == '\0' || strcmp(WIFI_SSID, "YOUR_WIFI_SSID") == 0) {
    Ui::drawBoot("Set WIFI_SSID in config.h");
    return false;
  }
  Ui::drawConnecting(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
    delay(250);
    M5.update();
  }
  return WiFi.status() == WL_CONNECTED;
}

void syncTime() {
  // POSIX: HKT-8 = UTC+8. time() stays UTC; localtime_r uses TZ.
  setenv("TZ", "HKT-8", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "hk.pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < 40; ++i) {
    if (time(nullptr) > 1700000000) {
      return;
    }
    delay(250);
  }
}

void showEtaScreen(bool refreshing) {
  g_wifi_ok = (WiFi.status() == WL_CONNECTED);
  const size_t n = Favorites::count();
  if (n == 0) {
    Ui::drawEmpty(g_lang_en, g_wifi_ok);
    return;
  }
  if (g_fav_index >= n) {
    g_fav_index = 0;
  }
  Ui::drawScreen(g_fav_index, n, Favorites::get(g_fav_index), g_result, g_lang_en,
                 g_wifi_ok, refreshing);
}

void refreshEta(bool show_busy) {
  g_wifi_ok = (WiFi.status() == WL_CONNECTED);
  const size_t n = Favorites::count();
  if (n == 0) {
    g_result = {};
    g_result.status = FetchStatus::Empty;
    showEtaScreen(false);
    return;
  }
  if (g_fav_index >= n) {
    g_fav_index = 0;
  }
  if (show_busy) {
    showEtaScreen(true);
  }
  g_result = BusApi::fetch(*Favorites::get(g_fav_index));
  g_last_fetch_ms = millis();
  showEtaScreen(false);
}

void enterEta() {
  g_mode = Mode::Eta;
  refreshEta(true);
}

void showMessage(const char *title, const char *body) {
  strncpy(g_msg_title, title ? title : "", sizeof(g_msg_title) - 1);
  strncpy(g_msg_body, body ? body : "", sizeof(g_msg_body) - 1);
  g_mode = Mode::Message;
  Ui::drawMessage(g_msg_title, g_msg_body, g_lang_en);
}

void ensureStopName() {
  if (g_stops.status != FetchStatus::Ok || g_stops.count == 0) {
    return;
  }
  if (g_stop_index >= g_stops.count) {
    g_stop_index = 0;
  }
  RouteStop &s = g_stops.stops[g_stop_index];
  if (s.name_tc[0] == '\0' && s.name_en[0] == '\0') {
    Ui::drawMessage(g_lang_en ? "Loading" : "載入中",
                    g_lang_en ? "Stop name..." : "車站名稱...", g_lang_en);
    BusApi::resolveStopName(g_add_op, s);
  }
  Ui::drawPickStop(g_stops, g_stop_index, g_lang_en);
}

size_t charsetIndex(char c) {
  c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  for (size_t i = 0; i < kCharsetLen; ++i) {
    if (kCharset[i] == c) {
      return i;
    }
  }
  return 0;
}

void bumpRouteChar() {
  if (g_route_cursor >= kRouteMaxLen - 1) {
    return;
  }
  if (g_route[g_route_cursor] == '\0') {
    // extend
    if (g_route_cursor > 0 && g_route[g_route_cursor - 1] == '\0') {
      return;
    }
    g_route[g_route_cursor] = '1';
    g_route[g_route_cursor + 1] = '\0';
  } else {
    size_t idx = charsetIndex(g_route[g_route_cursor]);
    idx = (idx + 1) % kCharsetLen;
    g_route[g_route_cursor] = kCharset[idx];
  }
}

void advanceRouteCursor() {
  if (g_route[0] == '\0') {
    g_route[0] = '1';
    g_route[1] = '\0';
    g_route_cursor = 0;
    return;
  }
  if (g_route_cursor + 1 >= kRouteMaxLen - 1) {
    return;
  }
  if (g_route[g_route_cursor + 1] == '\0') {
    if (strlen(g_route) >= kRouteMaxLen - 1) {
      return;
    }
    g_route[g_route_cursor + 1] = 'A';
    g_route[g_route_cursor + 2] = '\0';
  }
  g_route_cursor++;
}

void backspaceRoute() {
  size_t len = strlen(g_route);
  if (len == 0) {
    return;
  }
  g_route[len - 1] = '\0';
  if (g_route_cursor >= len) {
    g_route_cursor = len ? len - 1 : 0;
  } else if (g_route_cursor > 0 && g_route_cursor == len - 1) {
    // ok
  }
  if (g_route_cursor > 0 && g_route[g_route_cursor] == '\0') {
    g_route_cursor--;
  }
}

void startAdd() {
  if (Favorites::count() >= kMaxFavorites) {
    showMessage(g_lang_en ? "Full" : "已滿",
                g_lang_en ? "Max 8 favorites" : "最多 8 個收藏");
    return;
  }
  g_add_op = Operator::KMB;
  // Prefill 40X for faster entry; user can change with A/C.
  strncpy(g_route, "40X", sizeof(g_route) - 1);
  g_route_cursor = 2;
  g_bound_sel = 0;
  g_mode = Mode::AddOp;
  Ui::drawPickOperator(g_add_op, g_lang_en);
}

void loadStopsForBound() {
  const char *bound = g_bound_sel == 0 ? "outbound" : "inbound";
  Ui::drawMessage(g_lang_en ? "Loading" : "載入中",
                  g_lang_en ? "Fetching stops..." : "正在載入車站...", g_lang_en);
  delay(50);
  yield();
  g_stops.count = 0;
  g_stops.status = FetchStatus::Empty;
  BusApi::fetchRouteStops(g_add_op, g_route, bound, 1, g_stops);
  g_stop_index = 0;
  g_mode = Mode::AddStop;
  ensureStopName();
}

void saveCurrentStop() {
  if (g_stops.status != FetchStatus::Ok || g_stops.count == 0) {
    g_mode = Mode::AddBound;
    Ui::drawPickBound(g_out_tc, g_out_en, g_in_tc, g_in_en, g_bound_sel,
                     g_lang_en);
    return;
  }
  RouteStop &s = g_stops.stops[g_stop_index];
  BusApi::resolveStopName(g_add_op, s);

  Favorite fav = {};
  fav.op = g_add_op;
  fav.service_type = 1;
  fav.dir = (g_bound_sel == 0) ? 'O' : 'I';
  strncpy(fav.route, g_route, sizeof(fav.route) - 1);
  strncpy(fav.stop_id, s.stop_id, sizeof(fav.stop_id) - 1);
  strncpy(fav.label_tc, s.name_tc, sizeof(fav.label_tc) - 1);
  strncpy(fav.label_en, s.name_en[0] ? s.name_en : s.name_tc,
          sizeof(fav.label_en) - 1);

  // Free names only; keep stop_ids zeroed without huge stack temp.
  for (size_t i = 0; i < g_stops.count; ++i) {
    g_stops.stops[i].name_tc[0] = '\0';
    g_stops.stops[i].name_en[0] = '\0';
    g_stops.stops[i].stop_id[0] = '\0';
  }
  g_stops.count = 0;
  g_stops.status = FetchStatus::Empty;
  delay(100);
  yield();

  if (!Favorites::add(fav)) {
    showMessage(g_lang_en ? "Error" : "錯誤",
                g_lang_en ? "Could not save" : "無法儲存");
    return;
  }
  g_fav_index = Favorites::count() - 1;
  // Go straight to live ETA for the new favorite.
  enterEta();
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  // Longer hold threshold for menu (~1s)
  M5.BtnA.setHoldThresh(1000);
  M5.BtnB.setHoldThresh(1000);
  M5.BtnC.setHoldThresh(1000);

  Ui::begin();
  Favorites::begin();
  Ui::drawBoot("Starting...");

  g_wifi_ok = connectWifi();
  if (g_wifi_ok) {
    Ui::drawBoot("NTP sync...");
    syncTime();
  }
  enterEta();
}

void loop() {
  M5.update();

  switch (g_mode) {
    case Mode::Eta: {
      if (M5.BtnC.wasHold()) {
        g_menu_sel = 0;
        g_mode = Mode::Menu;
        Ui::drawMenu(g_menu_sel, g_lang_en);
        break;
      }
      if (M5.BtnA.wasClicked()) {
        size_t n = Favorites::count();
        if (n > 0) {
          g_fav_index = (g_fav_index + 1) % n;
          refreshEta(true);
        }
      }
      if (M5.BtnB.wasClicked()) {
        if (WiFi.status() != WL_CONNECTED) {
          g_wifi_ok = connectWifi(10000);
          if (g_wifi_ok) {
            syncTime();
          }
        }
        refreshEta(true);
      }
      if (M5.BtnC.wasClicked()) {
        g_lang_en = !g_lang_en;
        showEtaScreen(false);
      }
      if (g_wifi_ok && Favorites::count() > 0 &&
          (millis() - g_last_fetch_ms) >= REFRESH_INTERVAL_MS) {
        refreshEta(false);
      }
      break;
    }

    case Mode::Menu: {
      if (M5.BtnA.wasClicked()) {
        g_menu_sel = (g_menu_sel + 3) % 4;  // up
        Ui::drawMenu(g_menu_sel, g_lang_en);
      }
      if (M5.BtnC.wasClicked()) {
        g_menu_sel = (g_menu_sel + 1) % 4;  // down
        Ui::drawMenu(g_menu_sel, g_lang_en);
      }
      if (M5.BtnB.wasClicked()) {
        switch (g_menu_sel) {
          case 0:
            startAdd();
            break;
          case 1:
            if (Favorites::count() > 0) {
              Favorites::remove(g_fav_index);
              if (g_fav_index >= Favorites::count() && Favorites::count() > 0) {
                g_fav_index = Favorites::count() - 1;
              }
            }
            enterEta();
            break;
          case 2:
            g_lang_en = !g_lang_en;
            Ui::drawMenu(g_menu_sel, g_lang_en);
            break;
          default:
            enterEta();
            break;
        }
      }
      break;
    }

    case Mode::AddOp: {
      if (M5.BtnA.wasClicked()) {
        g_add_op = (g_add_op == Operator::KMB) ? Operator::CTB : Operator::KMB;
        Ui::drawPickOperator(g_add_op, g_lang_en);
      }
      if (M5.BtnB.wasClicked()) {
        g_mode = Mode::AddRoute;
        Ui::drawEnterRoute(g_route, g_route_cursor, g_lang_en);
      }
      if (M5.BtnC.wasClicked()) {
        enterEta();
      }
      break;
    }

    case Mode::AddRoute: {
      if (M5.BtnA.wasClicked()) {
        bumpRouteChar();
        Ui::drawEnterRoute(g_route, g_route_cursor, g_lang_en);
      }
      if (M5.BtnC.wasHold()) {
        backspaceRoute();
        Ui::drawEnterRoute(g_route, g_route_cursor, g_lang_en);
      } else if (M5.BtnC.wasClicked()) {
        advanceRouteCursor();
        Ui::drawEnterRoute(g_route, g_route_cursor, g_lang_en);
      }
      if (M5.BtnB.wasClicked()) {
        if (g_route[0] == '\0') {
          break;
        }
        // Uppercase route
        for (char *p = g_route; *p; ++p) {
          *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
        }
        g_bound_sel = 0;
        g_out_tc[0] = g_out_en[0] = g_in_tc[0] = g_in_en[0] = '\0';
        g_mode = Mode::AddBound;
        Ui::drawPickBound(g_out_tc, g_out_en, g_in_tc, g_in_en, g_bound_sel,
                         g_lang_en);
      }
      break;
    }

    case Mode::AddBound: {
      if (M5.BtnA.wasClicked() || M5.BtnC.wasClicked()) {
        g_bound_sel = 1 - g_bound_sel;
        Ui::drawPickBound(g_out_tc, g_out_en, g_in_tc, g_in_en, g_bound_sel,
                         g_lang_en);
      }
      if (M5.BtnB.wasClicked()) {
        loadStopsForBound();
      }
      break;
    }

    case Mode::AddStop: {
      if (g_stops.status != FetchStatus::Ok || g_stops.count == 0) {
        if (M5.BtnB.wasClicked()) {
          g_mode = Mode::AddBound;
          Ui::drawPickBound(g_out_tc, g_out_en, g_in_tc, g_in_en, g_bound_sel,
                           g_lang_en);
        }
        break;
      }
      if (M5.BtnA.wasClicked()) {
        g_stop_index = (g_stop_index + 1) % g_stops.count;
        ensureStopName();
      }
      if (M5.BtnC.wasClicked()) {
        g_stop_index = (g_stop_index + g_stops.count - 1) % g_stops.count;
        ensureStopName();
      }
      if (M5.BtnB.wasClicked()) {
        saveCurrentStop();
      }
      break;
    }

    case Mode::Message: {
      if (M5.BtnB.wasClicked() || M5.BtnA.wasClicked() || M5.BtnC.wasClicked()) {
        enterEta();
      }
      break;
    }
  }

  // Wi-Fi reconnect only on ETA screen
  if (g_mode == Mode::Eta) {
    static uint32_t last_wifi_try = 0;
    if (WiFi.status() != WL_CONNECTED && (millis() - last_wifi_try) > 15000) {
      last_wifi_try = millis();
      g_wifi_ok = connectWifi(8000);
      if (g_wifi_ok) {
        syncTime();
        refreshEta(true);
      } else {
        showEtaScreen(false);
      }
    }
  }

  delay(20);
}
