// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Utilities for FileOperationManager.
 */
var fileOperationUtil = {};

/**
 * Resolves a path to either a DirectoryEntry or a FileEntry, regardless of
 * whether the path is a directory or file.
 *
 * @param {DirectoryEntry} root The root of the filesystem to search.
 * @param {string} path The path to be resolved.
 * @return {Promise} Promise fulfilled with the resolved entry, or rejected with
 *     FileError.
 */
fileOperationUtil.resolvePath = function(root, path) {
  if (path === '' || path === '/')
    return Promise.resolve(root);
  return new Promise(root.getFile.bind(root, path, {create: false})).
      catch(function(error) {
        if (error.name === util.FileError.TYPE_MISMATCH_ERR) {
          // Bah.  It's a directory, ask again.
          return new Promise(
              root.getDirectory.bind(root, path, {create: false}));
        } else {
          return Promise.reject(error);
        }
      });
};

/**
 * Checks if an entry exists at |relativePath| in |dirEntry|.
 * If exists, tries to deduplicate the path by inserting parenthesized number,
 * such as " (1)", before the extension. If it still exists, tries the
 * deduplication again by increasing the number up to 10 times.
 * For example, suppose "file.txt" is given, "file.txt", "file (1).txt",
 * "file (2).txt", ..., "file (9).txt" will be tried.
 *
 * @param {DirectoryEntry} dirEntry The target directory entry.
 * @param {string} relativePath The path to be deduplicated.
 * @param {function(string)=} opt_successCallback Callback run with the
 *     deduplicated path on success.
 * @param {function(FileOperationManager.Error)=} opt_errorCallback Callback run
 *     on error.
 * @return {Promise} Promise fulfilled with available path.
 */
fileOperationUtil.deduplicatePath = function(
    dirEntry, relativePath, opt_successCallback, opt_errorCallback) {
  // The trial is up to 10.
  var MAX_RETRY = 10;

  // Crack the path into three part. The parenthesized number (if exists) will
  // be replaced by incremented number for retry. For example, suppose
  // |relativePath| is "file (10).txt", the second check path will be
  // "file (11).txt".
  var match = /^(.*?)(?: \((\d+)\))?(\.[^.]*?)?$/.exec(relativePath);
  var prefix = match[1];
  var ext = match[3] || '';

  // Check to see if the target exists.
  var resolvePath = function(trialPath, numRetry, copyNumber) {
    return fileOperationUtil.resolvePath(dirEntry, trialPath).then(function() {
      if (numRetry <= 1) {
        // Hit the limit of the number of retrial.
        // Note that we cannot create FileError object directly, so here we
        // use Object.create instead.
        return Promise.reject(
            util.createDOMError(util.FileError.PATH_EXISTS_ERR));
      }
      var newTrialPath = prefix + ' (' + copyNumber + ')' + ext;
      return resolvePath(newTrialPath, numRetry - 1, copyNumber + 1);
    }, function(error) {
      // We expect to be unable to resolve the target file, since we're
      // going to create it during the copy.  However, if the resolve fails
      // with anything other than NOT_FOUND, that's trouble.
      if (error.name === util.FileError.NOT_FOUND_ERR)
        return trialPath;
      else
        return Promise.reject(error);
    });
  };

  var promise = resolvePath(relativePath, MAX_RETRY, 1).catch(function(error) {
    var targetPromise;
    if (error.name === util.FileError.PATH_EXISTS_ERR) {
      // Failed to uniquify the file path. There should be an existing
      // entry, so return the error with it.
      targetPromise = fileOperationUtil.resolvePath(dirEntry, relativePath);
    } else {
      targetPromise = Promise.reject(error);
    }
    return targetPromise.then(function(entry) {
      return Promise.reject(new FileOperationManager.Error(
          util.FileOperationErrorType.TARGET_EXISTS, entry));
    }, function(/** (Error|DOMError) */ inError) {
      if (inError instanceof Error)
        return Promise.reject(inError);
      return Promise.reject(new FileOperationManager.Error(
          util.FileOperationErrorType.FILESYSTEM_ERROR, inError));
    });
  });
  if (opt_successCallback)
    promise.then(opt_successCallback, opt_errorCallback);
  return promise;
};

/**
 * Traverses files/subdirectories of the given entry, and returns them.
 * In addition, this method annotate the size of each entry. The result will
 * include the entry itself.
 *
 * @param {Entry} entry The root Entry for traversing.
 * @param {function(Array.<Entry>)} successCallback Called when the traverse
 *     is successfully done with the array of the entries.
 * @param {function(DOMError)} errorCallback Called on error with the first
 *     occurred error (i.e. following errors will just be discarded).
 */
fileOperationUtil.resolveRecursively = function(
    entry, successCallback, errorCallback) {
  var result = [];
  var error = null;
  var numRunningTasks = 0;

  var maybeInvokeCallback = function() {
    // If there still remain some running tasks, wait their finishing.
    if (numRunningTasks > 0)
      return;

    if (error)
      errorCallback(error);
    else
      successCallback(result);
  };

  // The error handling can be shared.
  var onError = function(fileError) {
    // If this is the first error, remember it.
    if (!error)
      error = fileError;
    --numRunningTasks;
    maybeInvokeCallback();
  };

  var process = function(entry) {
    numRunningTasks++;
    result.push(entry);
    if (entry.isDirectory) {
      // The size of a directory is 1 bytes here, so that the progress bar
      // will work smoother.
      // TODO(hidehiko): Remove this hack.
      entry.size = 1;

      // Recursively traverse children.
      var reader = entry.createReader();
      reader.readEntries(
          function processSubEntries(subEntries) {
            if (error || subEntries.length == 0) {
              // If an error is found already, or this is the completion
              // callback, then finish the process.
              --numRunningTasks;
              maybeInvokeCallback();
              return;
            }

            for (var i = 0; i < subEntries.length; i++)
              process(subEntries[i]);

            // Continue to read remaining children.
            reader.readEntries(processSubEntries, onError);
          },
          onError);
    } else {
      // For a file, annotate the file size.
      entry.getMetadata(function(metadata) {
        entry.size = metadata.size;
        --numRunningTasks;
        maybeInvokeCallback();
      }, onError);
    }
  };

  process(entry);
};

