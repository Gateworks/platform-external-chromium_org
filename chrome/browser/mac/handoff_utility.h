// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MAC_HANDOFF_UTILITY_H_
#define CHROME_BROWSER_MAC_HANDOFF_UTILITY_H_

#import <Foundation/Foundation.h>

namespace handoff {

// The value of this key in the userInfo dictionary of an NSUserActivity
// indicates the origin. The value should not be used for any privacy or
// security sensitive operations, since any application can set the key/value
// pair.
extern NSString* const kOriginKey;

// This value indicates that an NSUserActivity originated from Chrome on iOS.
extern NSString* const kOriginiOS;

// This value indicates that an NSUserActivity originated from Chrome on Mac.
extern NSString* const kOriginMac;

// Used for UMA metrics.
enum Origin {
  ORIGIN_UNKNOWN = 0,
  ORIGIN_IOS = 1,
  ORIGIN_MAC = 2,
  ORIGIN_COUNT
};

// Returns ORIGIN_UNKNOWN if |string| is nil or unrecognized.
Origin OriginFromString(NSString* string);

}  // namespace handoff

#endif  // CHROME_BROWSER_MAC_HANDOFF_UTILITY_H_
