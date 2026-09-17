#include "favorites.h"

#include <Preferences.h>
#include <string.h>

namespace {

Preferences prefs;
Favorite g_favs[kMaxFavorites];
size_t g_count = 0;
bool g_dual = false;

bool favValid(const Favorite &f) {
  return f.route[0] != '\0' && f.stop_id[0] != '\0';
}

void persistMeta() {
  prefs.putUChar("count", static_cast<uint8_t>(g_count));
  prefs.putBool("dual", g_dual);
  prefs.putUChar("ver", 5);
}

void persistOne(size_t i) {
  char key[8];
  snprintf(key, sizeof(key), "f%u", static_cast<unsigned>(i));
  prefs.putBytes(key, &g_favs[i], sizeof(Favorite));
}

void persistAll() {
  persistMeta();
  for (size_t i = 0; i < g_count; ++i) {
    persistOne(i);
  }
  // Clear leftover slots so stale entries are not reloaded later.
  for (size_t i = g_count; i < kMaxFavorites; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "f%u", static_cast<unsigned>(i));
    prefs.remove(key);
  }
}

void seed40XIfEmpty() {
  if (g_count > 0) {
    return;
  }
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
  persistAll();
}

}  // namespace

namespace Favorites {

void begin() {
  prefs.begin("hkbus", false);
  g_dual = prefs.getBool("dual", false);
  const uint8_t stored = prefs.getUChar("count", 0);
  g_count = 0;

  // Load whatever is in NVS. Never wipe user stops just because firmware
  // struct size / version changed — copy min(stored_len, sizeof) and keep
  // entries that still have route + stop_id.
  const size_t want = stored > kMaxFavorites ? kMaxFavorites : stored;
  for (size_t i = 0; i < want; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "f%u", static_cast<unsigned>(i));
    const size_t n = prefs.getBytesLength(key);
    if (n == 0) {
      continue;
    }
    Favorite tmp = {};
    const size_t copy_n = n < sizeof(Favorite) ? n : sizeof(Favorite);
    prefs.getBytes(key, &tmp, copy_n);
    if (!favValid(tmp)) {
      continue;
    }
    if (tmp.service_type == 0) {
      tmp.service_type = 1;
    }
    g_favs[g_count++] = tmp;
  }

  // Only seed defaults when flash is empty (first boot).
  if (g_count == 0) {
    seed40XIfEmpty();
  } else {
    // Rewrite in current layout so future boots are consistent.
    persistAll();
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
  if (!favValid(fav)) {
    return false;
  }
  for (size_t i = 0; i < g_count; ++i) {
    if (strcasecmp(g_favs[i].route, fav.route) == 0 &&
        strcmp(g_favs[i].stop_id, fav.stop_id) == 0) {
      g_favs[i] = fav;
      persistAll();
      return true;
    }
  }
  if (g_count >= kMaxFavorites) {
    return false;
  }
  g_favs[g_count++] = fav;
  persistAll();
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
  persistAll();
  return true;
}

void clearAll() {
  g_count = 0;
  persistAll();
}

bool dualPane() { return g_dual; }

void setDualPane(bool dual) {
  g_dual = dual;
  prefs.putBool("dual", g_dual);
}

}  // namespace Favorites