/**
 * Copies source to parent with the name newName recursively.
 * This should work very similar to FileSystem API's copyTo. The difference is;
 * - The progress callback is supported.
 * - The cancellation is supported.
 *
 * @param {Entry} source The entry to be copied.
 * @param {DirectoryEntry} parent The entry of the destination directory.
 * @param {string} newName The name of copied file.
 * @param {function(Entry, Entry)} entryChangedCallback
 *     Callback invoked when an entry is created with the source Entry and
 *     the destination Entry.
 * @param {function(Entry, number)} progressCallback Callback invoked
 *     periodically during the copying. It takes the source Entry and the
 *     processed bytes of it.
 * @param {function(Entry)} successCallback Callback invoked when the copy
 *     is successfully done with the Entry of the created entry.
 * @param {function(DOMError)} errorCallback Callback invoked when an error
 *     is found.
 * @return {function()} Callback to cancel the current file copy operation.
 *     When the cancel is done, errorCallback will be called. The returned
 *     callback must not be called more than once.
 */
fileOperationUtil.copyTo = function(
    source, parent, newName, entryChangedCallback, progressCallback,
    successCallback, errorCallback) {
  var copyId = null;
  var pendingCallbacks = [];

  // Makes the callback called in order they were invoked.
  var callbackQueue = new AsyncUtil.Queue();

  var onCopyProgress = function(progressCopyId, status) {
    callbackQueue.run(function(callback) {
      if (copyId === null) {
        // If the copyId is not yet available, wait for it.
        pendingCallbacks.push(
            onCopyProgress.bind(null, progressCopyId, status));
        callback();
        return;
      }

      // This is not what we're interested in.
      if (progressCopyId != copyId) {
        callback();
        return;
      }

      switch (status.type) {
        case 'begin_copy_entry':
          callback();
          break;

        case 'end_copy_entry':
          // TODO(mtomasz): Convert URL to Entry in custom bindings.
          (source.isFile ? parent.getFile : parent.getDirectory).call(
              parent,
              newName,
              null,
              function(entry) {
                entryChangedCallback(status.sourceUrl, entry);
                callback();
              },
              function() {
                entryChangedCallback(status.sourceUrl, null);
                callback();
              });
          break;

        case 'progress':
          progressCallback(status.sourceUrl, status.size);
          callback();
          break;

        case 'success':
          chrome.fileManagerPrivate.onCopyProgress.removeListener(
              onCopyProgress);
          // TODO(mtomasz): Convert URL to Entry in custom bindings.
          util.URLsToEntries(
              [status.destinationUrl], function(destinationEntries) {
                successCallback(destinationEntries[0] || null);
                callback();
              });
          break;

        case 'error':
          chrome.fileManagerPrivate.onCopyProgress.removeListener(
              onCopyProgress);
          errorCallback(util.createDOMError(status.error));
          callback();
          break;

        default:
          // Found unknown state. Cancel the task, and return an error.
          console.error('Unknown progress type: ' + status.type);
          chrome.fileManagerPrivate.onCopyProgress.removeListener(
              onCopyProgress);
          chrome.fileManagerPrivate.cancelCopy(copyId);
          errorCallback(util.createDOMError(
              util.FileError.INVALID_STATE_ERR));
          callback();
      }
    });
  };

  // Register the listener before calling startCopy. Otherwise some events
  // would be lost.
  chrome.fileManagerPrivate.onCopyProgress.addListener(onCopyProgress);

  // Then starts the copy.
  // TODO(mtomasz): Convert URL to Entry in custom bindings.
  chrome.fileManagerPrivate.startCopy(
      source.toURL(), parent.toURL(), newName, function(startCopyId) {
        // last error contains the FileError code on error.
        if (chrome.runtime.lastError) {
          // Unsubscribe the progress listener.
          chrome.fileManagerPrivate.onCopyProgress.removeListener(
              onCopyProgress);
          errorCallback(util.createDOMError(
              chrome.runtime.lastError.message || ''));
          return;
        }

        copyId = startCopyId;
        for (var i = 0; i < pendingCallbacks.length; i++) {
          pendingCallbacks[i]();
        }
      });

  return function() {
    // If copyId is not yet available, wait for it.
    if (copyId == null) {
      pendingCallbacks.push(function() {
        chrome.fileManagerPrivate.cancelCopy(copyId);
      });
      return;
    }

    chrome.fileManagerPrivate.cancelCopy(copyId);
  };
};

/**
 * Thin wrapper of chrome.fileManagerPrivate.zipSelection to adapt its
 * interface similar to copyTo().
 *
 * @param {Array.<Entry>} sources The array of entries to be archived.
 * @param {DirectoryEntry} parent The entry of the destination directory.
 * @param {string} newName The name of the archive to be created.
 * @param {function(FileEntry)} successCallback Callback invoked when the
 *     operation is successfully done with the entry of the created archive.
 * @param {function(DOMError)} errorCallback Callback invoked when an error
 *     is found.
 */
fileOperationUtil.zipSelection = function(
    sources, parent, newName, successCallback, errorCallback) {
  // TODO(mtomasz): Move conversion from entry to url to custom bindings.
  // crbug.com/345527.
  chrome.fileManagerPrivate.zipSelection(
      parent.toURL(),
      util.entriesToURLs(sources),
      newName, function(success) {
        if (!success) {
          // Failed to create a zip archive.
          errorCallback(
              util.createDOMError(util.FileError.INVALID_MODIFICATION_ERR));
          return;
        }

        // Returns the created entry via callback.
        parent.getFile(
            newName, {create: false}, successCallback, errorCallback);
      });
};

/**
 * @constructor
 */
function FileOperationManager() {
  this.copyTasks_ = [];
  this.deleteTasks_ = [];
  this.taskIdCounter_ = 0;
  this.eventRouter_ = new FileOperationManager.EventRouter();

  Object.seal(this);
}

