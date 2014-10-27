// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import org.chromium.base.CalledByNative;
import org.chromium.base.JNINamespace;
import org.chromium.base.ObserverList;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.components.bookmarks.BookmarkId;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Access gate to C++ side enhanced bookmarks functionalities.
 */
@JNINamespace("enhanced_bookmarks::android")
public final class EnhancedBookmarksBridge {
    private long mNativeEnhancedBookmarksBridge;
    private final ObserverList<FiltersObserver> mObservers =
            new ObserverList<FiltersObserver>();

    /**
     * Interface to provide consumers notifications to changes in clusters
     */
    public interface FiltersObserver {
        /**
         * Invoked when client detects that filters have been
         * added / removed from the server.
         */
        void onFiltersChanged();
    }

    public EnhancedBookmarksBridge(Profile profile) {
        mNativeEnhancedBookmarksBridge = nativeInit(profile);
    }

    public void destroy() {
        assert mNativeEnhancedBookmarksBridge != 0;
        nativeDestroy(mNativeEnhancedBookmarksBridge);
        mNativeEnhancedBookmarksBridge = 0;
    }

    public String getBookmarkDescription(BookmarkId id) {
        return nativeGetBookmarkDescription(mNativeEnhancedBookmarksBridge, id.getId(),
                id.getType());
    }

    public void setBookmarkDescription(BookmarkId id, String description) {
        nativeSetBookmarkDescription(mNativeEnhancedBookmarksBridge, id.getId(), id.getType(),
                description);
    }

    /**
     * Registers a FiltersObserver to listen for filter change notifications.
     * @param observer Observer to add
     */
    public void addFiltersObserver(FiltersObserver observer) {
        mObservers.addObserver(observer);
    }

    /**
     * Unregisters a FiltersObserver from listening to filter change notifications.
     * @param observer Observer to remove
     */
    public void removeFiltersObserver(FiltersObserver observer) {
        mObservers.removeObserver(observer);
    }

    /**
     * Gets all the bookmark ids associated with a filter string.
     * @param filter The filter string
     * @return List of bookmark ids
     */
    public List<BookmarkId> getBookmarksForFilter(String filter) {
        List<BookmarkId> list = new ArrayList<BookmarkId>();
        nativeGetBookmarksForFilter(mNativeEnhancedBookmarksBridge, filter, list);
        return list;
    }

    /**
     * @return Current set of known auto-filters for bookmarks.
     */
    public List<String> getFilters() {
        List<String> list =
                Arrays.asList(nativeGetFilters(mNativeEnhancedBookmarksBridge));
        return list;
    }

    @CalledByNative
    private void onFiltersChanged() {
        for (FiltersObserver observer : mObservers) {
            observer.onFiltersChanged();
        }
    }

    @CalledByNative
    private static void addToBookmarkIdList(List<BookmarkId> bookmarkIdList, long id, int type) {
        bookmarkIdList.add(new BookmarkId(id, type));
    }

    private native long nativeInit(Profile profile);
    private native void nativeDestroy(long nativeEnhancedBookmarksBridge);
    private native String nativeGetBookmarkDescription(long nativeEnhancedBookmarksBridge, long id,
            int type);
    private native void nativeSetBookmarkDescription(long nativeEnhancedBookmarksBridge, long id,
            int type, String description);
    private native void nativeGetBookmarksForFilter(long nativeEnhancedBookmarksBridge,
            String filter, List<BookmarkId> list);
    private native String[] nativeGetFilters(long nativeEnhancedBookmarksBridge);
}
