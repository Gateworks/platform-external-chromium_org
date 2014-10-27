// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.cronet_test_apk;

import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.test.util.Feature;
import org.chromium.net.HttpUrlRequest;

import java.util.HashMap;

/**
 * Tests that use mock URLRequestJobs to simulate URL requests.
 */

public class MockUrlRequestJobTest extends CronetTestBase {
    private static final String TAG = "MockURLRequestJobTest";
    private static final String MOCK_CRONET_TEST_SUCCESS_URL =
            "http://mock.http/success.txt";
    private static final String MOCK_CRONET_TEST_REDIRECT_URL =
            "http://mock.http/redirect.html";
    private static final String MOCK_CRONET_TEST_NOTFOUND_URL =
            "http://mock.http/notfound.html";
    private static final String MOCK_CRONET_TEST_FAILED_URL =
            "http://mock.failed.request/-2";

    // Helper function to create a HttpUrlRequest with the specified url.
    private TestHttpUrlRequestListener createUrlRequestAndWaitForComplete(
            String url) {
        CronetTestActivity activity = launchCronetTestApp();
        assertNotNull(activity);
        // AddUrlInterceptors() after native application context is initialized.
        MockUrlRequestJobUtil.addUrlInterceptors();

        HashMap<String, String> headers = new HashMap<String, String>();
        TestHttpUrlRequestListener listener = new TestHttpUrlRequestListener();

        HttpUrlRequest request = activity.mRequestFactory.createRequest(
                url,
                HttpUrlRequest.REQUEST_PRIORITY_MEDIUM,
                headers,
                listener);
        request.start();
        listener.blockForComplete();
        return listener;
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testSuccessURLRequest() throws Exception {
        TestHttpUrlRequestListener listener =
                createUrlRequestAndWaitForComplete(MOCK_CRONET_TEST_SUCCESS_URL);
        assertEquals(MOCK_CRONET_TEST_SUCCESS_URL, listener.mUrl);
        assertEquals(200, listener.mHttpStatusCode);
        assertEquals("this is a text file\n",
                new String(listener.mResponseAsBytes));
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testRedirectURLRequest() throws Exception {
        TestHttpUrlRequestListener listener =
                createUrlRequestAndWaitForComplete(MOCK_CRONET_TEST_REDIRECT_URL);

        // Currently Cronet does not expose the url after redirect.
        assertEquals(MOCK_CRONET_TEST_REDIRECT_URL, listener.mUrl);
        assertEquals(200, listener.mHttpStatusCode);
        // Expect that the request is redirected to success.txt.
        assertEquals("this is a text file\n",
                new String(listener.mResponseAsBytes));
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testNotFoundURLRequest() throws Exception {
        TestHttpUrlRequestListener listener =
                createUrlRequestAndWaitForComplete(MOCK_CRONET_TEST_NOTFOUND_URL);
        assertEquals(MOCK_CRONET_TEST_NOTFOUND_URL, listener.mUrl);
        assertEquals(404, listener.mHttpStatusCode);
        assertEquals(
                "<!DOCTYPE html>\n<html>\n<head>\n<title>Not found</title>\n" +
                "<p>Test page loaded.</p>\n</head>\n</html>\n",
                new String(listener.mResponseAsBytes));
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testFailedURLRequest() throws Exception {
        TestHttpUrlRequestListener listener =
                createUrlRequestAndWaitForComplete(MOCK_CRONET_TEST_FAILED_URL);
        assertEquals(MOCK_CRONET_TEST_FAILED_URL, listener.mUrl);
        assertEquals(0, listener.mHttpStatusCode);
    }
}
