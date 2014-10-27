// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var appWindow = chrome.app.window.current();

document.addEventListener('DOMContentLoaded', function() {
  var flow = new Flow();
  flow.startFlow();

  // Make the close buttons close the app window.
  var closeButtons = document.getElementsByClassName('close');
  for (var i = 0; i < closeButtons.length; ++i) {
    var closeButton = closeButtons[i];
    closeButton.addEventListener('click', function(e) {
      appWindow.close();
      e.preventDefault();
    });
  }

  $('ah-cancel-button').addEventListener('click', function(e) {
    appWindow.close();
    e.preventDefault();
  });

  $('done-button').addEventListener('click', function(e) {
    appWindow.close();
    e.preventDefault();
  });

  $('ho-cancel-button').addEventListener('click', function(e) {
    appWindow.close();
    e.preventDefault();
  });

  $('hw-cancel-button').addEventListener('click', function(e) {
    appWindow.close();
    e.preventDefault();
  });

  $('st-cancel-button').addEventListener('click', function(e) {
    appWindow.close();
    e.preventDefault();
  });

  $('ah-agree-button').addEventListener('click', function(e) {
    // TODO(kcarattini): Set the Audio History setting.
    appWindow.close();
    e.preventDefault();
  });

  $('hw-agree-button').addEventListener('click', function(e) {
    flow.advanceStep();
    e.preventDefault();
  });

  // TODO(kcarattini): Remove this once speech training is implemented. The
  // way to get to the next page will be to complete the speech training.
  $('st-training').addEventListener('click', function(e) {
    if (chrome.hotwordPrivate.setAudioLoggingEnabled)
      chrome.hotwordPrivate.setAudioLoggingEnabled(true, function() {});

    if (chrome.hotwordPrivate.setHotwordAlwaysOnSearchEnabled) {
      chrome.hotwordPrivate.setHotwordAlwaysOnSearchEnabled(true,
          flow.advanceStep.bind(flow));
    }
    e.preventDefault();
  });

  // TODO(kcarattini): Remove this once speech training is implemented. The
  // way to get to the next page will be to complete the speech training.
  $('ho-training').addEventListener('click', function(e) {
    if (chrome.hotwordPrivate.setAudioLoggingEnabled)
      chrome.hotwordPrivate.setAudioLoggingEnabled(true, function() {});

    if (chrome.hotwordPrivate.setHotwordAlwaysOnSearchEnabled) {
      chrome.hotwordPrivate.setHotwordAlwaysOnSearchEnabled(true,
          flow.advanceStep.bind(flow));
    }
    e.preventDefault();
  });
});
