// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.style.ForegroundColorSpan;
import android.text.style.StrikethroughSpan;
import android.text.style.StyleSpan;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.MeasureSpec;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemSelectedListener;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import org.chromium.base.CalledByNative;
import org.chromium.base.CommandLine;
import org.chromium.chrome.ChromeSwitches;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.toolbar.ToolbarModel;
import org.chromium.chrome.browser.ui.toolbar.ToolbarModelSecurityLevel;
import org.chromium.content.browser.WebContentsObserver;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.Clipboard;
import org.chromium.ui.base.DeviceFormFactor;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.Arrays;
import java.util.List;

/**
 * Java side of Android implementation of the website settings UI.
 * TODO(sashab): Rename this, and all its resources, to PageInfo* and page_info_* instead of
 *               WebsiteSettings* and website_settings_*. Do this on the C++ side as well.
 */
public class WebsiteSettingsPopup implements OnClickListener, OnItemSelectedListener {
    /**
     * An entry in the settings dropdown for a given permission. There are two options for each
     * permission: Allow and Block.
     */
    private static final class PageInfoPermissionEntry {
        public final String name;
        public final int type;
        public final int value;

        PageInfoPermissionEntry(String name, int type, int value) {
            this.name = name;
            this.type = type;
            this.value = value;
        }

        @Override
        public String toString() {
            return name;
        }
    }

    private static final int MAX_TABLET_DIALOG_WIDTH_DP = 400;

    private final Context mContext;
    private final WebContents mWebContents;

    // A pointer to the C++ object for this UI.
    private final long mNativeWebsiteSettingsPopup;

    // The outer container, filled with the layout from website_settings.xml.
    private final LinearLayout mContainer;

    // UI elements in the dialog.
    private final TextView mUrlTitle;
    private final TextView mUrlConnectionMessage;
    private final LinearLayout mPermissionsList;
    private final Button mCopyUrlButton;
    private final Button mSiteSettingsButton;
    private final Button mDoneButton;

    // The dialog the container is placed in.
    private final Dialog mDialog;

    // The full URL from the URL bar, which is copied to the user's clipboard when they select 'Copy
    // URL'.
    private String mFullUrl;
    private URI mUrl;

    /**
     * Creates the WebsiteSettingsPopup, but does not display it. Also initializes the corresponding
     * C++ object and saves a pointer to it.
     *
     * @param context Context which is used for launching a dialog.
     * @param webContents The WebContents for which to show Website information. This information is
     *                    retrieved for the visible entry.
     */
    private WebsiteSettingsPopup(Context context, WebContents webContents) {
        mContext = context;
        mWebContents = webContents;

        // Find the container and all it's important subviews.
        mContainer = (LinearLayout) LayoutInflater.from(mContext).inflate(
                R.layout.website_settings, null);

        mUrlTitle = (TextView) mContainer
                .findViewById(R.id.website_settings_url);
        mUrlConnectionMessage = (TextView) mContainer
                .findViewById(R.id.website_settings_connection_message);
        mPermissionsList = (LinearLayout) mContainer
                .findViewById(R.id.website_settings_permissions_list);

        mCopyUrlButton = (Button) mContainer.findViewById(R.id.website_settings_copy_url_button);
        mCopyUrlButton.setOnClickListener(this);

        mSiteSettingsButton = (Button) mContainer
                .findViewById(R.id.website_settings_site_settings_button);
        mSiteSettingsButton.setOnClickListener(this);
        // Hide the Site Settings button until there's a link to take it to.
        // TODO(sashab,finnur): Make this button visible once Site Settings is working.
        mSiteSettingsButton.setVisibility(View.GONE);

        mDoneButton = (Button) mContainer.findViewById(R.id.website_settings_done_button);
        mDoneButton.setOnClickListener(this);

        // Create the dialog.
        mDialog = new Dialog(mContext);
        mDialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        mDialog.setCanceledOnTouchOutside(true);

        // On smaller screens, place the dialog at the top of the screen, and remove its border.
        if (!DeviceFormFactor.isTablet(mContext)) {
            Window window = mDialog.getWindow();
            window.setGravity(Gravity.TOP);
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        }

        // This needs to come after other member initialization.
        mNativeWebsiteSettingsPopup = nativeInit(this, webContents);
        final WebContentsObserver webContentsObserver = new WebContentsObserver(mWebContents) {
            @Override
            public void navigationEntryCommitted() {
                // If a navigation is committed (e.g. from in-page redirect), the data we're showing
                // is stale so dismiss the dialog.
                mDialog.dismiss();
            }
        };
        mDialog.setOnDismissListener(new DialogInterface.OnDismissListener() {
            @Override
            public void onDismiss(DialogInterface dialog) {
                assert mNativeWebsiteSettingsPopup != 0;
                webContentsObserver.detachFromWebContents();
                nativeDestroy(mNativeWebsiteSettingsPopup);
            }
        });
    }

