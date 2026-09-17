#pragma once

#include "config.h"

namespace Favorites {
void begin();  // load from NVS
size_t count();
const Favorite *get(size_t index);
bool add(const Favorite &fav);
bool remove(size_t index);
void clearAll();
}