/**
 * Manages Event dispatching.
 * Currently this can send three types of events: "copy-progress",
 * "copy-operation-completed" and "delete".
 *
 * TODO(hidehiko): Reorganize the event dispatching mechanism.
 * @constructor
 * @extends {cr.EventTarget}
 */
FileOperationManager.EventRouter = function() {
  this.pendingDeletedEntries_ = [];
  this.pendingCreatedEntries_ = [];
  this.entryChangedEventRateLimiter_ = new AsyncUtil.RateLimiter(
      this.dispatchEntryChangedEvent_.bind(this), 500);
};

/**
 * Extends cr.EventTarget.
 */
FileOperationManager.EventRouter.prototype.__proto__ = cr.EventTarget.prototype;

/**
 * Dispatches a simple "copy-progress" event with reason and current
 * FileOperationManager status. If it is an ERROR event, error should be set.
 *
 * @param {string} reason Event type. One of "BEGIN", "PROGRESS", "SUCCESS",
 *     "ERROR" or "CANCELLED". TODO(hidehiko): Use enum.
 * @param {Object} status Current FileOperationManager's status. See also
 *     FileOperationManager.Task.getStatus().
 * @param {string} taskId ID of task related with the event.
 * @param {FileOperationManager.Error=} opt_error The info for the error. This
 *     should be set iff the reason is "ERROR".
 */
FileOperationManager.EventRouter.prototype.sendProgressEvent = function(
    reason, status, taskId, opt_error) {
  // Before finishing operation, dispatch pending entries-changed events.
  if (reason === 'SUCCESS' || reason === 'CANCELED')
    this.entryChangedEventRateLimiter_.runImmediately();

  var event = /** @type {FileOperationProgressEvent} */
      (new Event('copy-progress'));
  event.reason = reason;
  event.status = status;
  event.taskId = taskId;
  if (opt_error)
    event.error = opt_error;
  this.dispatchEvent(event);
};

/**
 * Stores changed (created or deleted) entry temporarily, and maybe dispatch
 * entries-changed event with stored entries.
 * @param {util.EntryChangedKind} kind The enum to represent if the entry is
 *     created or deleted.
 * @param {Entry} entry The changed entry.
 */
FileOperationManager.EventRouter.prototype.sendEntryChangedEvent = function(
    kind, entry) {
  if (kind === util.EntryChangedKind.DELETED)
    this.pendingDeletedEntries_.push(entry);
  if (kind === util.EntryChangedKind.CREATED)
    this.pendingCreatedEntries_.push(entry);

  this.entryChangedEventRateLimiter_.run();
};

/**
 * Dispatches an event to notify that entries are changed (created or deleted).
 * @private
 */
FileOperationManager.EventRouter.prototype.dispatchEntryChangedEvent_ =
    function() {
  if (this.pendingDeletedEntries_.length > 0) {
    var event = new Event('entries-changed');
    event.kind = util.EntryChangedKind.DELETED;
    event.entries = this.pendingDeletedEntries_;
    this.dispatchEvent(event);
    this.pendingDeletedEntries_ = [];
  }
  if (this.pendingCreatedEntries_.length > 0) {
    var event = new Event('entries-changed');
    event.kind = util.EntryChangedKind.CREATED;
    event.entries = this.pendingCreatedEntries_;
    this.dispatchEvent(event);
    this.pendingCreatedEntries_ = [];
  }
};

/**
 * Dispatches an event to notify entries are changed for delete task.
 *
 * @param {string} reason Event type. One of "BEGIN", "PROGRESS", "SUCCESS",
 *     or "ERROR". TODO(hidehiko): Use enum.
 * @param {!Object} task Delete task related with the event.
 */
FileOperationManager.EventRouter.prototype.sendDeleteEvent = function(
    reason, task) {
  var event = /** @type {FileOperationProgressEvent} */ (new Event('delete'));
  event.reason = reason;
  event.taskId = task.taskId;
  event.entries = task.entries;
  event.totalBytes = task.totalBytes;
  event.processedBytes = task.processedBytes;
  this.dispatchEvent(event);
};

/**
 * A record of a queued copy operation.
 *
 * Multiple copy operations may be queued at any given time.  Additional
 * Tasks may be added while the queue is being serviced.  Though a
 * cancel operation cancels everything in the queue.
 *
 * @param {util.FileOperationType} operationType The type of this operation.
 * @param {Array.<Entry>} sourceEntries Array of source entries.
 * @param {DirectoryEntry} targetDirEntry Target directory.
 * @constructor
 */
FileOperationManager.Task = function(
    operationType, sourceEntries, targetDirEntry) {
  this.operationType = operationType;
  this.sourceEntries = sourceEntries;
  this.targetDirEntry = targetDirEntry;

  /**
   * An array of map from url to Entry being processed.
   * @type {Array.<Object<string, Entry>>}
   */
  this.processingEntries = null;

  /**
   * Total number of bytes to be processed. Filled in initialize().
   * Use 1 as an initial value to indicate that the task is not completed.
   * @type {number}
   */
  this.totalBytes = 1;

  /**
   * Total number of already processed bytes. Updated periodically.
   * @type {number}
   */
  this.processedBytes = 0;

  /**
   * Index of the progressing entry in sourceEntries.
   * @type {number}
   * @private
   */
  this.processingSourceIndex_ = 0;

  /**
   * Set to true when cancel is requested.
   * @private {boolean}
   */
  this.cancelRequested_ = false;

  /**
   * Callback to cancel the running process.
   * @private {?function()}
   */
  this.cancelCallback_ = null;

  // TODO(hidehiko): After we support recursive copy, we don't need this.
  // If directory already exists, we try to make a copy named 'dir (X)',
  // where X is a number. When we do this, all subsequent copies from
  // inside the subtree should be mapped to the new directory name.
  // For example, if 'dir' was copied as 'dir (1)', then 'dir/file.txt' should
  // become 'dir (1)/file.txt'.
  this.renamedDirectories_ = [];
};

/**
 * @param {function()} callback When entries resolved.
 */