    /**
     * Finds the Image resource of the icon to use for the given permission.
     *
     * @param permission A valid ContentSettingsType that can be displayed in the PageInfo dialog to
     *                   retrieve the image for.
     * @return The resource ID of the icon to use for that permission.
     */
    private int getImageResourceForPermission(int permission) {
        switch (permission) {
            case ContentSettingsType.CONTENT_SETTINGS_TYPE_IMAGES:
                return R.drawable.permission_images;
            case ContentSettingsType.CONTENT_SETTINGS_TYPE_JAVASCRIPT:
                return R.drawable.permission_javascript;
            case ContentSettingsType.CONTENT_SETTINGS_TYPE_GEOLOCATION:
                return R.drawable.permission_location;
            case ContentSettingsType.CONTENT_SETTINGS_TYPE_MEDIASTREAM:
                return R.drawable.permission_media;
            case ContentSettingsType.CONTENT_SETTINGS_TYPE_NOTIFICATIONS:
                return R.drawable.permission_push_notification;
            case ContentSettingsType.CONTENT_SETTINGS_TYPE_POPUPS:
                return R.drawable.permission_popups;
            default:
                assert false : "Icon requested for invalid permission: " + permission;
                return -1;
        }
    }

    @CalledByNative
    private void updatePageDetails(boolean isInternalPage) {
        mFullUrl = mWebContents.getVisibleUrl();
        int securityLevel = ToolbarModel.getSecurityLevelForWebContents(mWebContents);

        try {
            mUrl = new URI(mFullUrl);
        } catch (URISyntaxException e) {
            assert false : "Invalid URL specified: " + mFullUrl;
        }

        int schemeColorId = -1;
        if (securityLevel == ToolbarModelSecurityLevel.SECURITY_ERROR) {
            schemeColorId = R.color.website_settings_popup_url_scheme_broken;

            String leadingText = mContext.getResources().getString(
                    R.string.page_info_connection_broken_leading_text);
            String followingText = mContext.getResources().getString(
                    R.string.page_info_connection_broken_following_text,
                    UrlUtilities.getOriginForDisplay(mUrl, false));
            SpannableStringBuilder sb = new SpannableStringBuilder(leadingText + " "
                    + followingText);
            final ForegroundColorSpan redSpan = new ForegroundColorSpan(mContext.getResources()
                    .getColor(R.color.website_settings_popup_url_scheme_broken));
            final StyleSpan boldSpan = new StyleSpan(android.graphics.Typeface.BOLD);
            sb.setSpan(redSpan, 0, leadingText.length(), Spannable.SPAN_INCLUSIVE_EXCLUSIVE);
            sb.setSpan(boldSpan, 0, leadingText.length(), Spannable.SPAN_INCLUSIVE_EXCLUSIVE);
            mUrlConnectionMessage.setText(sb);
        } else {
            int connectionMessageId = 0;
            if (isInternalPage) {
                schemeColorId = R.color.website_settings_popup_url_scheme_http;
                connectionMessageId = R.string.page_info_connection_internal_page;
            } else {
                switch (securityLevel) {
                    case ToolbarModelSecurityLevel.NONE:
                        schemeColorId = R.color.website_settings_popup_url_scheme_http;
                        connectionMessageId = R.string.page_info_connection_http;
                        break;
                    case ToolbarModelSecurityLevel.SECURE:
                    case ToolbarModelSecurityLevel.EV_SECURE:
                        schemeColorId = R.color.website_settings_popup_url_scheme_https;
                        connectionMessageId = R.string.page_info_connection_https;
                        break;
                    case ToolbarModelSecurityLevel.SECURITY_WARNING:
                    case ToolbarModelSecurityLevel.SECURITY_POLICY_WARNING:
                        schemeColorId = R.color.website_settings_popup_url_scheme_mixed;
                        connectionMessageId = R.string.page_info_connection_mixed;
                        break;
                    default:
                        assert false : "Invalid security level specified: " + securityLevel;
                        schemeColorId = R.color.website_settings_popup_url_scheme_http;
                        connectionMessageId = R.string.page_info_connection_http;
                }
            }
            mUrlConnectionMessage.setText(mContext.getResources().getString(connectionMessageId));
        }

        // Color the URI-parsed version of the URL.
        SpannableStringBuilder sb = new SpannableStringBuilder(mUrl.toString());
        final ForegroundColorSpan schemeColorSpan = new ForegroundColorSpan(mContext.getResources()
                .getColor(schemeColorId));
        sb.setSpan(schemeColorSpan, 0, mUrl.getScheme().length(),
                Spannable.SPAN_INCLUSIVE_EXCLUSIVE);
        if (securityLevel == ToolbarModelSecurityLevel.SECURITY_ERROR) {
            sb.setSpan(new StrikethroughSpan(), 0, mUrl.getScheme().length(),
                    Spannable.SPAN_INCLUSIVE_EXCLUSIVE);
        }

        // The domain is everything after the scheme until the end of the
        // origin.
        final ForegroundColorSpan domainColorSpan = new ForegroundColorSpan(
                mContext.getResources().getColor(R.color.website_settings_popup_url_domain));
        sb.setSpan(domainColorSpan, mUrl.getScheme().length(),
                UrlUtilities.getOriginForDisplay(mUrl, true).length(),
                Spannable.SPAN_INCLUSIVE_EXCLUSIVE);
        mUrlTitle.setText(sb);
    }

