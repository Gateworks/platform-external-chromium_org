// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NET_PACKED_CT_EV_WHITELIST_H_
#define CHROME_BROWSER_NET_PACKED_CT_EV_WHITELIST_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "net/cert/ct_ev_whitelist.h"

namespace base {
class FilePath;
}

// An implementation of the EVCertsWhitelist that gets its data packed using
// Golomb coding to encode the difference between subsequent hash values.
// Format of the packed list:
// * First 8 bytes: First hash
// * Repeating Golomb-coded number which is the numeric difference of the
//   previous hash value from this one
//
// The resulting, unpacked list is a sorted list of hash values that can be
// efficiently searched.
class PackedEVCertsWhitelist : public net::ct::EVCertsWhitelist {
 public:
  // Unpacks the given |compressed_whitelist|. See the class documentation
  // for description of the |compressed_whitelist| format.
  explicit PackedEVCertsWhitelist(const std::string& compressed_whitelist);

  // Returns true if the |certificate_hash| appears in the EV certificate hashes
  // whitelist. Must not be called if IsValid for this instance returned false.
  bool ContainsCertificateHash(
      const std::string& certificate_hash) const override;

  // Returns true if the EV certificate hashes whitelist provided in the c'tor
  // was valid, false otherwise.
  bool IsValid() const override;

 protected:
  ~PackedEVCertsWhitelist() override;

 private:
  FRIEND_TEST_ALL_PREFIXES(PackedEVCertsWhitelistTest,
                           UncompressFailsForTooShortList);
  FRIEND_TEST_ALL_PREFIXES(PackedEVCertsWhitelistTest,
                           UncompressFailsForTruncatedList);
  FRIEND_TEST_ALL_PREFIXES(PackedEVCertsWhitelistTest,
                           UncompressFailsForInvalidValuesInList);
  FRIEND_TEST_ALL_PREFIXES(PackedEVCertsWhitelistTest,
                           UncompressesWhitelistCorrectly);

  // Given a Golomb-coded list of hashes in |compressed_whitelist|, unpack into
  // |uncompressed_list|. Returns true if the format of the compressed whitelist
  // is valid, false otherwise.
  static bool UncompressEVWhitelist(const std::string& compressed_whitelist,
                                    std::vector<uint64_t>* uncompressed_list);

  bool is_whitelist_valid_;

  // The whitelist is an array containing certificate hashes (truncated
  // to a fixed size of 8 bytes), sorted.
  // Binary search is used to locate hashes in the the array.
  // Benchmarking bsearch vs std::set (with 120K entries, doing 1.2M lookups)
  // shows that bsearch is about twice as fast as std::set lookups (and std::set
  // has additional memory overhead).
  std::vector<uint64_t> whitelist_;

  DISALLOW_COPY_AND_ASSIGN(PackedEVCertsWhitelist);
};

// Sets the EV certificate hashes whitelist from |compressed_whitelist_file|
// in |ssl_config_service|, after uncompressing it.
// If the data in |compressed_whitelist_file| is not a valid compressed
// whitelist, does nothing.
// As this function performs file operations, it should be called from a
// blocking pool worker or a file worker.
// To set the new whitelist, this function dispatches a task to the IO thread.
void SetEVWhitelistFromFile(const base::FilePath& compressed_whitelist_file);

#endif  // CHROME_BROWSER_NET_PACKED_CT_EV_WHITELIST_H_
