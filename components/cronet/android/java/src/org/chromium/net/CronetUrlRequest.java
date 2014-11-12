// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.util.Log;

import org.chromium.base.CalledByNative;
import org.chromium.base.JNINamespace;

import java.nio.ByteBuffer;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;

/**
 * UrlRequest using Chromium HTTP stack implementation. Could be accessed from
 * any thread on Executor. Cancel can be done from any thread.
 * All @CallByNative methods are called on native network thread
 * and post tasks with listener calls onto Executor. Upon return from listener
 * callback native request adapter is called on executive thread and posts
 * native tasks to native network thread. Because Cancel could be called from
 * any thread it is protected by mUrlRequestAdapterLock.
 */
@JNINamespace("cronet")
final class CronetUrlRequest implements UrlRequest {
    /* Native adapter object, owned by UrlRequest. */
    private long mUrlRequestAdapter;
    private boolean mStarted = false;
    private boolean mCanceled = false;
    private boolean mInOnDataReceived = false;

    /*
     * Synchronize access to mUrlRequestAdapter, mStarted, mCanceled and
     * mDestroyAfterReading.
     */
    private final Object mUrlRequestAdapterLock = new Object();
    private final CronetUrlRequestContext mRequestContext;
    private final Executor mExecutor;

    /*
     * Url chain contans the URL currently being requested, and
     * all URLs previously requested.  New URLs are only added after it is
     * decided a redirect will be followed.
     */
    private final List<String> mUrlChain = new ArrayList<String>();

    private final UrlRequestListener mListener;
    private final String mInitialUrl;
    private final int mPriority;
    private String mInitialMethod;
    private final HeadersList mRequestHeaders = new HeadersList();

    private NativeResponseInfo mResponseInfo;

    /*
     * Listener callback is repeatedly called when data is received, so it is
     * cached as member variable.
     */
    private OnDataReceivedRunnable mOnDataReceivedTask;

    static final class HeaderEntry extends
            AbstractMap.SimpleEntry<String, String> {
        public HeaderEntry(String name, String value) {
            super(name, value);
        }
    }

    static final class HeadersList extends ArrayList<HeaderEntry> {
    }

    final class OnDataReceivedRunnable implements Runnable {
        ByteBuffer mByteBuffer;

        public void run() {
            if (isCanceled()) {
                return;
            }
            try {
                synchronized (mUrlRequestAdapterLock) {
                    if (mUrlRequestAdapter == 0) {
                        return;
                    }
                    // mByteBuffer is direct buffer backed by native memory,
                    // and passed to listener, so the request adapter cannot
                    // be destroyed while listener has access to it.
                    // Set |mInOnDataReceived| flag during the call to listener
                    // and destroy adapter immediately after if request was
                    // cancelled during the call from listener or other thread.
                    mInOnDataReceived = true;
                }
                mListener.onDataReceived(CronetUrlRequest.this,
                        mResponseInfo, mByteBuffer);
                mByteBuffer = null;
                synchronized (mUrlRequestAdapterLock) {
                    mInOnDataReceived = false;
                    if (isCanceled()) {
                        destroyRequestAdapter();
                        return;
                    }
                    nativeReceiveData(mUrlRequestAdapter);
                }
            } catch (Exception e) {
                synchronized (mUrlRequestAdapterLock) {
                    mInOnDataReceived = false;
                    if (isCanceled()) {
                        destroyRequestAdapter();
                    }
                }
                onListenerException(e);
            }
        }
    }

    static final class NativeResponseInfo implements ResponseInfo {
        private final String[] mResponseInfoUrlChain;
        private final int mHttpStatusCode;
        private final HeadersMap mAllHeaders = new HeadersMap();
        private final boolean mWasCached;
        private final String mNegotiatedProtocol;

        NativeResponseInfo(String[] urlChain, int httpStatusCode,
                boolean wasCached, String negotiatedProtocol) {
            mResponseInfoUrlChain = urlChain;
            mHttpStatusCode = httpStatusCode;
            mWasCached = wasCached;
            mNegotiatedProtocol = negotiatedProtocol;
        }

        @Override
        public String getUrl() {
            return mResponseInfoUrlChain[mResponseInfoUrlChain.length - 1];
        }

        @Override
        public String[] getUrlChain() {
            return mResponseInfoUrlChain;
        }

        @Override
        public int getHttpStatusCode() {
            return mHttpStatusCode;
        }

        @Override
        public Map<String, List<String>> getAllHeaders() {
            return mAllHeaders;
        }