    /**
     * Adds a new row for the given permission.
     *
     * @param name The title of the permission to display to the user.
     * @param type The ContentSettingsType of the permission.
     * @param currentSetting The ContentSetting of the currently selected setting.
     */
    @CalledByNative
    private void addPermissionSection(String name, int type, int currentSetting) {
        LinearLayout permissionRow = (LinearLayout) LayoutInflater.from(mContext).inflate(
                R.layout.website_settings_permission_row, null);

        ImageView permission_icon = (ImageView) permissionRow.findViewById(
                R.id.website_settings_permission_icon);
        permission_icon.setImageResource(getImageResourceForPermission(type));

        TextView permission_name = (TextView) permissionRow.findViewById(
                R.id.website_settings_permission_name);
        permission_name.setText(name);

        Spinner permission_spinner = (Spinner) permissionRow.findViewById(
                R.id.website_settings_permission_spinner);

        // Work out the index of the currently selected setting.
        int selectedSettingIndex = -1;
        switch (currentSetting) {
            case ContentSetting.ALLOW:
                selectedSettingIndex = 0;
                break;
            case ContentSetting.BLOCK:
                selectedSettingIndex = 1;
                break;
            default:
                assert false : "Invalid setting " + currentSetting + " for permission " + type;
        }

        List<PageInfoPermissionEntry> settingsChoices = Arrays.asList(
                new PageInfoPermissionEntry(mContext.getResources().getString(
                        R.string.page_info_permission_allow), type, ContentSetting.ALLOW),
                new PageInfoPermissionEntry(mContext.getResources().getString(
                        R.string.page_info_permission_block), type, ContentSetting.BLOCK));
        ArrayAdapter<PageInfoPermissionEntry> adapter = new ArrayAdapter<PageInfoPermissionEntry>(
                mContext, R.drawable.website_settings_permission_spinner_item, settingsChoices);
        adapter.setDropDownViewResource(
                R.drawable.website_settings_permission_spinner_dropdown_item);
        permission_spinner.setAdapter(adapter);
        permission_spinner.setSelection(selectedSettingIndex, false);
        permission_spinner.setOnItemSelectedListener(this);
        mPermissionsList.addView(permissionRow);
    }

