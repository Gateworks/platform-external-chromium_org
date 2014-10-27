// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This hack prevents a bug on the cast extension.
// TODO(yoshiki): Remove this once the cast extension supports Chrome apps.
// Although localStorage in Chrome app is not supported, but it's used in the
// cast extension. This line prevents an exception on using localStorage.
window.__defineGetter__('localStorage', function() { return {}; });

var APPLICATION_ID = '4CCB98DA';

util.addPageLoadHandler(function() {
  initialize();
}.wrap());

/**
 * Starts initialization of cast-related feature.
 */
function initialize() {
  if (window.loadMockCastExtensionForTest) {
    // If the test flag is set, the mock extension for test will be laoded by
    // the test script. Sets the handler to wait for loading.
    onLoadCastExtension(initializeApi);
    return;
  }

  CastExtensionDiscoverer.findInstalledExtension(function(foundId) {
    if (foundId)
      loadCastAPI(initializeApi);
    else
      console.info('No Google Cast extension is installed.');
  }.wrap());
}

/**
 * Loads the cast API extention. If not install, the extension is installed
 * in background before load. The cast API will load the cast SDK automatically.
 * The given callback is executes after the cast SDK extension is initialized.
 *
 * @param {function} callback Callback (executed asynchronously).
 * @param {boolean=} opt_secondTry Spericy try if it's second call after
 *     installation of Cast API extension.
 */
function loadCastAPI(callback, opt_secondTry) {
  var script = document.createElement('script');

  var onError = function() {
    script.removeEventListener('error', onError);
    document.body.removeChild(script);

    if (opt_secondTry) {
      // Shows error message and exits if it's the 2nd try.
      console.error('Google Cast API extension load failed.');
      return;
    }

    // Installs the Google Cast API extension and retry loading.
    chrome.fileManagerPrivate.installWebstoreItem(
        'mafeflapfdfljijmlienjedomfjfmhpd',
        true,  // Don't use installation prompt.
        function() {
          if (chrome.runtime.lastError) {
            console.error('Google Cast API extension installation error.',
                          chrome.runtime.lastError.message);
            return;
          }

          console.info('Google Cast API extension installed.');
          // Loads API again.
          setTimeout(loadCastAPI.bind(null, callback, true));
        }.wrap());
  }.wrap();

  // Trys to load the cast API extention which is defined in manifest.json.
  script.src = '_modules/mafeflapfdfljijmlienjedomfjfmhpd/cast_sender.js';
  script.addEventListener('error', onError);
  script.addEventListener('load', onLoadCastExtension.bind(null, callback));
  document.body.appendChild(script);
}

/**
 * Loads the cast sdk extension.
 * @param {function()} callback Callback (executed asynchronously).
 */
function onLoadCastExtension(callback) {
  if(!chrome.cast || !chrome.cast.isAvailable) {
    var checkTimer = setTimeout(function() {
      console.error('Either "Google Cast API" or "Google Cast" extension ' +
                    'seems not to be installed?');
    }.wrap(), 5000);

    window['__onGCastApiAvailable'] = function(loaded, errorInfo) {
      clearTimeout(checkTimer);

      if (loaded)
        callback();
      else
        console.error('Google Cast extension load failed.', errorInfo);
    }.wrap();
  } else {
    setTimeout(callback);  // Runs asynchronously.
  }
}

/**
 * Initialize Cast API.
 */
function initializeApi() {
  var onSession = function() {
    // TODO(yoshiki): Implement this.
  };

  var onInitSuccess = function() {
    // TODO(yoshiki): Implement this.
  };

  /**
   * @param {chrome.cast.Error} error
   */
  var onError = function(error) {
    console.error('Error on Cast initialization.', error);
  };

  var sessionRequest = new chrome.cast.SessionRequest(APPLICATION_ID);
  var apiConfig = new chrome.cast.ApiConfig(sessionRequest,
                                            onSession,
                                            onReceiver);
  chrome.cast.initialize(apiConfig, onInitSuccess, onError);
}

/**
 * @param {chrome.cast.ReceiverAvailability} availability Availability of casts.
 * @param {Array.<Object>} receivers List of casts.
 */
function onReceiver(availability, receivers) {
  if (availability === chrome.cast.ReceiverAvailability.AVAILABLE) {
    if (!receivers) {
      console.error('Receiver list is empty.');
      receivers = [];
    }

    player.setCastList(receivers);
  } else if (availability == chrome.cast.ReceiverAvailability.UNAVAILABLE) {
    player.setCastList([]);
  } else {
    console.error('Unexpected response in onReceiver.', arguments);
    player.setCastList([]);
  }
}
