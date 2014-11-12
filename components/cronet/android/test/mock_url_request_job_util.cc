// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_url_request_job_util.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/threading/sequenced_worker_pool.h"
#include "jni/MockUrlRequestJobUtil_jni.h"
#include "net/test/url_request/url_request_failed_job.h"
#include "net/test/url_request/url_request_mock_http_job.h"

namespace cronet {

void AddUrlInterceptors(JNIEnv* env, jclass jcaller) {
  base::FilePath test_files_root;
  PathService::Get(base::DIR_ANDROID_APP_DATA, &test_files_root);
  net::URLRequestMockHTTPJob::AddUrlHandler(
      test_files_root, new base::SequencedWorkerPool(1, "Worker"));
  net::URLRequestFailedJob::AddUrlHandler();
}

jstring GetMockUrl(JNIEnv* jenv, jclass jcaller, jstring jpath) {
  base::FilePath path(base::android::ConvertJavaStringToUTF8(jenv, jpath));
  GURL url(net::URLRequestMockHTTPJob::GetMockUrl(path));
  return base::android::ConvertUTF8ToJavaString(jenv, url.spec()).Release();
}

jstring GetMockUrlWithFailure(JNIEnv* jenv,
                              jclass jcaller,
                              jstring jpath,
                              jint jphase,
                              jint jnet_error) {
  base::FilePath path(base::android::ConvertJavaStringToUTF8(jenv, jpath));
  GURL url(net::URLRequestMockHTTPJob::GetMockUrlWithFailure(
      path,
      static_cast<net::URLRequestMockHTTPJob::FailurePhase>(jphase),
      static_cast<int>(jnet_error)));
  return base::android::ConvertUTF8ToJavaString(jenv, url.spec()).Release();
}

bool RegisterMockUrlRequestJobUtil(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace cronet
