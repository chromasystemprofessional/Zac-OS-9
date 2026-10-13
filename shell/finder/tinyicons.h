#pragma once

#include "items.h"

/* List view's tiny icon size (View Options; Missing Manual p. 25): 12 px,
 * "so small that the individual illustration disappears entirely" - a
 * generic folder, document, application, disk or Trash, in the item's
 * label colour, darkened when selected. */
constexpr int TINY_ICON = 12;
void paintTinyIcon(pl_canvas *c, const Item &item, int x, int y, bool highlight);