        @Override
        public boolean wasCached() {
            return mWasCached;
        }

        @Override
        public String getNegotiatedProtocol() {
            return mNegotiatedProtocol;
        }
    };

    static final class NativeExtendedResponseInfo implements
            ExtendedResponseInfo {
        private final ResponseInfo mResponseInfo;
        private final long mTotalReceivedBytes;

        NativeExtendedResponseInfo(ResponseInfo responseInfo,
                                   long totalReceivedBytes) {
            mResponseInfo = responseInfo;
            mTotalReceivedBytes = totalReceivedBytes;
        }

        @Override
        public ResponseInfo getResponseInfo() {
            return mResponseInfo;
        }

        @Override
        public long getTotalReceivedBytes() {
            return mTotalReceivedBytes;
        }
    };

    CronetUrlRequest(CronetUrlRequestContext requestContext,
            long urlRequestContextAdapter,
            String url,
            int priority,
            UrlRequestListener listener,
            Executor executor) {
        if (url == null) {
            throw new NullPointerException("URL is required");
        }
        if (listener == null) {
            throw new NullPointerException("Listener is required");
        }
        if (executor == null) {
            throw new NullPointerException("Executor is required");
        }

        mRequestContext = requestContext;
        mInitialUrl = url;
        mUrlChain.add(url);
        mPriority = convertRequestPriority(priority);
        mListener = listener;
        mExecutor = executor;
    }

    @Override
    public void setHttpMethod(String method) {
        checkNotStarted();
        if (method == null) {
            throw new NullPointerException("Method is required.");
        }
        mInitialMethod = method;
    }

    @Override
    public void addHeader(String header, String value) {
        checkNotStarted();
        if (header == null) {
            throw new NullPointerException("Invalid header name.");
        }
        if (value == null) {
            throw new NullPointerException("Invalid header value.");
        }
        mRequestHeaders.add(new HeaderEntry(header, value));
    }

    @Override
    public void start() {
        synchronized (mUrlRequestAdapterLock) {
            checkNotStarted();
            mUrlRequestAdapter = nativeCreateRequestAdapter(
                    mRequestContext.getUrlRequestContextAdapter(),
                    mInitialUrl,
                    mPriority);
            mRequestContext.onRequestStarted(this);
            if (mInitialMethod != null) {
                if (!nativeSetHttpMethod(mUrlRequestAdapter, mInitialMethod)) {
                    destroyRequestAdapter();
                    throw new IllegalArgumentException("Invalid http method "
                            + mInitialMethod);
                }
            }
            for (HeaderEntry header : mRequestHeaders) {
                if (!nativeAddHeader(mUrlRequestAdapter, header.getKey(),
                        header.getValue())) {
                    destroyRequestAdapter();
                    throw new IllegalArgumentException("Invalid header "
                            + header.getKey() + "=" + header.getValue());
                }
            }
            mStarted = true;
            nativeStart(mUrlRequestAdapter);
        }
    }

    @Override
    public void cancel() {
        synchronized (mUrlRequestAdapterLock) {
            if (mCanceled || !mStarted) {
                return;
            }
            mCanceled = true;
            // During call into listener OnDataReceived adapter cannot be
            // destroyed as it owns the byte buffer.
            if (!mInOnDataReceived) {
                destroyRequestAdapter();
            }
        }
    }

    @Override
    public boolean isCanceled() {
        synchronized (mUrlRequestAdapterLock) {
            return mCanceled;
        }
    }

    @Override
    public void pause() {
        throw new UnsupportedOperationException("Not implemented yet");
    }

    @Override
    public boolean isPaused() {
        return false;
    }

    @Override
    public void resume() {
        throw new UnsupportedOperationException("Not implemented yet");
    }

    /**
     * Post task to application Executor. Used for Listener callbacks
     * and other tasks that should not be executed on network thread.
     */
    private void postTaskToExecutor(Runnable task) {
        mExecutor.execute(task);
    }

    private static int convertRequestPriority(int priority) {
        switch (priority) {
            case REQUEST_PRIORITY_IDLE:
                return RequestPriority.IDLE;
            case REQUEST_PRIORITY_LOWEST:
                return RequestPriority.LOWEST;
            case REQUEST_PRIORITY_LOW:
                return RequestPriority.LOW;
            case REQUEST_PRIORITY_MEDIUM:
                return RequestPriority.MEDIUM;
            case REQUEST_PRIORITY_HIGHEST:
                return RequestPriority.HIGHEST;
            default:
                return RequestPriority.MEDIUM;
        }
    }