FileOperationManager.Task.prototype.initialize = function(callback) {
};

/**
 * Requests cancellation of this task.
 * When the cancellation is done, it is notified via callbacks of run().
 */
FileOperationManager.Task.prototype.requestCancel = function() {
  this.cancelRequested_ = true;
  if (this.cancelCallback_) {
    this.cancelCallback_();
    this.cancelCallback_ = null;
  }
};

/**
 * Runs the task. Sub classes must implement this method.
 *
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function()} progressCallback Callback invoked periodically during
 *     the operation.
 * @param {function()} successCallback Callback run on success.
 * @param {function(FileOperationManager.Error)} errorCallback Callback run on
 *     error.
 */
FileOperationManager.Task.prototype.run = function(
    entryChangedCallback, progressCallback, successCallback, errorCallback) {
};

/**
 * Get states of the task.
 * TOOD(hirono): Removes this method and sets a task to progress events.
 * @return {Object} Status object.
 */
FileOperationManager.Task.prototype.getStatus = function() {
  var processingEntry = this.sourceEntries[this.processingSourceIndex_];
  return {
    operationType: this.operationType,
    numRemainingItems: this.sourceEntries.length - this.processingSourceIndex_,
    totalBytes: this.totalBytes,
    processedBytes: this.processedBytes,
    processingEntryName: processingEntry ? processingEntry.name : ''
  };
};

/**
 * Obtains the number of total processed bytes.
 * @return {number} Number of total processed bytes.
 * @private
 */
FileOperationManager.Task.prototype.calcProcessedBytes_ = function() {
  var bytes = 0;
  for (var i = 0; i < this.processingSourceIndex_ + 1; i++) {
    var entryMap = this.processingEntries[i];
    if (!entryMap)
      break;
    for (var name in entryMap) {
      bytes += i < this.processingSourceIndex_ ?
          entryMap[name].size : entryMap[name].processedBytes;
    }
  }
  return bytes;
};

/**
 * Task to copy entries.
 *
 * @param {Array.<Entry>} sourceEntries Array of source entries.
 * @param {DirectoryEntry} targetDirEntry Target directory.
 * @param {boolean} deleteAfterCopy Whether the delete original files after
 *     copy.
 * @constructor
 * @extends {FileOperationManager.Task}
 */
FileOperationManager.CopyTask = function(sourceEntries,
                                         targetDirEntry,
                                         deleteAfterCopy) {
  FileOperationManager.Task.call(
      this,
      deleteAfterCopy ?
          util.FileOperationType.MOVE : util.FileOperationType.COPY,
      sourceEntries,
      targetDirEntry);
  this.deleteAfterCopy = deleteAfterCopy;

  /**
   * Rate limiter which is used to avoid sending update request for progress bar
   * too frequently.
   * @type {AsyncUtil.RateLimiter}
   * @private
   */
  this.updateProgressRateLimiter_ = null;
};

/**
 * Extends FileOperationManager.Task.
 */
FileOperationManager.CopyTask.prototype.__proto__ =
    FileOperationManager.Task.prototype;

/**
 * Initializes the CopyTask.
 * @param {function()} callback Called when the initialize is completed.
 */
FileOperationManager.CopyTask.prototype.initialize = function(callback) {
  var group = new AsyncUtil.Group();
  // Correct all entries to be copied for status update.
  this.processingEntries = [];
  for (var i = 0; i < this.sourceEntries.length; i++) {
    group.add(function(index, callback) {
      fileOperationUtil.resolveRecursively(
          this.sourceEntries[index],
          function(resolvedEntries) {
            var resolvedEntryMap = {};
            for (var j = 0; j < resolvedEntries.length; ++j) {
              var entry = resolvedEntries[j];
              entry.processedBytes = 0;
              resolvedEntryMap[entry.toURL()] = entry;
            }
            this.processingEntries[index] = resolvedEntryMap;
            callback();
          }.bind(this),
          function(error) {
            console.error(
                'Failed to resolve for copy: %s', error.name);
            callback();
          });
    }.bind(this, i));
  }

  group.run(function() {
    // Fill totalBytes.
    this.totalBytes = 0;
    for (var i = 0; i < this.processingEntries.length; i++) {
      for (var entryURL in this.processingEntries[i])
        this.totalBytes += this.processingEntries[i][entryURL].size;
    }

    callback();
  }.bind(this));
};

/**
 * Copies all entries to the target directory.
 * Note: this method contains also the operation of "Move" due to historical
 * reason.
 *
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function()} progressCallback Callback invoked periodically during
 *     the copying.
 * @param {function()} successCallback On success.
 * @param {function(FileOperationManager.Error)} errorCallback On error.
 * @override
 */
