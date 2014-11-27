// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/copresence/handlers/directive_handler.h"

#include "base/bind.h"
#include "base/guid.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "components/copresence/handlers/audio/audio_directive_handler_impl.h"
#include "components/copresence/proto/data.pb.h"

namespace {

const int kMaxUnlabeledDirectiveTtl = 60000;  // 1 minute

}  // namespace

namespace copresence {

// Public functions

DirectiveHandler::DirectiveHandler(
    scoped_ptr<AudioDirectiveHandler> audio_handler)
    : audio_handler_(audio_handler.Pass()), is_started_(false) {
}

DirectiveHandler::~DirectiveHandler() {}

void DirectiveHandler::Start(WhispernetClient* whispernet_client,
                             const TokensCallback& tokens_cb) {
  audio_handler_->Initialize(whispernet_client, tokens_cb);
  DVLOG(2) << "Directive handler starting";

  is_started_ = true;

  // Run all the queued directives.
  for (const auto& op_id : pending_directives_) {
    for (const Directive& directive : op_id.second)
      StartDirective(op_id.first, directive);
  }
  pending_directives_.clear();
}

void DirectiveHandler::AddDirective(const Directive& original_directive) {
  // We may need to modify the directive's TTL.
  Directive directive(original_directive);

  // We only handle transmit and receive directives.
  // WiFi and BLE scans aren't implemented.
  DCHECK_EQ(directive.instruction_type(), TOKEN);

  std::string op_id = directive.published_message_id();
  if (op_id.empty())
    op_id = directive.subscription_id();

  // GCM directives will not have a publish or subscribe ID populated.
  if (op_id.empty()) {
    op_id = base::GenerateGUID();
    DVLOG(3) << "No operation associated with directive. Setting op id to "
             << op_id;

    // The app can't cancel these directives, so make sure they're not too long.
    if (directive.ttl_millis() > kMaxUnlabeledDirectiveTtl) {
      DVLOG(2) << "Cutting TTL of unlabeled directive from "
               << directive.ttl_millis() << " down to "
               << kMaxUnlabeledDirectiveTtl << " milliseconds";
      directive.set_ttl_millis(kMaxUnlabeledDirectiveTtl);
    }
  }

  if (!is_started_) {
    pending_directives_[op_id].push_back(directive);
  } else {
    StartDirective(op_id, directive);
  }
}

void DirectiveHandler::RemoveDirectives(const std::string& op_id) {
  // If whispernet_client_ is null, audio_handler_ hasn't been Initialized.
  if (is_started_) {
    audio_handler_->RemoveInstructions(op_id);
  } else {
    pending_directives_.erase(op_id);
  }
}

const std::string DirectiveHandler::GetCurrentAudioToken(AudioType type) const {
  // If whispernet_client_ is null, audio_handler_ hasn't been Initialized.
  return is_started_ ? audio_handler_->PlayingToken(type) : "";
}

bool DirectiveHandler::IsAudioTokenHeard(AudioType type) const {
  return is_started_ ? audio_handler_->IsPlayingTokenHeard(type) : false;
}

// Private functions

void DirectiveHandler::StartDirective(const std::string& op_id,
                                      const Directive& directive) {
  DCHECK(is_started_);
  const TokenInstruction& ti = directive.token_instruction();
  if (ti.medium() == AUDIO_ULTRASOUND_PASSBAND ||
      ti.medium() == AUDIO_AUDIBLE_DTMF) {
    audio_handler_->AddInstruction(
        ti, op_id, base::TimeDelta::FromMilliseconds(directive.ttl_millis()));
  } else {
    // We should only get audio directives.
    NOTREACHED() << "Received directive for unimplemented medium "
                 << ti.medium();
  }
}

}  // namespace copresence
