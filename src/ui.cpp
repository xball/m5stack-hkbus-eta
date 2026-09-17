#include "ui.h"

#include <M5Unified.h>
#include <time.h>

namespace {

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kFg = TFT_WHITE;
constexpr uint16_t kAccent = TFT_ORANGE;
constexpr uint16_t kDim = TFT_DARKGREY;
constexpr uint16_t kOk = TFT_GREEN;
constexpr uint16_t kErr = TFT_RED;

void setCnFont() { M5.Display.setFont(&fonts::efontTW_16); }

void footer(const char *text) {
  M5.Display.fillRect(0, 214, 320, 26, TFT_NAVY);
  M5.Display.setTextColor(kFg, TFT_NAVY);
  setCnFont();
  M5.Display.setCursor(6, 218);
  M5.Display.print(text);
}

void headerBar(const char *title, bool wifi_ok) {
  M5.Display.fillRect(0, 0, 320, 28, TFT_NAVY);
  M5.Display.setTextColor(kFg, TFT_NAVY);
  setCnFont();
  M5.Display.setCursor(6, 6);
  M5.Display.print(title);
  M5.Display.setTextColor(wifi_ok ? kOk : kErr, TFT_NAVY);
  M5.Display.setCursor(280, 6);
  M5.Display.print(wifi_ok ? "WiFi" : "----");
}

int minutesUntil(time_t eta) {
  time_t now = time(nullptr);
  if (now < 100000 || eta <= 0) {
    return -1;
  }
  long diff = static_cast<long>(eta - now);
  if (diff < 0) {
    return 0;
  }
  // Floor minutes — same style as most HK bus apps (not ceil).
  return static_cast<int>(diff / 60);
}

void formatClock(time_t eta, char *buf, size_t len) {
  if (eta <= 0 || buf == nullptr || len == 0) {
    if (buf && len) {
      buf[0] = '\0';
    }
    return;
  }
  struct tm t = {};
  localtime_r(&eta, &t);
  snprintf(buf, len, "%02d:%02d", t.tm_hour, t.tm_min);
}

const char *statusMessage(FetchStatus st, bool en) {
  switch (st) {
    case FetchStatus::WifiDown:
      return en ? "Wi-Fi disconnected" : "Wi-Fi 未連線";
    case FetchStatus::HttpError:
      return en ? "Network / HTTP error" : "網絡或 HTTP 錯誤";
    case FetchStatus::ParseError:
      return en ? "Bad response" : "資料解析失敗";
    case FetchStatus::Empty:
      return en ? "No more departures" : "尾班車已過或未有班次";
    default:
      return "";
  }
}

// Truncate UTF-8 to at most max_chars code points (for dual-pane: 10 CJK chars).
void utf8Clip(const char *src, char *dst, size_t dst_len, size_t max_chars) {
  if (dst == nullptr || dst_len == 0) {
    return;
  }
  dst[0] = '\0';
  if (src == nullptr || max_chars == 0) {
    return;
  }
  size_t chars = 0;
  size_t o = 0;
  const unsigned char *p = reinterpret_cast<const unsigned char *>(src);
  while (*p && chars < max_chars && (o + 4) < dst_len) {
    size_t nbytes = 1;
    if ((*p & 0x80) == 0x00) {
      nbytes = 1;
    } else if ((*p & 0xE0) == 0xC0) {
      nbytes = 2;
    } else if ((*p & 0xF0) == 0xE0) {
      nbytes = 3;
    } else if ((*p & 0xF8) == 0xF0) {
      nbytes = 4;
    } else {
      // Invalid lead — skip one byte
      ++p;
      continue;
    }
    if (o + nbytes >= dst_len) {
      break;
    }
    for (size_t i = 0; i < nbytes; ++i) {
      dst[o++] = static_cast<char>(p[i]);
    }
    p += nbytes;
    ++chars;
  }
  dst[o] = '\0';
}

constexpr size_t kDualMaxChars = 10;

}  // namespace