FileOperationManager.CopyTask.prototype.run = function(
    entryChangedCallback, progressCallback, successCallback, errorCallback) {
  // TODO(hidehiko): We should be able to share the code to iterate on entries
  // with serviceMoveTask_().
  if (this.sourceEntries.length == 0) {
    successCallback();
    return;
  }

  // TODO(hidehiko): Delete after copy is the implementation of Move.
  // Migrate the part into MoveTask.run().
  var deleteOriginals = function() {
    var count = this.sourceEntries.length;

    var onEntryDeleted = function(entry) {
      entryChangedCallback(util.EntryChangedKind.DELETED, entry);
      count--;
      if (!count)
        successCallback();
    };

    var onFilesystemError = function(err) {
      errorCallback(new FileOperationManager.Error(
          util.FileOperationErrorType.FILESYSTEM_ERROR, err));
    };

    for (var i = 0; i < this.sourceEntries.length; i++) {
      var entry = this.sourceEntries[i];
      util.removeFileOrDirectory(
          entry, onEntryDeleted.bind(null, entry), onFilesystemError);
    }
  }.bind(this);

  /**
   * Accumulates processed bytes and call |progressCallback| if needed.
   *
   * @param {number} index The index of processing source.
   * @param {string} sourceEntryUrl URL of the entry which has been processed.
   * @param {number=} opt_size Processed bytes of the |sourceEntry|. If it is
   *     dropped, all bytes of the entry are considered to be processed.
   */
  var updateProgress = function(index, sourceEntryUrl, opt_size) {
    if (!sourceEntryUrl)
      return;

    var processedEntry = this.processingEntries[index][sourceEntryUrl];
    if (!processedEntry)
      return;

    // Accumulates newly processed bytes.
    var size = opt_size !== undefined ? opt_size : processedEntry.size;
    this.processedBytes += size - processedEntry.processedBytes;
    processedEntry.processedBytes = size;

    // Updates progress bar in limited frequency so that intervals between
    // updates have at least 200ms.
    this.updateProgressRateLimiter_.run();
  };
  updateProgress = updateProgress.bind(this);

  this.updateProgressRateLimiter_ = new AsyncUtil.RateLimiter(progressCallback);

  AsyncUtil.forEach(
      this.sourceEntries,
      function(callback, entry, index) {
        if (this.cancelRequested_) {
          errorCallback(new FileOperationManager.Error(
              util.FileOperationErrorType.FILESYSTEM_ERROR,
              util.createDOMError(util.FileError.ABORT_ERR)));
          return;
        }
        progressCallback();
        this.processEntry_(
            entry, this.targetDirEntry,
            function(sourceEntryUrl, destinationEntry) {
              updateProgress(index, sourceEntryUrl);
              // The destination entry may be null, if the copied file got
              // deleted just after copying.
              if (destinationEntry) {
                entryChangedCallback(
                    util.EntryChangedKind.CREATED, destinationEntry);
              }
            },
            function(sourceEntryUrl, size) {
              updateProgress(index, sourceEntryUrl, size);
            },
            function() {
              // Finishes off delayed updates if necessary.
              this.updateProgressRateLimiter_.runImmediately();
              // Update current source index and processing bytes.
              this.processingSourceIndex_ = index + 1;
              this.processedBytes = this.calcProcessedBytes_();
              callback();
            }.bind(this),
            function(error) {
              // Finishes off delayed updates if necessary.
              this.updateProgressRateLimiter_.runImmediately();
              errorCallback(error);
            }.bind(this));
      },
      function() {
        if (this.deleteAfterCopy) {
          deleteOriginals();
        } else {
          successCallback();
        }
      }.bind(this),
      this);
};

/**
 * Copies the source entry to the target directory.
 *
 * @param {Entry} sourceEntry An entry to be copied.
 * @param {DirectoryEntry} destinationEntry The entry which will contain the
 *     copied entry.
 * @param {function(Entry, Entry)} entryChangedCallback
 *     Callback invoked when an entry is created with the source Entry and
 *     the destination Entry.
 * @param {function(Entry, number)} progressCallback Callback invoked
 *     periodically during the copying.
 * @param {function()} successCallback On success.
 * @param {function(FileOperationManager.Error)} errorCallback On error.
 * @private
 */
FileOperationManager.CopyTask.prototype.processEntry_ = function(
    sourceEntry, destinationEntry, entryChangedCallback, progressCallback,
    successCallback, errorCallback) {
  fileOperationUtil.deduplicatePath(
      destinationEntry, sourceEntry.name,
      function(destinationName) {
        if (this.cancelRequested_) {
          errorCallback(new FileOperationManager.Error(
              util.FileOperationErrorType.FILESYSTEM_ERROR,
              util.createDOMError(util.FileError.ABORT_ERR)));
          return;
        }
        this.cancelCallback_ = fileOperationUtil.copyTo(
            sourceEntry, destinationEntry, destinationName,
            entryChangedCallback, progressCallback,
            function(entry) {
              this.cancelCallback_ = null;
              successCallback();
            }.bind(this),
            function(error) {
              this.cancelCallback_ = null;
              errorCallback(new FileOperationManager.Error(
                  util.FileOperationErrorType.FILESYSTEM_ERROR, error));
            }.bind(this));
      }.bind(this),
      errorCallback);
};

/**
 * Task to move entries.
 *
 * @param {Array.<Entry>} sourceEntries Array of source entries.
 * @param {DirectoryEntry} targetDirEntry Target directory.
 * @constructor
 * @extends {FileOperationManager.Task}
 */
FileOperationManager.MoveTask = function(sourceEntries, targetDirEntry) {
  FileOperationManager.Task.call(
      this, util.FileOperationType.MOVE, sourceEntries, targetDirEntry);
};

/**
 * Extends FileOperationManager.Task.
 */
FileOperationManager.MoveTask.prototype.__proto__ =
    FileOperationManager.Task.prototype;

/**
 * Initializes the MoveTask.
 * @param {function()} callback Called when the initialize is completed.
 */
FileOperationManager.MoveTask.prototype.initialize = function(callback) {
  // This may be moving from search results, where it fails if we
  // move parent entries earlier than child entries. We should
  // process the deepest entry first. Since move of each entry is
  // done by a single moveTo() call, we don't need to care about the
  // recursive traversal order.
  this.sourceEntries.sort(function(entry1, entry2) {
    return entry2.toURL().length - entry1.toURL().length;
  });

  this.processingEntries = [];
  for (var i = 0; i < this.sourceEntries.length; i++) {
    var processingEntryMap = {};
    var entry = this.sourceEntries[i];

    // The move should be done with updating the metadata. So here we assume
    // all the file size is 1 byte. (Avoiding 0, so that progress bar can
    // move smoothly).
    // TODO(hidehiko): Remove this hack.
    entry.size = 1;
    processingEntryMap[entry.toURL()] = entry;
    this.processingEntries[i] = processingEntryMap;
  }

  callback();
};

/**
 * Moves all entries in the task.
 *
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function()} progressCallback Callback invoked periodically during
 *     the moving.
 * @param {function()} successCallback On success.
 * @param {function(FileOperationManager.Error)} errorCallback On error.
 * @override
 */
