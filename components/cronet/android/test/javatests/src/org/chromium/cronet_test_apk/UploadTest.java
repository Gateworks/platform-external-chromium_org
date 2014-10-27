// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.cronet_test_apk;

import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.test.util.Feature;
import org.chromium.net.HttpUrlRequest;
import org.chromium.net.HttpUrlRequestListener;

import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.nio.channels.Channels;
import java.nio.channels.ReadableByteChannel;
import java.util.HashMap;

/**
 * Test fixture to test upload APIs.  Uses an in-process test server.
 */
public class UploadTest extends CronetTestBase {
    private static final String UPLOAD_DATA = "Nifty upload data!";
    private static final String UPLOAD_CHANNEL_DATA = "Upload channel data";

    private CronetTestActivity mActivity;

    // @Override
    protected void setUp() throws Exception {
        super.setUp();
        mActivity = launchCronetTestApp();
        assertNotNull(mActivity);
        assertTrue(UploadTestServer.startUploadTestServer());
    }

    private HttpUrlRequest createRequest(
            String url, HttpUrlRequestListener listener) {
        HashMap<String, String> headers = new HashMap<String, String>();
        return mActivity.mRequestFactory.createRequest(
                url, HttpUrlRequest.REQUEST_PRIORITY_MEDIUM, headers, listener);
    }

    /**
     * Sets request to have an upload channel containing the given data.
     * uploadDataLength should generally be uploadData.length(), unless a test
     * needs to get a read error.
     */
    private void setUploadChannel(HttpUrlRequest request,
                                  String contentType,
                                  String uploadData,
                                  int uploadDataLength) {
        InputStream uploadDataStream = new ByteArrayInputStream(
                uploadData.getBytes());
        ReadableByteChannel uploadDataChannel =
                Channels.newChannel(uploadDataStream);
        request.setUploadChannel(
                contentType, uploadDataChannel, uploadDataLength);
    }

    /**
     * Tests uploading an in-memory string.
     */
    @SmallTest
    @Feature({"Cronet"})
    public void testUploadData() throws Exception {
        TestHttpUrlRequestListener listener = new TestHttpUrlRequestListener();
        HttpUrlRequest request = createRequest(
                UploadTestServer.getEchoBodyURL(), listener);
        request.setUploadData("text/plain", UPLOAD_DATA.getBytes("UTF8"));
        request.start();
        listener.blockForComplete();

        assertEquals(200, listener.mHttpStatusCode);
        assertEquals(UPLOAD_DATA, listener.mResponseAsString);
    }

    /**
     * Tests uploading an in-memory string with a redirect that preserves the
     * POST body.  This makes sure the body is correctly sent again.
     */
    @SmallTest
    @Feature({"Cronet"})
    public void testUploadDataWithRedirect() throws Exception {
        TestHttpUrlRequestListener listener = new TestHttpUrlRequestListener();
        HttpUrlRequest request = createRequest(
                UploadTestServer.getRedirectToEchoBody(), listener);
        request.setUploadData("text/plain", UPLOAD_DATA.getBytes("UTF8"));
        request.start();
        listener.blockForComplete();

        assertEquals(200, listener.mHttpStatusCode);
        assertEquals(UPLOAD_DATA, listener.mResponseAsString);
    }

    /**
     * Tests Content-Type can be set when uploading an in-memory string.
     */
    @SmallTest
    @Feature({"Cronet"})
    public void testUploadDataContentType() throws Exception {
        String contentTypes[] = {"text/plain", "chicken/spicy"};
        for (String contentType : contentTypes) {
            TestHttpUrlRequestListener listener =
                    new TestHttpUrlRequestListener();
            HttpUrlRequest request = createRequest(
                    UploadTestServer.getEchoHeaderURL("Content-Type"),
                    listener);
            request.setUploadData(contentType, UPLOAD_DATA.getBytes("UTF8"));
            request.start();
            listener.blockForComplete();

            assertEquals(200, listener.mHttpStatusCode);
            assertEquals(contentType, listener.mResponseAsString);
        }
    }

