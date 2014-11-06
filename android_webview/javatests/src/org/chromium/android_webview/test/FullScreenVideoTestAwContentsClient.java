// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test;

import android.app.Activity;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.webkit.WebChromeClient;
import android.widget.FrameLayout;

import static org.chromium.base.test.util.ScalableTimeout.scaleTimeout;

import org.chromium.content.browser.test.util.CallbackHelper;

import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

/**
 * This class is a AwContentsClient for full screen video test.
 */
public class FullScreenVideoTestAwContentsClient extends TestAwContentsClient {
    public static final long WAITING_SECONDS = scaleTimeout(20);
    private CallbackHelper mOnShowCustomViewCallbackHelper = new CallbackHelper();
    private CallbackHelper mOnHideCustomViewCallbackHelper = new CallbackHelper();

    private final Activity mActivity;
    private final boolean mAllowHardwareAcceleration;
    private View mCustomView;
    private WebChromeClient.CustomViewCallback mExitCallback;

    public FullScreenVideoTestAwContentsClient(Activity activity,
            boolean allowHardwareAcceleration) {
        mActivity = activity;
        mAllowHardwareAcceleration = allowHardwareAcceleration;
    }

    @Override
    public void onShowCustomView(View view, WebChromeClient.CustomViewCallback callback) {
        mCustomView = view;
        if (!mAllowHardwareAcceleration) {
            // The hardware emulation in the testing infrastructure is not perfect, and this is
            // required to work-around some of the limitations.
            mCustomView.setLayerType(View.LAYER_TYPE_SOFTWARE, null);
        }
        mExitCallback = callback;
        mActivity.getWindow().setFlags(
                WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);

        mActivity.getWindow().addContentView(view,
                new FrameLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        Gravity.CENTER));
        mOnShowCustomViewCallbackHelper.notifyCalled();
    }

    @Override
    public void onHideCustomView() {
        mActivity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        mOnHideCustomViewCallbackHelper.notifyCalled();
    }

    public WebChromeClient.CustomViewCallback getExitCallback() {
        return mExitCallback;
    }

    public View getCustomView() {
        return mCustomView;
    }

    public boolean wasCustomViewShownCalled() {
        return mOnShowCustomViewCallbackHelper.getCallCount() > 0;
    }

    public void waitForCustomViewShown() throws TimeoutException, InterruptedException {
        mOnShowCustomViewCallbackHelper.waitForCallback(0, 1, WAITING_SECONDS, TimeUnit.SECONDS);
    }

    public void waitForCustomViewHidden() throws InterruptedException, TimeoutException {
        mOnHideCustomViewCallbackHelper.waitForCallback(0, 1, WAITING_SECONDS, TimeUnit.SECONDS);
    }
}