FileOperationManager.MoveTask.prototype.run = function(
    entryChangedCallback, progressCallback, successCallback, errorCallback) {
  if (this.sourceEntries.length == 0) {
    successCallback();
    return;
  }

  AsyncUtil.forEach(
      this.sourceEntries,
      function(callback, entry, index) {
        if (this.cancelRequested_) {
          errorCallback(new FileOperationManager.Error(
              util.FileOperationErrorType.FILESYSTEM_ERROR,
              util.createDOMError(util.FileError.ABORT_ERR)));
          return;
        }
        progressCallback();
        FileOperationManager.MoveTask.processEntry_(
            entry, this.targetDirEntry, entryChangedCallback,
            function() {
              // Update current source index.
              this.processingSourceIndex_ = index + 1;
              this.processedBytes = this.calcProcessedBytes_();
              callback();
            }.bind(this),
            errorCallback);
      },
      function() {
        successCallback();
      }.bind(this),
      this);
};

/**
 * Moves the sourceEntry to the targetDirEntry in this task.
 *
 * @param {Entry} sourceEntry An entry to be moved.
 * @param {!DirectoryEntry} destinationEntry The entry of the destination
 *     directory.
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function()} successCallback On success.
 * @param {function(FileOperationManager.Error)} errorCallback On error.
 * @private
 */
FileOperationManager.MoveTask.processEntry_ = function(
    sourceEntry, destinationEntry, entryChangedCallback, successCallback,
    errorCallback) {
  fileOperationUtil.deduplicatePath(
      destinationEntry,
      sourceEntry.name,
      function(destinationName) {
        sourceEntry.moveTo(
            destinationEntry, destinationName,
            function(movedEntry) {
              entryChangedCallback(util.EntryChangedKind.CREATED, movedEntry);
              entryChangedCallback(util.EntryChangedKind.DELETED, sourceEntry);
              successCallback();
            },
            function(error) {
              errorCallback(new FileOperationManager.Error(
                  util.FileOperationErrorType.FILESYSTEM_ERROR, error));
            });
      },
      errorCallback);
};

/**
 * Task to create a zip archive.
 *
 * @param {Array.<Entry>} sourceEntries Array of source entries.
 * @param {DirectoryEntry} targetDirEntry Target directory.
 * @param {DirectoryEntry} zipBaseDirEntry Base directory dealt as a root
 *     in ZIP archive.
 * @constructor
 * @extends {FileOperationManager.Task}
 */
FileOperationManager.ZipTask = function(
    sourceEntries, targetDirEntry, zipBaseDirEntry) {
  FileOperationManager.Task.call(
      this, util.FileOperationType.ZIP, sourceEntries, targetDirEntry);
  this.zipBaseDirEntry = zipBaseDirEntry;
};

/**
 * Extends FileOperationManager.Task.
 */
FileOperationManager.ZipTask.prototype.__proto__ =
    FileOperationManager.Task.prototype;


/**
 * Initializes the ZipTask.
 * @param {function()} callback Called when the initialize is completed.
 */
FileOperationManager.ZipTask.prototype.initialize = function(callback) {
  var resolvedEntryMap = {};
  var group = new AsyncUtil.Group();
  for (var i = 0; i < this.sourceEntries.length; i++) {
    group.add(function(index, callback) {
      fileOperationUtil.resolveRecursively(
          this.sourceEntries[index],
          function(entries) {
            for (var j = 0; j < entries.length; j++)
              resolvedEntryMap[entries[j].toURL()] = entries[j];
            callback();
          },
          callback);
    }.bind(this, i));
  }

  group.run(function() {
    // For zip archiving, all the entries are processed at once.
    this.processingEntries = [resolvedEntryMap];

    this.totalBytes = 0;
    for (var url in resolvedEntryMap)
      this.totalBytes += resolvedEntryMap[url].size;

    callback();
  }.bind(this));
};

/**
 * Runs a zip file creation task.
 *
 * @param {function(util.EntryChangedKind, Entry)} entryChangedCallback
 *     Callback invoked when an entry is changed.
 * @param {function()} progressCallback Callback invoked periodically during
 *     the moving.
 * @param {function()} successCallback On complete.
 * @param {function(FileOperationManager.Error)} errorCallback On error.
 * @override
 */
FileOperationManager.ZipTask.prototype.run = function(
    entryChangedCallback, progressCallback, successCallback, errorCallback) {
  // TODO(hidehiko): we should localize the name.
  var destName = 'Archive';
  if (this.sourceEntries.length == 1) {
    var entryName = this.sourceEntries[0].name;
    var i = entryName.lastIndexOf('.');
    destName = ((i < 0) ? entryName : entryName.substr(0, i));
  }

  fileOperationUtil.deduplicatePath(
      this.targetDirEntry, destName + '.zip',
      function(destPath) {
        // TODO: per-entry zip progress update with accurate byte count.
        // For now just set completedBytes to 0 so that it is not full until
        // the zip operatoin is done.
        this.processedBytes = 0;
        progressCallback();

        // The number of elements in processingEntries is 1. See also
        // initialize().
        var entries = [];
        for (var url in this.processingEntries[0])
          entries.push(this.processingEntries[0][url]);

        fileOperationUtil.zipSelection(
            entries,
            this.zipBaseDirEntry,
            destPath,
            function(entry) {
              this.processedBytes = this.totalBytes;
              entryChangedCallback(util.EntryChangedKind.CREATED, entry);
              successCallback();
            }.bind(this),
            function(error) {
              errorCallback(new FileOperationManager.Error(
                  util.FileOperationErrorType.FILESYSTEM_ERROR, error));
            });
      }.bind(this),
      errorCallback);
};

/**
 * Error class used to report problems with a copy operation.
 * If the code is UNEXPECTED_SOURCE_FILE, data should be a path of the file.
 * If the code is TARGET_EXISTS, data should be the existing Entry.
 * If the code is FILESYSTEM_ERROR, data should be the FileError.
 *
 * @param {util.FileOperationErrorType} code Error type.
 * @param {string|Entry|DOMError} data Additional data.
 * @constructor
 */
FileOperationManager.Error = function(code, data) {
  this.code = code;
  this.data = data;
};

// FileOperationManager methods.

/**
 * Adds an event listener for the tasks.
 * @param {string} type The name of the event.
 * @param {function(Event)} handler The handler for the event.
 *     This is called when the event is dispatched.
 */
