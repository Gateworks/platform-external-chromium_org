// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.cronet_test_apk;

import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.test.util.Feature;
import org.chromium.cronet_test_apk.TestUrlRequestListener.FailureType;
import org.chromium.cronet_test_apk.TestUrlRequestListener.ResponseStep;
import org.chromium.net.ResponseInfo;
import org.chromium.net.UrlRequest;

import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;

/**
 * Test functionality of CronetUrlRequest.
 */
public class CronetUrlRequestTest extends CronetTestBase {
    // URL used for base tests.
    private static final String TEST_URL = "http://127.0.0.1:8000";
    private static final String MOCK_SUCCESS_PATH = "success.txt";

    private static final String MOCK_CRONET_TEST_SUCCESS_URL =
            "http://mock.http/success.txt";
    private static final String MOCK_CRONET_TEST_REDIRECT_URL =
            "http://mock.http/redirect.html";
    private static final String MOCK_CRONET_TEST_MULTI_REDIRECT_URL =
            "http://mock.http/multiredirect.html";
    private static final String MOCK_CRONET_TEST_NOTFOUND_URL =
            "http://mock.http/notfound.html";
    private static final String MOCK_CRONET_TEST_FAILED_URL =
            "http://mock.failed.request/-2";

    CronetTestActivity mActivity;

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        mActivity = launchCronetTestApp();
        // Make sure the activity was created as expected.
        waitForActiveShellToBeDoneLoading();
        assertTrue(UploadTestServer.startUploadTestServer());
        // AddUrlInterceptors() after native application context is initialized.
        MockUrlRequestJobUtil.addUrlInterceptors();
    }

    @Override
    protected void tearDown() throws Exception {
        mActivity.mUrlRequestContext.shutdown();
        UploadTestServer.shutdownUploadTestServer();
        super.tearDown();
    }

    private TestUrlRequestListener startAndWaitForComplete(String url)
            throws Exception {
        TestUrlRequestListener listener = new TestUrlRequestListener();
        // Create request.
        UrlRequest urlRequest = mActivity.mUrlRequestContext.createRequest(
                url, listener, listener.getExecutor());
        urlRequest.start();
        listener.blockForDone();
        return listener;
    }

    private void checkResponseInfo(ResponseInfo responseInfo,
            String expectedUrl, int expectedStatusHttpCode) {
        assertEquals(expectedUrl, responseInfo.getUrl());
        assertEquals(expectedUrl, responseInfo.getUrlChain()[
                responseInfo.getUrlChain().length - 1]);
        assertEquals(expectedStatusHttpCode, responseInfo.getHttpStatusCode());
        assertFalse(responseInfo.wasCached());
    }

    private void checkResponseInfoHeader(ResponseInfo responseInfo,
            String headerName, String headerValue) {
        Map<String, List<String>> responseHeaders =
                responseInfo.getAllHeaders();
        List<String> header = responseHeaders.get(headerName);
        assertNotNull(header);
        assertTrue(header.contains(headerValue));
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testSimpleGet() throws Exception {
        TestUrlRequestListener listener = startAndWaitForComplete(
                UploadTestServer.getEchoMethodURL());
        assertEquals(200, listener.mResponseInfo.getHttpStatusCode());
        // Default method is 'GET'.
        assertEquals("GET", listener.mResponseAsString);
        assertFalse(listener.mOnRedirectCalled);
        assertEquals(listener.mResponseStep, ResponseStep.ON_SUCCEEDED);
        checkResponseInfo(listener.mResponseInfo,
                UploadTestServer.getEchoMethodURL(), 200);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testSetHttpMethod() throws Exception {
        TestUrlRequestListener listener = new TestUrlRequestListener();
        String methodName = "HEAD";
        UrlRequest urlRequest = mActivity.mUrlRequestContext.createRequest(
                UploadTestServer.getEchoMethodURL(), listener,
                listener.getExecutor());
        // Try to set 'null' method.
        try {
            urlRequest.setHttpMethod(null);
            fail("Exception not thrown");
        } catch (NullPointerException e) {
            assertEquals("Method is required.", e.getMessage());
        }

        urlRequest.setHttpMethod(methodName);
        urlRequest.start();
        try {
            urlRequest.setHttpMethod("toolate");
            fail("Exception not thrown");
        } catch (IllegalStateException e) {
            assertEquals("Request is already started.", e.getMessage());
        }
        listener.blockForDone();
        assertEquals(200, listener.mResponseInfo.getHttpStatusCode());
        assertEquals(0, listener.mHttpResponseDataLength);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testBadMethod() throws Exception {
        TestUrlRequestListener listener = new TestUrlRequestListener();
        UrlRequest urlRequest = mActivity.mUrlRequestContext.createRequest(
                TEST_URL, listener, listener.getExecutor());
        try {
            urlRequest.setHttpMethod("bad:method!");
            urlRequest.start();
            fail("IllegalArgumentException not thrown.");
        } catch (IllegalArgumentException e) {
            assertEquals("Invalid http method bad:method!",
                    e.getMessage());
        }
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testBadHeaderName() throws Exception {
        TestUrlRequestListener listener = new TestUrlRequestListener();
        UrlRequest urlRequest = mActivity.mUrlRequestContext.createRequest(
                TEST_URL, listener, listener.getExecutor());
        try {
            urlRequest.addHeader("header:name", "headervalue");
            urlRequest.start();
            fail("IllegalArgumentException not thrown.");
        } catch (IllegalArgumentException e) {
            assertEquals("Invalid header header:name=headervalue",
                    e.getMessage());
        }
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testBadHeaderValue() throws Exception {
        TestUrlRequestListener listener = new TestUrlRequestListener();
        UrlRequest urlRequest = mActivity.mUrlRequestContext.createRequest(
                TEST_URL, listener, listener.getExecutor());
        try {
            urlRequest.addHeader("headername", "bad header\r\nvalue");
            urlRequest.start();
            fail("IllegalArgumentException not thrown.");
        } catch (IllegalArgumentException e) {
            assertEquals("Invalid header headername=bad header\r\nvalue",
                    e.getMessage());
        }
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testAddHeader() throws Exception {
        TestUrlRequestListener listener = new TestUrlRequestListener();
        String headerName = "header-name";
        String headerValue = "header-value";
        UrlRequest urlRequest = mActivity.mUrlRequestContext.createRequest(
                UploadTestServer.getEchoHeaderURL(headerName), listener,
                listener.getExecutor());

        urlRequest.addHeader(headerName, headerValue);
        urlRequest.start();
        try {
            urlRequest.addHeader("header2", "value");
            fail("Exception not thrown");
        } catch (IllegalStateException e) {
            assertEquals("Request is already started.", e.getMessage());
        }
        listener.blockForDone();
        assertEquals(200, listener.mResponseInfo.getHttpStatusCode());
        assertEquals(headerValue, listener.mResponseAsString);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testMultiRequestHeaders() throws Exception {
        TestUrlRequestListener listener = new TestUrlRequestListener();
        String headerName = "header-name";
        String headerValue1 = "header-value1";
        String headerValue2 = "header-value2";
        UrlRequest urlRequest = mActivity.mUrlRequestContext.createRequest(
                UploadTestServer.getEchoHeaderURL(headerName), listener,
                listener.getExecutor());
        urlRequest.addHeader(headerName, headerValue1);
        urlRequest.addHeader(headerName, headerValue2);
        urlRequest.start();
        listener.blockForDone();
        assertEquals(200, listener.mResponseInfo.getHttpStatusCode());
        // TODO(mef): Fix embedded test server to correctly return combination
        // of headerValue1 +  headerValue2.
        assertEquals(headerValue2, listener.mResponseAsString);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testCustomUserAgent() throws Exception {
        TestUrlRequestListener listener = new TestUrlRequestListener();
        String userAgentName = "User-Agent";
        String userAgentValue = "User-Agent-Value";
        UrlRequest urlRequest = mActivity.mUrlRequestContext.createRequest(
                UploadTestServer.getEchoHeaderURL(userAgentName), listener,
                listener.getExecutor());
        urlRequest.addHeader(userAgentName, userAgentValue);
        urlRequest.start();
        listener.blockForDone();
        assertEquals(200, listener.mResponseInfo.getHttpStatusCode());
        assertEquals(userAgentValue, listener.mResponseAsString);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testDefaultUserAgent() throws Exception {
        TestUrlRequestListener listener = new TestUrlRequestListener();
        String headerName = "User-Agent";
        UrlRequest urlRequest = mActivity.mUrlRequestContext.createRequest(
                UploadTestServer.getEchoHeaderURL(headerName), listener,
                listener.getExecutor());
        urlRequest.start();
        listener.blockForDone();
        assertEquals(200, listener.mResponseInfo.getHttpStatusCode());
        assertTrue("Default User-Agent should contain Cronet/n.n.n.n but is "
                           + listener.mResponseAsString,
                   Pattern.matches(".+Cronet/\\d+\\.\\d+\\.\\d+\\.\\d+.+",
                           listener.mResponseAsString));
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testMockSuccess() throws Exception {
        TestUrlRequestListener listener = startAndWaitForComplete(
                MOCK_CRONET_TEST_SUCCESS_URL);
        assertEquals(200, listener.mResponseInfo.getHttpStatusCode());
        assertEquals(0, listener.mRedirectResponseInfoList.size());
        assertTrue(listener.mHttpResponseDataLength != 0);
        assertEquals(listener.mResponseStep, ResponseStep.ON_SUCCEEDED);
        Map<String, List<String>> responseHeaders =
                listener.mResponseInfo.getAllHeaders();
        assertEquals("header-value", responseHeaders.get("header-name").get(0));
        List<String> multiHeader = responseHeaders.get("multi-header-name");
        assertEquals(2, multiHeader.size());
        assertEquals("header-value1", multiHeader.get(0));
        assertEquals("header-value2", multiHeader.get(1));
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testMockRedirect() throws Exception {
        TestUrlRequestListener listener = startAndWaitForComplete(
                MOCK_CRONET_TEST_REDIRECT_URL);
        ResponseInfo mResponseInfo = listener.mResponseInfo;
        assertTrue(listener.mOnRedirectCalled);
        assertEquals(200, mResponseInfo.getHttpStatusCode());
        assertEquals(1, listener.mRedirectResponseInfoList.size());
        assertEquals(MOCK_CRONET_TEST_SUCCESS_URL,
                mResponseInfo.getUrl());
        assertEquals(2, mResponseInfo.getUrlChain().length);
        assertEquals(MOCK_CRONET_TEST_REDIRECT_URL,
                mResponseInfo.getUrlChain()[0]);
        assertEquals(MOCK_CRONET_TEST_SUCCESS_URL,
                mResponseInfo.getUrlChain()[1]);
        checkResponseInfo(listener.mRedirectResponseInfoList.get(0),
                MOCK_CRONET_TEST_REDIRECT_URL, 302);
        checkResponseInfoHeader(listener.mRedirectResponseInfoList.get(0),
                "redirect-header", "header-value");
        assertTrue(listener.mHttpResponseDataLength != 0);
        assertTrue(listener.mOnRedirectCalled);
        assertEquals(listener.mResponseStep, ResponseStep.ON_SUCCEEDED);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testMockMultiRedirect() throws Exception {
        TestUrlRequestListener listener = startAndWaitForComplete(
                MOCK_CRONET_TEST_MULTI_REDIRECT_URL);
        ResponseInfo mResponseInfo = listener.mResponseInfo;
        assertTrue(listener.mOnRedirectCalled);
        assertEquals(200, mResponseInfo.getHttpStatusCode());
        assertEquals(2, listener.mRedirectResponseInfoList.size());

        // Check first redirect (multiredirect.html -> redirect.html)
        ResponseInfo firstRedirectResponseInfo =
                listener.mRedirectResponseInfoList.get(0);
        assertEquals(1, firstRedirectResponseInfo.getUrlChain().length);
        assertEquals(MOCK_CRONET_TEST_MULTI_REDIRECT_URL,
                firstRedirectResponseInfo.getUrlChain()[0]);
        checkResponseInfo(firstRedirectResponseInfo,
                MOCK_CRONET_TEST_MULTI_REDIRECT_URL, 302);
        checkResponseInfoHeader(firstRedirectResponseInfo,
                "redirect-header0", "header-value");

        // Check second redirect (redirect.html -> success.txt)
        ResponseInfo secondRedirectResponseInfo =
                listener.mRedirectResponseInfoList.get(1);
        assertEquals(2, secondRedirectResponseInfo.getUrlChain().length);
        assertEquals(MOCK_CRONET_TEST_MULTI_REDIRECT_URL,
                secondRedirectResponseInfo.getUrlChain()[0]);
        assertEquals(MOCK_CRONET_TEST_REDIRECT_URL,
                secondRedirectResponseInfo.getUrlChain()[1]);
        checkResponseInfo(secondRedirectResponseInfo,
                MOCK_CRONET_TEST_REDIRECT_URL, 302);
        checkResponseInfoHeader(secondRedirectResponseInfo,
                "redirect-header", "header-value");

        // Check final response (success.txt).
        assertEquals(MOCK_CRONET_TEST_SUCCESS_URL,
                mResponseInfo.getUrl());
        assertEquals(3, mResponseInfo.getUrlChain().length);
        assertEquals(MOCK_CRONET_TEST_MULTI_REDIRECT_URL,
                mResponseInfo.getUrlChain()[0]);
        assertEquals(MOCK_CRONET_TEST_REDIRECT_URL,
                mResponseInfo.getUrlChain()[1]);
        assertEquals(MOCK_CRONET_TEST_SUCCESS_URL,
                mResponseInfo.getUrlChain()[2]);
        assertTrue(listener.mHttpResponseDataLength != 0);
        assertTrue(listener.mOnRedirectCalled);
        assertEquals(listener.mResponseStep, ResponseStep.ON_SUCCEEDED);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testMockNotFound() throws Exception {
        TestUrlRequestListener listener = startAndWaitForComplete(
                MOCK_CRONET_TEST_NOTFOUND_URL);
        assertEquals(404, listener.mResponseInfo.getHttpStatusCode());
        assertTrue(listener.mHttpResponseDataLength != 0);
        assertFalse(listener.mOnRedirectCalled);
        assertFalse(listener.mOnErrorCalled);
        assertEquals(listener.mResponseStep, ResponseStep.ON_SUCCEEDED);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testMockStartAsyncError() throws Exception {
        final int arbitraryNetError = -3;
        TestUrlRequestListener listener = startAndWaitForComplete(
                MockUrlRequestJobUtil.getMockUrlWithFailure(
                        MOCK_SUCCESS_PATH,
                        MockUrlRequestJobUtil.FailurePhase.START,
                        arbitraryNetError));
        assertNull(listener.mResponseInfo);
        assertNotNull(listener.mError);
        assertEquals(arbitraryNetError, listener.mError.netError());
        assertFalse(listener.mOnRedirectCalled);
        assertTrue(listener.mOnErrorCalled);
        assertEquals(listener.mResponseStep, ResponseStep.NOTHING);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testMockReadDataSyncError() throws Exception {
        final int arbitraryNetError = -4;
        TestUrlRequestListener listener = startAndWaitForComplete(
                MockUrlRequestJobUtil.getMockUrlWithFailure(
                        MOCK_SUCCESS_PATH,
                        MockUrlRequestJobUtil.FailurePhase.READ_SYNC,
                        arbitraryNetError));
        assertEquals(200, listener.mResponseInfo.getHttpStatusCode());
        assertNotNull(listener.mError);
        assertEquals(arbitraryNetError, listener.mError.netError());
        assertFalse(listener.mOnRedirectCalled);
        assertTrue(listener.mOnErrorCalled);
        assertEquals(listener.mResponseStep, ResponseStep.ON_RESPONSE_STARTED);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testMockReadDataAsyncError() throws Exception {
        final int arbitraryNetError = -5;
        TestUrlRequestListener listener = startAndWaitForComplete(
                MockUrlRequestJobUtil.getMockUrlWithFailure(
                        MOCK_SUCCESS_PATH,
                        MockUrlRequestJobUtil.FailurePhase.READ_ASYNC,
                        arbitraryNetError));
        assertEquals(200, listener.mResponseInfo.getHttpStatusCode());
        assertNotNull(listener.mError);
        assertEquals(arbitraryNetError, listener.mError.netError());
        assertFalse(listener.mOnRedirectCalled);
        assertTrue(listener.mOnErrorCalled);
        assertEquals(listener.mResponseStep, ResponseStep.ON_RESPONSE_STARTED);
    }

    private void throwOrCancel(FailureType failureType, ResponseStep failureStep,
            boolean expectResponseInfo, boolean expectError) {
        TestUrlRequestListener listener = new TestUrlRequestListener();
        listener.setFailure(failureType, failureStep);
        UrlRequest urlRequest = mActivity.mUrlRequestContext.createRequest(
                MOCK_CRONET_TEST_REDIRECT_URL, listener, listener.getExecutor());
        urlRequest.start();
        listener.blockForDone();
        assertTrue(listener.mOnRedirectCalled);
        assertEquals(listener.mResponseStep, failureStep);
        assertTrue(urlRequest.isCanceled());
        assertEquals(expectResponseInfo, listener.mResponseInfo != null);
        assertEquals(expectError, listener.mError != null);
        assertEquals(expectError, listener.mOnErrorCalled);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testFailures() throws Exception {
        throwOrCancel(FailureType.CANCEL_SYNC, ResponseStep.ON_REDIRECT,
                false, false);
        throwOrCancel(FailureType.CANCEL_ASYNC, ResponseStep.ON_REDIRECT,
                false, false);
        throwOrCancel(FailureType.THROW_SYNC, ResponseStep.ON_REDIRECT,
                false, true);

        throwOrCancel(FailureType.CANCEL_SYNC, ResponseStep.ON_RESPONSE_STARTED,
                true, false);
        throwOrCancel(FailureType.CANCEL_ASYNC, ResponseStep.ON_RESPONSE_STARTED,
                true, false);
        throwOrCancel(FailureType.THROW_SYNC, ResponseStep.ON_RESPONSE_STARTED,
                true, true);
        throwOrCancel(FailureType.CANCEL_SYNC, ResponseStep.ON_DATA_RECEIVED,
                true, false);
        throwOrCancel(FailureType.CANCEL_ASYNC, ResponseStep.ON_DATA_RECEIVED,
                true, false);
        throwOrCancel(FailureType.THROW_SYNC, ResponseStep.ON_DATA_RECEIVED,
                true, true);
    }

    @SmallTest
    @Feature({"Cronet"})
    public void testThrowON_SUCCEEDED() {
        TestUrlRequestListener listener = new TestUrlRequestListener();
        listener.setFailure(FailureType.THROW_SYNC, ResponseStep.ON_SUCCEEDED);
        UrlRequest urlRequest = mActivity.mUrlRequestContext.createRequest(
                MOCK_CRONET_TEST_REDIRECT_URL, listener, listener.getExecutor());
        urlRequest.start();
        listener.blockForDone();
        assertTrue(listener.mOnRedirectCalled);
        assertEquals(listener.mResponseStep, ResponseStep.ON_SUCCEEDED);
        assertFalse(urlRequest.isCanceled());
        assertNotNull(listener.mResponseInfo);
        assertNull(listener.mError);
        assertFalse(listener.mOnErrorCalled);
    }
}