    private NativeResponseInfo prepareResponseInfoOnNetworkThread(
            int httpStatusCode) {
        long urlRequestAdapter;
        synchronized (mUrlRequestAdapterLock) {
            if (mUrlRequestAdapter == 0) {
                return null;
            }
            // This method is running on network thread, so even if
            // mUrlRequestAdapter is set to 0 from another thread the actual
            // deletion of the adapter is posted to network thread, so it is
            // safe to preserve and use urlRequestAdapter outside the lock.
            urlRequestAdapter = mUrlRequestAdapter;
        }
        NativeResponseInfo responseInfo = new NativeResponseInfo(
                mUrlChain.toArray(new String[mUrlChain.size()]),
                httpStatusCode,
                nativeGetWasCached(urlRequestAdapter),
                nativeGetNegotiatedProtocol(urlRequestAdapter));
        nativePopulateResponseHeaders(urlRequestAdapter,
                                      responseInfo.mAllHeaders);
        return responseInfo;
    }

    private void checkNotStarted() {
        synchronized (mUrlRequestAdapterLock) {
            if (mStarted || isCanceled()) {
                throw new IllegalStateException("Request is already started.");
            }
        }
    }

    private void destroyRequestAdapter() {
        synchronized (mUrlRequestAdapterLock) {
            if (mUrlRequestAdapter == 0) {
                return;
            }
            nativeDestroyRequestAdapter(mUrlRequestAdapter);
            mRequestContext.onRequestDestroyed(this);
            mUrlRequestAdapter = 0;
        }
    }

    /**
     * If listener method throws an exception, request gets canceled
     * and exception is reported via onFailed listener callback.
     * Only called on the Executor.
     */
    private void onListenerException(Exception e) {
        UrlRequestException requestError = new UrlRequestException(
                "CalledByNative method has thrown an exception", e);
        Log.e(CronetUrlRequestContext.LOG_TAG,
                "Exception in CalledByNative method", e);
        // Do not call into listener if request is canceled.
        if (isCanceled()) {
            return;
        }
        try {
            cancel();
            mListener.onFailed(this, mResponseInfo, requestError);
        } catch (Exception cancelException) {
            Log.e(CronetUrlRequestContext.LOG_TAG,
                    "Exception trying to cancel request", cancelException);
        }
    }

    ////////////////////////////////////////////////
    // Private methods called by the native code.
    // Always called on network thread.
    ////////////////////////////////////////////////

    /**
     * Called before following redirects. The redirect will automatically be
     * followed, unless the request is paused or canceled during this
     * callback. If the redirect response has a body, it will be ignored.
     * This will only be called between start and onResponseStarted.
     *
     * @param newLocation Location where request is redirected.
     * @param httpStatusCode from redirect response
     */
    @SuppressWarnings("unused")
    @CalledByNative
    private void onRedirect(final String newLocation, int httpStatusCode) {
        final NativeResponseInfo responseInfo =
                prepareResponseInfoOnNetworkThread(httpStatusCode);
        Runnable task = new Runnable() {
            public void run() {
                if (isCanceled()) {
                    return;
                }
                try {
                    mListener.onRedirect(CronetUrlRequest.this, responseInfo,
                            newLocation);
                    synchronized (mUrlRequestAdapterLock) {
                        if (isCanceled()) {
                            return;
                        }
                        // It is Ok to access mUrlChain not on the network
                        // thread as the request is waiting to follow redirect.
                        mUrlChain.add(newLocation);
                        nativeFollowDeferredRedirect(mUrlRequestAdapter);
                    }
                } catch (Exception e) {
                    onListenerException(e);
                }
            }
        };
        postTaskToExecutor(task);
    }

    /**
     * Called when the final set of headers, after all redirects,
     * is received. Can only be called once for each request.
     */
    @SuppressWarnings("unused")
    @CalledByNative
    private void onResponseStarted(int httpStatusCode) {
        mResponseInfo = prepareResponseInfoOnNetworkThread(httpStatusCode);
        Runnable task = new Runnable() {
            public void run() {
                if (isCanceled()) {
                    return;
                }
                try {
                    mListener.onResponseStarted(CronetUrlRequest.this,
                                                mResponseInfo);
                    synchronized (mUrlRequestAdapterLock) {
                        if (isCanceled()) {
                            return;
                        }
                        nativeReceiveData(mUrlRequestAdapter);
                    }
                } catch (Exception e) {
                    onListenerException(e);
                }
            }
        };
        postTaskToExecutor(task);
    }