    /**
     * Displays the WebsiteSettingsPopup.
     */
    @CalledByNative
    private void showDialog() {
        if (!DeviceFormFactor.isTablet(mContext)) {
            // On smaller screens, make the dialog fill the width of the screen.
            ScrollView scrollView = new ScrollView(mContext);
            scrollView.addView(mContainer);
            mDialog.addContentView(scrollView, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.MATCH_PARENT));

            // This must be called after addContentView, or it won't fully fill to the edge.
            Window window = mDialog.getWindow();
            window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT);
        } else {
            // On larger screens, make the dialog centered in the screen and have a maximum width.
            ScrollView scrollView = new ScrollView(mContext) {
                @Override
                protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
                    final int maxDialogWidthInPx = (int) (MAX_TABLET_DIALOG_WIDTH_DP
                            * mContext.getResources().getDisplayMetrics().density);
                    if (MeasureSpec.getSize(widthMeasureSpec) > maxDialogWidthInPx) {
                        widthMeasureSpec = MeasureSpec.makeMeasureSpec(maxDialogWidthInPx,
                                MeasureSpec.EXACTLY);
                    }
                    super.onMeasure(widthMeasureSpec, heightMeasureSpec);
                }
            };

            scrollView.addView(mContainer);
            mDialog.addContentView(scrollView, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.MATCH_PARENT));
        }

        mDialog.show();
    }

    @Override
    public void onItemSelected(AdapterView<?> parent, View view, int pos, long id) {
        PageInfoPermissionEntry entry = (PageInfoPermissionEntry) parent.getItemAtPosition(pos);
        nativeOnPermissionSettingChanged(mNativeWebsiteSettingsPopup, entry.type, entry.value);
    }

    @Override
    public void onNothingSelected(AdapterView<?> parent) {
        // Do nothing intentionally.
    }

    @Override
    public void onClick(View view) {
        if (view == mCopyUrlButton) {
            new Clipboard(mContext).setText(mFullUrl, mFullUrl);
            mDialog.dismiss();
        } else if (view == mSiteSettingsButton) {
            // TODO(sashab,finnur): Make this open the Website Settings dialog.
            assert false : "No Website Settings here!";
            mDialog.dismiss();
        } else if (view == mDoneButton) {
            mDialog.dismiss();
        }
    }

    /**
     * Shows a WebsiteSettings dialog for the provided WebContents. The popup adds itself to the
     * view hierarchy which owns the reference while it's visible.
     *
     * @param context Context which is used for launching a dialog.
     * @param webContents The WebContents for which to show Website information. This information is
     *                    retrieved for the visible entry.
     */
    @SuppressWarnings("unused")
    public static void show(Context context, WebContents webContents) {
        if (!CommandLine.getInstance().hasSwitch(ChromeSwitches.DISABLE_NEW_WEBSITE_SETTINGS)) {
            new WebsiteSettingsPopup(context, webContents);
        } else {
            WebsiteSettingsPopupLegacy.show(context, webContents);
        }
    }

    private static native long nativeInit(WebsiteSettingsPopup popup, WebContents webContents);

    private native void nativeDestroy(long nativeWebsiteSettingsPopupAndroid);

    private native void nativeOnPermissionSettingChanged(long nativeWebsiteSettingsPopupAndroid,
            int type, int setting);
}