FileOperationManager.prototype.addEventListener = function(type, handler) {
  this.eventRouter_.addEventListener(type, handler);
};

/**
 * Removes an event listener for the tasks.
 * @param {string} type The name of the event.
 * @param {function(Event)} handler The handler to be removed.
 */
FileOperationManager.prototype.removeEventListener = function(type, handler) {
  this.eventRouter_.removeEventListener(type, handler);
};

/**
 * Says if there are any tasks in the queue.
 * @return {boolean} True, if there are any tasks.
 */
FileOperationManager.prototype.hasQueuedTasks = function() {
  return this.copyTasks_.length > 0 || this.deleteTasks_.length > 0;
};

/**
 * Completely clear out the copy queue, either because we encountered an error
 * or completed successfully.
 *
 * @private
 */
FileOperationManager.prototype.resetQueue_ = function() {
  this.copyTasks_ = [];
};

/**
 * Requests the specified task to be canceled.
 * @param {string} taskId ID of task to be canceled.
 */
FileOperationManager.prototype.requestTaskCancel = function(taskId) {
  var task = null;
  for (var i = 0; i < this.copyTasks_.length; i++) {
    task = this.copyTasks_[i];
    if (task.taskId !== taskId)
      continue;
    task.requestCancel();
    // If the task is not on progress, remove it immediately.
    if (i !== 0) {
      this.eventRouter_.sendProgressEvent('CANCELED',
                                          task.getStatus(),
                                          task.taskId);
      this.copyTasks_.splice(i, 1);
    }
  }
  for (var i = 0; i < this.deleteTasks_.length; i++) {
    task = this.deleteTasks_[i];
    if (task.taskId !== taskId)
      continue;
    task.cancelRequested = true;
    // If the task is not on progress, remove it immediately.
    if (i !== 0) {
      this.eventRouter_.sendDeleteEvent('CANCELED', task);
      this.deleteTasks_.splice(i, 1);
    }
  }
};

/**
 * Filters the entry in the same directory
 *
 * @param {Array.<Entry>} sourceEntries Entries of the source files.
 * @param {DirectoryEntry} targetEntry The destination entry of the target
 *     directory.
 * @param {boolean} isMove True if the operation is "move", otherwise (i.e.
 *     if the operation is "copy") false.
 * @return {Promise} Promise fulfilled with the filtered entry. This is not
 *     rejected.
 */
FileOperationManager.prototype.filterSameDirectoryEntry = function(
    sourceEntries, targetEntry, isMove) {
  if (!isMove)
    return Promise.resolve(sourceEntries);
  // Utility function to concat arrays.
  var compactArrays = function(arrays) {
    return arrays.filter(function(element) { return !!element; });
  };
  // Call processEntry for each item of entries.
  var processEntries = function(entries) {
    var promises = entries.map(processFileOrDirectoryEntries);
    return Promise.all(promises).then(compactArrays);
  };
  // Check all file entries and keeps only those need sharing operation.
  var processFileOrDirectoryEntries = function(entry) {
    return new Promise(function(resolve) {
      entry.getParent(function(inParentEntry) {
        if (!util.isSameEntry(inParentEntry, targetEntry))
          resolve(entry);
        else
          resolve(null);
      }, function(error) {
        console.error(error.stack || error);
        resolve(null);
      });
    });
  };
  return processEntries(sourceEntries);
}

/**
 * Kick off pasting.
 *
 * @param {Array.<Entry>} sourceEntries Entries of the source files.
 * @param {DirectoryEntry} targetEntry The destination entry of the target
 *     directory.
 * @param {boolean} isMove True if the operation is "move", otherwise (i.e.
 *     if the operation is "copy") false.
 * @param {string=} opt_taskId If the corresponding item has already created
 *     at another places, we need to specify the ID of the item. If the
 *     item is not created, FileOperationManager generates new ID.
 */
FileOperationManager.prototype.paste = function(
    sourceEntries, targetEntry, isMove, opt_taskId) {
  // Do nothing if sourceEntries is empty.
  if (sourceEntries.length === 0)
    return;

  this.filterSameDirectoryEntry(sourceEntries, targetEntry, isMove).then(
      function(entries) {
        if (entries.length === 0)
          return;
        this.queueCopy_(targetEntry, entries, isMove, opt_taskId);
  }.bind(this)).catch(function(error) {
    console.error(error.stack || error);
  });
};

/**
 * Initiate a file copy. When copying files, null can be specified as source
 * directory.
 *
 * @param {DirectoryEntry} targetDirEntry Target directory.
 * @param {Array.<Entry>} entries Entries to copy.
 * @param {boolean} isMove In case of move.
 * @param {string=} opt_taskId If the corresponding item has already created
 *     at another places, we need to specify the ID of the item. If the
 *     item is not created, FileOperationManager generates new ID.
 * @private
 */
FileOperationManager.prototype.queueCopy_ = function(
    targetDirEntry, entries, isMove, opt_taskId) {
  var task;
  if (isMove) {
    // When moving between different volumes, moving is implemented as a copy
    // and delete. This is because moving between volumes is slow, and moveTo()
    // is not cancellable nor provides progress feedback.
    if (util.isSameFileSystem(entries[0].filesystem,
                              targetDirEntry.filesystem)) {
      task = new FileOperationManager.MoveTask(entries, targetDirEntry);
    } else {
      task = new FileOperationManager.CopyTask(entries, targetDirEntry, true);
    }
  } else {
    task = new FileOperationManager.CopyTask(entries, targetDirEntry, false);
  }

  task.taskId = opt_taskId || this.generateTaskId();
  this.eventRouter_.sendProgressEvent('BEGIN', task.getStatus(), task.taskId);
  task.initialize(function() {
    this.copyTasks_.push(task);
    if (this.copyTasks_.length === 1)
      this.serviceAllTasks_();
  }.bind(this));
};

/**
 * Service all pending tasks, as well as any that might appear during the
 * copy.
 *
 * @private
 */