    /**
     * Called whenever data is received. The ByteBuffer remains
     * valid only until listener callback. Or if the callback
     * pauses the request, it remains valid until the request is resumed.
     * Cancelling the request also invalidates the buffer.
     *
     * @param byteBuffer Received data.
     */
    @SuppressWarnings("unused")
    @CalledByNative
    private void onDataReceived(final ByteBuffer byteBuffer) {
        if (mOnDataReceivedTask == null) {
            mOnDataReceivedTask = new OnDataReceivedRunnable();
        }
        mOnDataReceivedTask.mByteBuffer = byteBuffer;
        postTaskToExecutor(mOnDataReceivedTask);
    }

    /**
     * Called when request is completed successfully, no callbacks will be
     * called afterwards.
     */
    @SuppressWarnings("unused")
    @CalledByNative
    private void onSucceeded() {
        long totalReceivedBytes;
        synchronized (mUrlRequestAdapterLock) {
            if (mUrlRequestAdapter == 0) {
                return;
            }
            totalReceivedBytes =
                    nativeGetTotalReceivedBytes(mUrlRequestAdapter);
        }

        final NativeExtendedResponseInfo extendedResponseInfo =
                new NativeExtendedResponseInfo(mResponseInfo,
                        totalReceivedBytes);
        Runnable task = new Runnable() {
            public void run() {
                synchronized (mUrlRequestAdapterLock) {
                    if (isCanceled()) {
                        return;
                    }
                    // Destroy adapter first, so request context could be shut
                    // down from the listener.
                    destroyRequestAdapter();
                }
                try {
                    mListener.onSucceeded(CronetUrlRequest.this,
                                          extendedResponseInfo);
                } catch (Exception e) {
                    Log.e(CronetUrlRequestContext.LOG_TAG,
                            "Exception in onComplete method", e);
                }
            }
        };
        postTaskToExecutor(task);
    }

    /**
     * Called when error has occured, no callbacks will be called afterwards.
     *
     * @param nativeError native net error code.
     * @param errorString textual representation of the error code.
     */
    @SuppressWarnings("unused")
    @CalledByNative
    private void onError(final int nativeError, final String errorString) {
        Runnable task = new Runnable() {
            public void run() {
                if (isCanceled()) {
                    return;
                }
                // Destroy adapter first, so request context could be shut down
                // from the listener.
                destroyRequestAdapter();
                try {
                    UrlRequestException requestError = new UrlRequestException(
                            "Exception in CronetUrlRequest: " + errorString,
                            nativeError);
                    mListener.onFailed(CronetUrlRequest.this,
                                       mResponseInfo,
                                       requestError);
                } catch (Exception e) {
                    Log.e(CronetUrlRequestContext.LOG_TAG,
                            "Exception in onError method", e);
                }
            }
        };
        postTaskToExecutor(task);
    }

    /**
     * Appends header |name| with value |value| to |headersMap|.
     */
    @SuppressWarnings("unused")
    @CalledByNative
    private void onAppendResponseHeader(HeadersMap headersMap,
            String name, String value) {
        try {
            if (!headersMap.containsKey(name)) {
                headersMap.put(name, new ArrayList<String>());
            }
            headersMap.get(name).add(value);
        } catch (final Exception e) {
            Log.e(CronetUrlRequestContext.LOG_TAG,
                    "Exception in onAppendResponseHeader method", e);
        }
    }

    // Native methods are implemented in cronet_url_request.cc.

    private native long nativeCreateRequestAdapter(
            long urlRequestContextAdapter, String url, int priority);

    private native boolean nativeAddHeader(long urlRequestAdapter, String name,
            String value);

    private native boolean nativeSetHttpMethod(long urlRequestAdapter,
            String method);

    private native void nativeStart(long urlRequestAdapter);

    private native void nativeDestroyRequestAdapter(long urlRequestAdapter);

    private native void nativeFollowDeferredRedirect(long urlRequestAdapter);

    private native void nativeReceiveData(long urlRequestAdapter);

    private native void nativePopulateResponseHeaders(long urlRequestAdapter,
            HeadersMap headers);

    private native String nativeGetNegotiatedProtocol(long urlRequestAdapter);

    private native boolean nativeGetWasCached(long urlRequestAdapter);

    private native long nativeGetTotalReceivedBytes(long urlRequestAdapter);

    // Explicit class to work around JNI-generator generics confusion.
    private static class HeadersMap extends HashMap<String, List<String>> {
    }
}