namespace Ui {

void begin() {
  M5.Display.setRotation(1);
  M5.Display.fillScreen(kBg);
  M5.Display.setTextColor(kFg, kBg);
  setCnFont();
}

void drawBoot(const char *msg) {
  M5.Display.fillScreen(kBg);
  setCnFont();
  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.setTextColor(kAccent, kBg);
  M5.Display.drawString("HK Bus ETA", 160, 90);
  M5.Display.setTextColor(kFg, kBg);
  M5.Display.drawString(msg ? msg : "", 160, 130);
  M5.Display.setTextDatum(textdatum_t::top_left);
}

void drawConnecting(const char *ssid) {
  char line[64];
  snprintf(line, sizeof(line), "Wi-Fi: %s", ssid ? ssid : "");
  drawBoot(line);
}

void drawEmpty(bool lang_en, bool wifi_ok) {
  M5.Display.fillScreen(kBg);
  headerBar(lang_en ? "No favorites" : "未有收藏", wifi_ok);
  setCnFont();
  M5.Display.setTextColor(kFg, kBg);
  M5.Display.setCursor(16, 70);
  M5.Display.print(lang_en ? "Long-press C: Menu" : "長按 C 開選單");
  M5.Display.setCursor(16, 100);
  M5.Display.print(lang_en ? "Add route, then pick stop" : "新增路線後選擇車站");
  footer(lang_en ? "hold A:1/2 pane  hold C:Menu" : "長按A:單/雙欄 長按C:選單");
}

void drawEtaBlock(int x, int w, const Favorite *fav, const EtaResult &result,
                  bool lang_en, bool refreshing) {
  if (fav == nullptr) {
    return;
  }

  // Keep drawing inside this column so text cannot spill into the other half.
  const int col_w = (w > 8) ? (w - 4) : w;
  M5.Display.setClipRect(x, 28, col_w, 186);
  M5.Display.setTextWrap(false);

  setCnFont();
  M5.Display.setTextColor(kAccent, kBg);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.setCursor(x + 4, 36);
  M5.Display.print(fav->route);

  char line[48];
  setCnFont();
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.setCursor(x + 4, 62);
  const char *lab = fav->label_tc[0] ? fav->label_tc : fav->label_en;
  utf8Clip(lab, line, sizeof(line), kDualMaxChars);
  M5.Display.print(line);

  if (refreshing) {
    M5.Display.setTextColor(kAccent, kBg);
    M5.Display.setCursor(x + 4, 84);
    M5.Display.print("...");
  }

  const char *dest =
      lang_en ? (result.dest_en[0] ? result.dest_en : fav->label_en)
              : (result.dest_tc[0] ? result.dest_tc : fav->label_tc);
  utf8Clip(dest, line, sizeof(line), kDualMaxChars);
  M5.Display.setTextColor(kFg, kBg);
  M5.Display.setCursor(x + 4, 104);
  M5.Display.printf("%s%s", lang_en ? "" : "往", line);

  int y = 130;
  if (result.status != FetchStatus::Ok) {
    M5.Display.setTextColor(kErr, kBg);
    M5.Display.setCursor(x + 4, y);
    utf8Clip(statusMessage(result.status, lang_en), line, sizeof(line),
             kDualMaxChars);
    M5.Display.print(line);
  } else {
    for (size_t i = 0; i < result.count && i < 3; ++i) {
      const EtaEntry &e = result.etas[i];
      int mins = minutesUntil(e.eta_epoch);
      char clock[8];
      formatClock(e.eta_epoch, clock, sizeof(clock));
      M5.Display.setTextColor(kFg, kBg);
      M5.Display.setCursor(x + 4, y);
      if (mins <= 1) {
        M5.Display.printf("%s %s", clock, lang_en ? "Soon" : "即將");
      } else {
        M5.Display.printf("%s %d%s", clock, mins, lang_en ? "m" : "分");
      }
      y += 26;
    }
  }

  M5.Display.clearClipRect();
}

void drawScreen(size_t fav_index, size_t fav_count, const Favorite *fav,
                const EtaResult &result, bool lang_en, bool wifi_ok,
                bool refreshing, bool dual_pane) {
  if (fav == nullptr || fav_count == 0) {
    drawEmpty(lang_en, wifi_ok);
    return;
  }

  M5.Display.fillScreen(kBg);
  setCnFont();
  char title[48];
  snprintf(title, sizeof(title), "%u/%u %s%s",
           static_cast<unsigned>(fav_index + 1),
           static_cast<unsigned>(fav_count), fav->route,
           dual_pane ? " [2]" : " [1]");
  headerBar(title, wifi_ok);

  M5.Display.setTextColor(kAccent, kBg);
  M5.Display.setCursor(8, 36);
  M5.Display.print(fav->label_tc[0] ? fav->label_tc : fav->label_en);
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.setCursor(8, 56);
  M5.Display.print(fav->label_en[0] ? fav->label_en : "");

  M5.Display.setTextColor(kFg, kBg);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.setCursor(8, 78);
  M5.Display.print(fav->route);
  setCnFont();
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.setCursor(100, 86);
  M5.Display.print(fav->op == Operator::KMB ? "KMB" : "CTB");
  if (refreshing) {
    M5.Display.setTextColor(kAccent, kBg);
    M5.Display.setCursor(160, 86);
    M5.Display.print("...");
  }

  const char *dest_tc =
      result.dest_tc[0] ? result.dest_tc : fav->label_tc;
  const char *dest_en =
      result.dest_en[0] ? result.dest_en : fav->label_en;
  M5.Display.setTextColor(kFg, kBg);
  M5.Display.setCursor(8, 112);
  if (lang_en) {
    M5.Display.printf("To %s", dest_en);
  } else {
    M5.Display.printf("往 %s", dest_tc);
  }

  int y = 140;
  if (result.status != FetchStatus::Ok) {
    M5.Display.setTextColor(kErr, kBg);
    M5.Display.setCursor(8, y);
    M5.Display.print(statusMessage(result.status, lang_en));
    if (result.status == FetchStatus::HttpError && result.http_code != 0) {
      M5.Display.setCursor(8, y + 22);
      M5.Display.setTextColor(kDim, kBg);
      M5.Display.printf("HTTP %d", result.http_code);
    }
  } else {
    for (size_t i = 0; i < result.count; ++i) {
      const EtaEntry &e = result.etas[i];
      int mins = minutesUntil(e.eta_epoch);
      char clock[8];
      formatClock(e.eta_epoch, clock, sizeof(clock));

      M5.Display.setTextColor(kFg, kBg);
      M5.Display.setCursor(8, y);
      if (mins >= 0 && mins <= 1) {
        M5.Display.printf("%s  %s", clock, lang_en ? "Arriving" : "即將到站");
      } else if (mins > 1) {
        M5.Display.printf("%s  %d %s", clock, mins, lang_en ? "min" : "分鐘");
      } else {
        M5.Display.printf("%s", clock);
      }
      y += 24;
    }
  }

  footer(lang_en ? "A:Next holdA:1/2 B:Ref C:Lang holdC:Menu"
                 : "A:換站 長按A:單雙 B:更新 C:語言 長按C:選單");
}

void drawDualScreen(size_t fav_index, size_t fav_count, const Favorite *left,
                    const EtaResult &left_r, const Favorite *right,
                    const EtaResult &right_r, bool lang_en, bool wifi_ok,
                    bool refreshing) {
  if (left == nullptr || fav_count == 0) {
    drawEmpty(lang_en, wifi_ok);
    return;
  }

  M5.Display.fillScreen(kBg);
  char title[40];
  snprintf(title, sizeof(title), "%u+%u/%u [2]",
           static_cast<unsigned>(fav_index + 1),
           static_cast<unsigned>((fav_index + 1) % fav_count + 1),
           static_cast<unsigned>(fav_count));
  headerBar(title, wifi_ok);

  // Divider
  M5.Display.drawFastVLine(160, 28, 186, TFT_NAVY);

  drawEtaBlock(0, 160, left, left_r, lang_en, refreshing);
  if (right != nullptr && right != left) {
    drawEtaBlock(160, 160, right, right_r, lang_en, refreshing);
  } else {
    setCnFont();
    M5.Display.setTextColor(kDim, kBg);
    M5.Display.setCursor(170, 80);
    M5.Display.print(lang_en ? "Add 2nd stop" : "請再加一站");
  }

  footer(lang_en ? "A:Next holdA:1pane B:Ref holdC:Menu"
                 : "A:換站 長按A:單欄 B:更新 長按C:選單");
}

void drawMenu(int selected, bool lang_en) {
  M5.Display.fillScreen(kBg);
  headerBar(lang_en ? "Menu" : "選單", true);
  setCnFont();
  const char *items_tc[] = {"新增路線/選站", "刪除目前站", "語言 EN/繁", "返回"};
  const char *items_en[] = {"Add route/stop", "Delete stop", "Language", "Back"};
  for (int i = 0; i < 4; ++i) {
    M5.Display.setTextColor(i == selected ? kAccent : kFg, kBg);
    M5.Display.setCursor(24, 50 + i * 32);
    M5.Display.printf("%s %s", i == selected ? ">" : " ",
                      lang_en ? items_en[i] : items_tc[i]);
  }
  footer(lang_en ? "A:Up C:Down B:OK" : "A:上 C:下 B:確定");
}

void drawPickOperator(Operator op, bool lang_en) {
  M5.Display.fillScreen(kBg);
  headerBar(lang_en ? "Operator" : "選擇公司", true);
  setCnFont();
  M5.Display.setTextColor(op == Operator::KMB ? kAccent : kFg, kBg);
  M5.Display.setCursor(40, 70);
  M5.Display.print(op == Operator::KMB ? "> KMB / LWB" : "  KMB / LWB");
  M5.Display.setTextColor(op == Operator::CTB ? kAccent : kFg, kBg);
  M5.Display.setCursor(40, 110);
  M5.Display.print(op == Operator::CTB ? "> Citybus (CTB)" : "  Citybus (CTB)");
  footer(lang_en ? "A:Toggle B:Next C:Cancel" : "A:切換 B:下一步 C:取消");
}

void drawEnterRoute(const char *route, size_t cursor, bool lang_en) {
  M5.Display.fillScreen(kBg);
  headerBar(lang_en ? "Enter route" : "輸入路線", true);
  setCnFont();
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.setCursor(16, 50);
  M5.Display.print(lang_en ? "e.g. 1A  296D  A21" : "例如 1A  296D  A21");

  M5.Display.setFont(&fonts::Font4);
  M5.Display.setTextColor(kAccent, kBg);
  M5.Display.setCursor(16, 100);
  M5.Display.print(route && route[0] ? route : "_");

  setCnFont();
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.setCursor(16, 150);
  M5.Display.printf(lang_en ? "pos %u" : "位置 %u",
                    static_cast<unsigned>(cursor + 1));
  footer(lang_en ? "A:Char C:NextPos holdC:Del B:OK" : "A:字元 C:下一位 長按C:刪 B:確定");
}

void drawPickBound(const char *out_tc, const char *out_en, const char *in_tc,
                   const char *in_en, int selected, bool lang_en) {
  M5.Display.fillScreen(kBg);
  headerBar(lang_en ? "Direction" : "選擇方向", true);
  setCnFont();
  M5.Display.setTextColor(selected == 0 ? kAccent : kFg, kBg);
  M5.Display.setCursor(16, 60);
  M5.Display.printf("%s %s", selected == 0 ? ">" : " ",
                    lang_en ? "Outbound" : "往程 outbound");
  M5.Display.setCursor(32, 88);
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.print(lang_en ? (out_en[0] ? out_en : "-")
                           : (out_tc[0] ? out_tc : "-"));

  M5.Display.setTextColor(selected == 1 ? kAccent : kFg, kBg);
  M5.Display.setCursor(16, 130);
  M5.Display.printf("%s %s", selected == 1 ? ">" : " ",
                    lang_en ? "Inbound" : "回程 inbound");
  M5.Display.setCursor(32, 158);
  M5.Display.setTextColor(kDim, kBg);
  M5.Display.print(lang_en ? (in_en[0] ? in_en : "-")
                           : (in_tc[0] ? in_tc : "-"));
  footer(lang_en ? "A/C:Select B:Load stops" : "A/C:選擇 B:載入車站");
}

void drawPickStop(const RouteStopList &list, size_t index, bool lang_en) {
  M5.Display.fillScreen(kBg);
  headerBar(lang_en ? "Select stop to monitor" : "選擇要監察的車站", true);
  setCnFont();

  if (list.status != FetchStatus::Ok || list.count == 0) {
    M5.Display.setTextColor(kErr, kBg);
    M5.Display.setCursor(16, 80);
    M5.Display.print(statusMessage(list.status, lang_en));
    if (list.http_code != 0) {
      M5.Display.setCursor(16, 110);
      M5.Display.setTextColor(kDim, kBg);
      M5.Display.printf("HTTP %d", list.http_code);
    }
    footer(lang_en ? "B:Back" : "B:返回");
    return;
  }

  if (index >= list.count) {
    index = 0;
  }
  const RouteStop &s = list.stops[index];

  M5.Display.setTextColor(kDim, kBg);
  M5.Display.setCursor(16, 40);
  M5.Display.printf("%s %u / %u", lang_en ? "Stop" : "車站",
                    static_cast<unsigned>(index + 1),
                    static_cast<unsigned>(list.count));

  // Always show both languages so missing TW glyphs are still readable in EN.
  M5.Display.setTextColor(kAccent, kBg);
  M5.Display.setCursor(16, 70);
  M5.Display.print(s.name_tc[0] ? s.name_tc : "...");
  M5.Display.setTextColor(kFg, kBg);
  M5.Display.setCursor(16, 100);
  M5.Display.print(s.name_en[0] ? s.name_en : s.stop_id);

  M5.Display.setTextColor(kDim, kBg);
  M5.Display.setCursor(16, 140);
  M5.Display.print(lang_en ? "B = monitor this stop" : "按 B 監察此站");

  footer(lang_en ? "A:Next C:Prev B:Save" : "A:下一站 C:上一站 B:儲存");
}

void drawMessage(const char *title, const char *msg, bool lang_en) {
  (void)lang_en;
  M5.Display.fillScreen(kBg);
  headerBar(title ? title : "", true);
  setCnFont();
  M5.Display.setTextColor(kFg, kBg);
  M5.Display.setCursor(16, 90);
  M5.Display.print(msg ? msg : "");
  footer("B:OK");
}

}  // namespace Ui
