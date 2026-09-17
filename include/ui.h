#pragma once

#include "bus_api.h"
#include "config.h"

namespace Ui {
void begin();
void drawBoot(const char *msg);
void drawConnecting(const char *ssid);
void drawScreen(size_t fav_index, size_t fav_count, const Favorite *fav,
                const EtaResult &result, bool lang_en, bool wifi_ok,
                bool refreshing);
void drawEmpty(bool lang_en, bool wifi_ok);
void drawMenu(int selected, bool lang_en);
void drawPickOperator(Operator op, bool lang_en);
void drawEnterRoute(const char *route, size_t cursor, bool lang_en);
void drawPickBound(const char *out_tc, const char *out_en, const char *in_tc,
                   const char *in_en, int selected, bool lang_en);
void drawPickStop(const RouteStopList &list, size_t index, bool lang_en);
void drawMessage(const char *title, const char *msg, bool lang_en);
}
