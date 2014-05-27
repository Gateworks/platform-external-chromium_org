// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_CONTENT_VERIFIER_H_
#define EXTENSIONS_BROWSER_CONTENT_VERIFIER_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "extensions/browser/content_verify_job.h"

namespace base {
class FilePath;
}

namespace content {
class BrowserContext;
}

namespace extensions {

class Extension;
class ContentHashFetcher;
class ContentVerifierDelegate;

// Used for managing overall content verification - both fetching content
// hashes as needed, and supplying job objects to verify file contents as they
// are read.
class ContentVerifier : public base::RefCountedThreadSafe<ContentVerifier> {
 public:
  // Takes ownership of |delegate|.
  ContentVerifier(content::BrowserContext* context,
                  ContentVerifierDelegate* delegate);
  void Start();
  void Shutdown();

  // Call this before reading a file within an extension. The caller owns the
  // returned job.
  ContentVerifyJob* CreateJobFor(const std::string& extension_id,
                                 const base::FilePath& extension_root,
                                 const base::FilePath& relative_path);

  // Called (typically by a verification job) to indicate that verification
  // failed while reading some file in |extension_id|.
  void VerifyFailed(const std::string& extension_id,
                    ContentVerifyJob::FailureReason reason);

 private:
  DISALLOW_COPY_AND_ASSIGN(ContentVerifier);

  friend class base::RefCountedThreadSafe<ContentVerifier>;
  virtual ~ContentVerifier();

  // Note that it is important for these to appear in increasing "severity"
  // order, because we use this to let command line flags increase, but not
  // decrease, the mode you're running in compared to the experiment group.
  enum Mode {
    // Do not try to fetch content hashes if they are missing, and do not
    // enforce them if they are present.
    NONE = 0,

    // If content hashes are missing, try to fetch them, but do not enforce.
    BOOTSTRAP,

    // If hashes are present, enforce them. If they are missing, try to fetch
    // them.
    ENFORCE,

    // Treat the absence of hashes the same as a verification failure.
    ENFORCE_STRICT
  };

  static Mode GetMode();

  // The mode we're running in - set once at creation.
  const Mode mode_;

  // The associated BrowserContext.
  content::BrowserContext* context_;

  scoped_ptr<ContentVerifierDelegate> delegate_;

  // For fetching content hash signatures.
  scoped_ptr<ContentHashFetcher> fetcher_;
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_CONTENT_VERIFIER_H_