    /**
     * Tests the default method when uploading.
     */
    @SmallTest
    @Feature({"Cronet"})
    public void testDefaultUploadMethod() throws Exception {
        TestHttpUrlRequestListener listener = new TestHttpUrlRequestListener();
        HttpUrlRequest request = createRequest(
                UploadTestServer.getEchoMethodURL(), listener);
        request.setUploadData("text/plain", UPLOAD_DATA.getBytes("UTF8"));
        request.start();
        listener.blockForComplete();

        assertEquals(200, listener.mHttpStatusCode);
        assertEquals("POST", listener.mResponseAsString);
    }

    /**
     * Tests methods can be set when uploading.
     */
    @SmallTest
    @Feature({"Cronet"})
    public void testUploadMethods() throws Exception {
        String uploadMethods[] = {"POST", "PUT"};
        for (String uploadMethod : uploadMethods) {
            TestHttpUrlRequestListener listener =
                    new TestHttpUrlRequestListener();
            HttpUrlRequest request = createRequest(
                    UploadTestServer.getEchoMethodURL(), listener);
            request.setHttpMethod(uploadMethod);
            request.setUploadData("text/plain", UPLOAD_DATA.getBytes("UTF8"));
            request.start();
            listener.blockForComplete();

            assertEquals(200, listener.mHttpStatusCode);
            assertEquals(uploadMethod, listener.mResponseAsString);
        }
    }

    /**
     * Tests uploading from a channel.
     */
    @SmallTest
    @Feature({"Cronet"})
    public void testUploadChannel() throws Exception {
        TestHttpUrlRequestListener listener = new TestHttpUrlRequestListener();
        HttpUrlRequest request = createRequest(
                UploadTestServer.getEchoBodyURL(), listener);
        setUploadChannel(request, "text/plain", UPLOAD_CHANNEL_DATA,
                         UPLOAD_CHANNEL_DATA.length());
        request.start();
        listener.blockForComplete();

        assertEquals(200, listener.mHttpStatusCode);
        assertEquals(UPLOAD_CHANNEL_DATA, listener.mResponseAsString);
    }

    /**
     * Tests uploading from a channel in the case a redirect preserves the post
     * body.  Since channels can't be rewound, the request fails when we try to
     * rewind it to send the second request.
     */
    @SmallTest
    @Feature({"Cronet"})
    public void testUploadChannelWithRedirect() throws Exception {
        TestHttpUrlRequestListener listener = new TestHttpUrlRequestListener();
        HttpUrlRequest request = createRequest(
                UploadTestServer.getRedirectToEchoBody(), listener);
        setUploadChannel(request, "text/plain", UPLOAD_CHANNEL_DATA,
                         UPLOAD_CHANNEL_DATA.length());
        request.start();
        listener.blockForComplete();

        assertEquals(0, listener.mHttpStatusCode);
        assertEquals(
                "System error: net::ERR_UPLOAD_STREAM_REWIND_NOT_SUPPORTED(-25)",
                listener.mException.getMessage());
    }

    /**
     * Tests uploading from a channel when there's a read error.  The body
     * should be 0-padded.
     */
    @SmallTest
    @Feature({"Cronet"})
    public void testUploadChannelWithReadError() throws Exception {
        TestHttpUrlRequestListener listener = new TestHttpUrlRequestListener();
        HttpUrlRequest request = createRequest(
                UploadTestServer.getEchoBodyURL(), listener);
        setUploadChannel(request, "text/plain", UPLOAD_CHANNEL_DATA,
                         UPLOAD_CHANNEL_DATA.length() + 2);
        request.start();
        listener.blockForComplete();

        assertEquals(200, listener.mHttpStatusCode);
        assertEquals(UPLOAD_CHANNEL_DATA + "\0\0", listener.mResponseAsString);
    }

    /**
     * Tests Content-Type can be set when uploading from a channel.
     */
    @SmallTest
    @Feature({"Cronet"})
    public void testUploadChannelContentType() throws Exception {
        String contentTypes[] = {"text/plain", "chicken/spicy"};
        for (String contentType : contentTypes) {
            TestHttpUrlRequestListener listener =
                    new TestHttpUrlRequestListener();
            HttpUrlRequest request = createRequest(
                    UploadTestServer.getEchoHeaderURL("Content-Type"),
                                                      listener);
            setUploadChannel(request, contentType, UPLOAD_CHANNEL_DATA,
                             UPLOAD_CHANNEL_DATA.length());
            request.start();
            listener.blockForComplete();

            assertEquals(200, listener.mHttpStatusCode);
            assertEquals(contentType, listener.mResponseAsString);
        }
    }
}
