// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/packed_ct_ev_whitelist.h"

#include <string.h>

#include <algorithm>

#include "base/big_endian.h"
#include "base/files/file_util.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "chrome/browser/net/bit_stream_reader.h"
#include "content/public/browser/browser_thread.h"
#include "net/ssl/ssl_config_service.h"

namespace {
const uint8_t kCertHashLengthBits = 64;  // 8 bytes
const uint8_t kCertHashLength = kCertHashLengthBits / 8;
const uint64_t kGolombMParameterBits = 47;  // 2^47

void SetNewEVWhitelistInSSLConfigService(
    const scoped_refptr<net::ct::EVCertsWhitelist>& new_whitelist) {
  net::SSLConfigService::SetEVCertsWhitelist(new_whitelist);
}

int TruncatedHashesComparator(const void* v1, const void* v2) {
  const uint64_t& h1(*(static_cast<const uint64_t*>(v1)));
  const uint64_t& h2(*(static_cast<const uint64_t*>(v2)));
  if (h1 < h2)
    return -1;
  else if (h1 > h2)
    return 1;
  return 0;
}
}  // namespace

void SetEVWhitelistFromFile(const base::FilePath& compressed_whitelist_file) {
  VLOG(1) << "Setting EV whitelist from file: "
          << compressed_whitelist_file.value();
  std::string compressed_list;
  if (!base::ReadFileToString(compressed_whitelist_file, &compressed_list)) {
    VLOG(1) << "Failed reading from " << compressed_whitelist_file.value();
    return;
  }

  scoped_refptr<net::ct::EVCertsWhitelist> new_whitelist(
      new PackedEVCertsWhitelist(compressed_list));
  if (!new_whitelist->IsValid()) {
    VLOG(1) << "Failed uncompressing EV certs whitelist.";
    return;
  }

  base::Closure assign_cb =
      base::Bind(SetNewEVWhitelistInSSLConfigService, new_whitelist);
  content::BrowserThread::PostTask(
      content::BrowserThread::IO, FROM_HERE, assign_cb);
}

bool PackedEVCertsWhitelist::UncompressEVWhitelist(
    const std::string& compressed_whitelist,
    std::vector<uint64_t>* uncompressed_list) {
  internal::BitStreamReader reader(base::StringPiece(
      compressed_whitelist.data(), compressed_whitelist.size()));
  std::vector<uint64_t> result;

  VLOG(1) << "Uncompressing EV whitelist of size "
          << compressed_whitelist.size();
  uint64_t curr_hash(0);
  if (!reader.ReadBits(kCertHashLengthBits, &curr_hash)) {
    VLOG(1) << "Failed reading first hash.";
    return false;
  }
  result.push_back(curr_hash);
  // M is the tunable parameter used by the Golomb coding.
  static const uint64_t kGolombParameterM = static_cast<uint64_t>(1)
                                            << kGolombMParameterBits;

  while (reader.BitsLeft() > kGolombMParameterBits) {
    uint64_t read_prefix = 0;
    if (!reader.ReadUnaryEncoding(&read_prefix)) {
      VLOG(1) << "Failed reading unary-encoded prefix.";
      return false;
    }
    if (read_prefix > (UINT64_MAX / kGolombParameterM)) {
      VLOG(1) << "Received value that would cause overflow: " << read_prefix;
      return false;
    }

    uint64_t r = 0;
    if (!reader.ReadBits(kGolombMParameterBits, &r)) {
      VLOG(1) << "Failed reading " << kGolombMParameterBits << " bits.";
      return false;
    }
    DCHECK_LT(r, kGolombParameterM);

    uint64_t curr_diff = read_prefix * kGolombParameterM + r;
    curr_hash += curr_diff;

    result.push_back(curr_hash);
  }

  uncompressed_list->swap(result);
  return true;
}

PackedEVCertsWhitelist::PackedEVCertsWhitelist(
    const std::string& compressed_whitelist)
    : is_whitelist_valid_(false) {
  if (!UncompressEVWhitelist(compressed_whitelist, &whitelist_)) {
    whitelist_.clear();
    return;
  }

  is_whitelist_valid_ = true;
}

PackedEVCertsWhitelist::~PackedEVCertsWhitelist() {
}

bool PackedEVCertsWhitelist::ContainsCertificateHash(
    const std::string& certificate_hash) const {
  DCHECK(!whitelist_.empty());
  uint64_t hash_to_lookup;

  base::ReadBigEndian(certificate_hash.data(), &hash_to_lookup);
  return bsearch(&hash_to_lookup,
                 &whitelist_[0],
                 whitelist_.size(),
                 kCertHashLength,
                 TruncatedHashesComparator) != NULL;
}

bool PackedEVCertsWhitelist::IsValid() const {
  return is_whitelist_valid_;
}
