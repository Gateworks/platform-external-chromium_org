// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_COPRESENCE_HANDLERS_DIRECTIVE_HANDLER_H_
#define COMPONENTS_COPRESENCE_HANDLERS_DIRECTIVE_HANDLER_H_

#include <string>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "components/copresence/handlers/audio/audio_directive_handler.h"
#include "components/copresence/mediums/audio/audio_manager.h"

namespace copresence {

class Directive;

// The directive handler manages transmit and receive directives
// given to it by the manager.
class DirectiveHandler {
 public:
  DirectiveHandler();
  virtual ~DirectiveHandler();

  // Initialize the |audio_handler_| with the appropriate callbacks.
  // This function must be called before any others.
  // TODO(ckehoe): Instead of this, use a static Create() method
  //               and make the constructor private.
  virtual void Initialize(const AudioManager::DecodeSamplesCallback& decode_cb,
                          const AudioManager::EncodeTokenCallback& encode_cb);

  // Adds a directive to handle.
  virtual void AddDirective(const copresence::Directive& directive);
  // Removes any directives associated with the given operation id.
  virtual void RemoveDirectives(const std::string& op_id);

  const std::string GetCurrentAudioToken(AudioType type) const;

 private:
  scoped_ptr<AudioDirectiveHandler> audio_handler_;

  DISALLOW_COPY_AND_ASSIGN(DirectiveHandler);
};

}  // namespace copresence

#endif  // COMPONENTS_COPRESENCE_HANDLERS_DIRECTIVE_HANDLER_H_
