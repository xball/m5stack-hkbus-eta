#pragma once

#include "config.h"

namespace Favorites {
void begin();  // load from NVS (survives firmware upload)
size_t count();
const Favorite *get(size_t index);
bool add(const Favorite &fav);
bool remove(size_t index);
void clearAll();

// Display: one column vs two columns (left+right). Persisted in NVS.
bool dualPane();
void setDualPane(bool dual);
}