FileOperationManager.prototype.serviceAllTasks_ = function() {
  if (!this.copyTasks_.length) {
    // All tasks have been serviced, clean up and exit.
    chrome.power.releaseKeepAwake();
    this.resetQueue_();
    return;
  }

  // Prevent the system from sleeping while copy is in progress.
  chrome.power.requestKeepAwake('system');

  var onTaskProgress = function() {
    this.eventRouter_.sendProgressEvent('PROGRESS',
                                        this.copyTasks_[0].getStatus(),
                                        this.copyTasks_[0].taskId);
  }.bind(this);

  var onEntryChanged = function(kind, entry) {
    this.eventRouter_.sendEntryChangedEvent(kind, entry);
  }.bind(this);

  var onTaskError = function(err) {
    var task = this.copyTasks_.shift();
    var reason = err.data.name === util.FileError.ABORT_ERR ?
        'CANCELED' : 'ERROR';
    this.eventRouter_.sendProgressEvent(reason,
                                        task.getStatus(),
                                        task.taskId,
                                        err);
    this.serviceAllTasks_();
  }.bind(this);

  var onTaskSuccess = function() {
    // The task at the front of the queue is completed. Pop it from the queue.
    var task = this.copyTasks_.shift();
    this.eventRouter_.sendProgressEvent('SUCCESS',
                                        task.getStatus(),
                                        task.taskId);
    this.serviceAllTasks_();
  }.bind(this);

  var nextTask = this.copyTasks_[0];
  this.eventRouter_.sendProgressEvent('PROGRESS',
                                      nextTask.getStatus(),
                                      nextTask.taskId);
  nextTask.run(onEntryChanged, onTaskProgress, onTaskSuccess, onTaskError);
};

/**
 * Timeout before files are really deleted (to allow undo).
 */
FileOperationManager.DELETE_TIMEOUT = 30 * 1000;

/**
 * Schedules the files deletion.
 *
 * @param {Array.<Entry>} entries The entries.
 */
FileOperationManager.prototype.deleteEntries = function(entries) {
  // TODO(hirono): Make FileOperationManager.DeleteTask.
  var task = Object.seal({
    entries: entries,
    taskId: this.generateTaskId(),
    entrySize: {},
    totalBytes: 0,
    processedBytes: 0,
    cancelRequested: false
  });

  // Obtains entry size and sum them up.
  var group = new AsyncUtil.Group();
  for (var i = 0; i < task.entries.length; i++) {
    group.add(function(entry, callback) {
      entry.getMetadata(function(metadata) {
        var index = task.entries.indexOf(entries);
        task.entrySize[entry.toURL()] = metadata.size;
        task.totalBytes += metadata.size;
        callback();
      }, function() {
        // Fail to obtain the metadata. Use fake value 1.
        task.entrySize[entry.toURL()] = 1;
        task.totalBytes += 1;
        callback();
      });
    }.bind(this, task.entries[i]));
  }

  // Add a delete task.
  group.run(function() {
    this.deleteTasks_.push(task);
    this.eventRouter_.sendDeleteEvent('BEGIN', task);
    if (this.deleteTasks_.length === 1)
      this.serviceAllDeleteTasks_();
  }.bind(this));
};

/**
 * Service all pending delete tasks, as well as any that might appear during the
 * deletion.
 *
 * Must not be called if there is an in-flight delete task.
 *
 * @private
 */
FileOperationManager.prototype.serviceAllDeleteTasks_ = function() {
  this.serviceDeleteTask_(
      this.deleteTasks_[0],
      function() {
        this.deleteTasks_.shift();
        if (this.deleteTasks_.length)
          this.serviceAllDeleteTasks_();
      }.bind(this));
};

/**
 * Performs the deletion.
 *
 * @param {Object} task The delete task (see deleteEntries function).
 * @param {function()} callback Callback run on task end.
 * @private
 */
FileOperationManager.prototype.serviceDeleteTask_ = function(task, callback) {
  var queue = new AsyncUtil.Queue();

  // Delete each entry.
  var error = null;
  var deleteOneEntry = function(inCallback) {
    if (!task.entries.length || task.cancelRequested || error) {
      inCallback();
      return;
    }
    this.eventRouter_.sendDeleteEvent('PROGRESS', task);
    util.removeFileOrDirectory(
        task.entries[0],
        function() {
          this.eventRouter_.sendEntryChangedEvent(
              util.EntryChangedKind.DELETED, task.entries[0]);
          task.processedBytes += task.entrySize[task.entries[0].toURL()];
          task.entries.shift();
          deleteOneEntry(inCallback);
        }.bind(this),
        function(inError) {
          error = inError;
          inCallback();
        }.bind(this));
  }.bind(this);
  queue.run(deleteOneEntry);

  // Send an event and finish the async steps.
  queue.run(function(inCallback) {
    var reason;
    if (error)
      reason = 'ERROR';
    else if (task.cancelRequested)
      reason = 'CANCELED';
    else
      reason = 'SUCCESS';
    this.eventRouter_.sendDeleteEvent(reason, task);
    inCallback();
    callback();
  }.bind(this));
};

/**
 * Creates a zip file for the selection of files.
 *
 * @param {!DirectoryEntry} dirEntry The directory containing the selection.
 * @param {Array.<Entry>} selectionEntries The selected entries.
 */
FileOperationManager.prototype.zipSelection = function(
    dirEntry, selectionEntries) {
  var zipTask = new FileOperationManager.ZipTask(
      selectionEntries, dirEntry, dirEntry);
  zipTask.taskId = this.generateTaskId();
  zipTask.zip = true;
  this.eventRouter_.sendProgressEvent('BEGIN',
                                      zipTask.getStatus(),
                                      zipTask.taskId);
  zipTask.initialize(function() {
    this.copyTasks_.push(zipTask);
    if (this.copyTasks_.length == 1)
      this.serviceAllTasks_();
  }.bind(this));
};

/**
 * Generates new task ID.
 *
 * @return {string} New task ID.
 */
FileOperationManager.prototype.generateTaskId = function() {
  return 'file-operation-' + this.taskIdCounter_++;
};
