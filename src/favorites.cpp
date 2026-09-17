#include "favorites.h"

#include <Preferences.h>
#include <string.h>

namespace {

Preferences prefs;
Favorite g_favs[kMaxFavorites];
size_t g_count = 0;

void persist() {
  prefs.putUChar("count", static_cast<uint8_t>(g_count));
  prefs.putUChar("ver", 4);
  for (size_t i = 0; i < g_count; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "f%u", static_cast<unsigned>(i));
    prefs.putBytes(key, &g_favs[i], sizeof(Favorite));
  }
}

void seed40X() {
  // KMB 40X termini — Wu Kai Sha (O→葵涌) / Kwai Chung (I→烏溪沙)
  Favorite a = {};
  a.op = Operator::KMB;
  a.service_type = 1;
  a.dir = 'O';
  strncpy(a.route, "40X", sizeof(a.route) - 1);
  strncpy(a.stop_id, "9AD30F8EDBC3F139", sizeof(a.stop_id) - 1);
  strncpy(a.label_tc, "烏溪沙站", sizeof(a.label_tc) - 1);
  strncpy(a.label_en, "Wu Kai Sha Stn", sizeof(a.label_en) - 1);

  Favorite b = {};
  b.op = Operator::KMB;
  b.service_type = 1;
  b.dir = 'I';
  strncpy(b.route, "40X", sizeof(b.route) - 1);
  strncpy(b.stop_id, "DB87B8A5A56F8336", sizeof(b.stop_id) - 1);
  strncpy(b.label_tc, "葵涌邨總站", sizeof(b.label_tc) - 1);
  strncpy(b.label_en, "Kwai Chung Estate", sizeof(b.label_en) - 1);

  g_favs[0] = a;
  g_favs[1] = b;
  g_count = 2;
  persist();
}

}  // namespace

namespace Favorites {

void begin() {
  prefs.begin("hkbus", false);
  const uint8_t ver = prefs.getUChar("ver", 0);
  g_count = prefs.getUChar("count", 0);
  if (g_count > kMaxFavorites) {
    g_count = 0;
  }

  bool ok = (ver >= 4);
  for (size_t i = 0; ok && i < g_count; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "f%u", static_cast<unsigned>(i));
    size_t n = prefs.getBytesLength(key);
    if (n != sizeof(Favorite)) {
      ok = false;
      break;
    }
    prefs.getBytes(key, &g_favs[i], sizeof(Favorite));
    if (g_favs[i].route[0] == '\0' || g_favs[i].stop_id[0] == '\0') {
      ok = false;
      break;
    }
  }

  if (!ok || g_count == 0) {
    seed40X();
  }
}

size_t count() { return g_count; }

const Favorite *get(size_t index) {
  if (index >= g_count) {
    return nullptr;
  }
  return &g_favs[index];
}

bool add(const Favorite &fav) {
  if (g_count >= kMaxFavorites) {
    return false;
  }
  if (fav.route[0] == '\0' || fav.stop_id[0] == '\0') {
    return false;
  }
  // Replace duplicate route+stop
  for (size_t i = 0; i < g_count; ++i) {
    if (strcasecmp(g_favs[i].route, fav.route) == 0 &&
        strcmp(g_favs[i].stop_id, fav.stop_id) == 0) {
      g_favs[i] = fav;
      persist();
      return true;
    }
  }
  g_favs[g_count++] = fav;
  persist();
  return true;
}

bool remove(size_t index) {
  if (index >= g_count) {
    return false;
  }
  for (size_t i = index; i + 1 < g_count; ++i) {
    g_favs[i] = g_favs[i + 1];
  }
  --g_count;
  persist();
  return true;
}

void clearAll() {
  g_count = 0;
  persist();
}

}  // namespace Favorites
