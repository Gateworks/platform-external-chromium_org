// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATHENA_HOME_HOME_CARD_CONSTANTS_H_
#define ATHENA_HOME_HOME_CARD_CONSTANTS_H_

#include "athena/athena_export.h"

namespace athena {

// The height of the home card in BOTTOM state.
ATHENA_EXPORT extern const int kHomeCardHeight;

// The height of the white drag indicator.
ATHENA_EXPORT extern const int kHomeCardDragIndicatorHeight;

// The width of the white drag indicator.
ATHENA_EXPORT extern const int kHomeCardDragIndicatorWidth;

// The margin height from the edge of home card window to the drag indicator.
ATHENA_EXPORT extern const int kHomeCardDragIndicatorMarginHeight;

// The height of the home card of MINIMIZED state.
ATHENA_EXPORT extern const int kHomeCardMinimizedHeight;

// The height of the system UI area.
ATHENA_EXPORT extern const int kSystemUIHeight;

// The view ID for the seach box in the home card.
ATHENA_EXPORT extern const int kHomeCardSearchBoxId;

}  // namespace athena

#endif  // ATHENA_HOME_HOME_CARD_CONSTANTS_H_
