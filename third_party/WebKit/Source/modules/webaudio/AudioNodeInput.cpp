/*
 * Copyright (C) 2010, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include "modules/webaudio/AudioNodeInput.h"
#include "modules/webaudio/AudioNodeOutput.h"
#include "wtf/PtrUtil.h"
#include <algorithm>
#include <memory>

namespace blink {

inline AudioNodeInput::AudioNodeInput(AudioHandler& handler)
    : AudioSummingJunction(handler.context()->deferredTaskHandler()),
      m_handler(handler) {
  // Set to mono by default.
  m_internalSummingBus =
      AudioBus::create(1, AudioUtilities::kRenderQuantumFrames);
}

std::unique_ptr<AudioNodeInput> AudioNodeInput::create(AudioHandler& handler) {
  return WTF::wrapUnique(new AudioNodeInput(handler));
}

void AudioNodeInput::connect(AudioNodeOutput& output) {
  ASSERT(deferredTaskHandler().isGraphOwner());

  // Check if we're already connected to this output.
  if (m_outputs.contains(&output))
    return;

  output.addInput(*this);
  m_outputs.insert(&output);
  changedOutputs();
}

void AudioNodeInput::disconnect(AudioNodeOutput& output) {
  ASSERT(deferredTaskHandler().isGraphOwner());

  // First try to disconnect from "active" connections.
  if (m_outputs.contains(&output)) {
    m_outputs.remove(&output);
    changedOutputs();
    output.removeInput(*this);
    // Note: it's important to return immediately after removeInput() calls
    // since the node may be deleted.
    return;
  }

  // Otherwise, try to disconnect from disabled connections.
  if (m_disabledOutputs.contains(&output)) {
    m_disabledOutputs.remove(&output);
    output.removeInput(*this);
    // Note: it's important to return immediately after all removeInput() calls
    // since the node may be deleted.
    return;
  }

  ASSERT_NOT_REACHED();
}

void AudioNodeInput::disable(AudioNodeOutput& output) {
  ASSERT(deferredTaskHandler().isGraphOwner());
  DCHECK(m_outputs.contains(&output));

  m_disabledOutputs.insert(&output);
  m_outputs.remove(&output);
  changedOutputs();

  // Propagate disabled state to outputs.
  handler().disableOutputsIfNecessary();
}

void AudioNodeInput::enable(AudioNodeOutput& output) {
  ASSERT(deferredTaskHandler().isGraphOwner());

  // Move output from disabled list to active list.
  m_outputs.insert(&output);
  if (m_disabledOutputs.size() > 0) {
    DCHECK(m_disabledOutputs.contains(&output));
    m_disabledOutputs.remove(&output);
  }
  changedOutputs();

  // Propagate enabled state to outputs.
  handler().enableOutputsIfNecessary();
}

void AudioNodeInput::didUpdate() {
  handler().checkNumberOfChannelsForInput(this);
}

void AudioNodeInput::updateInternalBus() {
  DCHECK(deferredTaskHandler().isAudioThread());
  ASSERT(deferredTaskHandler().isGraphOwner());

  unsigned numberOfInputChannels = numberOfChannels();

  if (numberOfInputChannels == m_internalSummingBus->numberOfChannels())
    return;

  m_internalSummingBus = AudioBus::create(numberOfInputChannels,
                                          AudioUtilities::kRenderQuantumFrames);
}

unsigned AudioNodeInput::numberOfChannels() const {
  AudioHandler::ChannelCountMode mode = handler().internalChannelCountMode();
  if (mode == AudioHandler::Explicit)
    return handler().channelCount();

  // Find the number of channels of the connection with the largest number of
  // channels.
  unsigned maxChannels = 1;  // one channel is the minimum allowed

  for (AudioNodeOutput* output : m_outputs) {
    // Use output()->numberOfChannels() instead of
    // output->bus()->numberOfChannels(), because the calling of
    // AudioNodeOutput::bus() is not safe here.
    maxChannels = std::max(maxChannels, output->numberOfChannels());
  }

  if (mode == AudioHandler::ClampedMax)
    maxChannels =
        std::min(maxChannels, static_cast<unsigned>(handler().channelCount()));

  return maxChannels;
}

AudioBus* AudioNodeInput::bus() {
  DCHECK(deferredTaskHandler().isAudioThread());

  // Handle single connection specially to allow for in-place processing.
  if (numberOfRenderingConnections() == 1 &&
      handler().internalChannelCountMode() == AudioHandler::Max)
    return renderingOutput(0)->bus();

  // Multiple connections case or complex ChannelCountMode (or no connections).
  return internalSummingBus();
}

AudioBus* AudioNodeInput::internalSummingBus() {
  DCHECK(deferredTaskHandler().isAudioThread());

  return m_internalSummingBus.get();
}

void AudioNodeInput::sumAllConnections(AudioBus* summingBus,
                                       size_t framesToProcess) {
  DCHECK(deferredTaskHandler().isAudioThread());

  // We shouldn't be calling this method if there's only one connection, since
  // it's less efficient.
  //    DCHECK(numberOfRenderingConnections() > 1 ||
  //        handler().internalChannelCountMode() != AudioHandler::Max);

  DCHECK(summingBus);
  if (!summingBus)
    return;

  summingBus->zero();

  AudioBus::ChannelInterpretation interpretation =
      handler().internalChannelInterpretation();

  for (unsigned i = 0; i < numberOfRenderingConnections(); ++i) {
    AudioNodeOutput* output = renderingOutput(i);
    DCHECK(output);

    // Render audio from this output.
    AudioBus* connectionBus = output->pull(0, framesToProcess);

    // Sum, with unity-gain.
    summingBus->sumFrom(*connectionBus, interpretation);
  }
}

AudioBus* AudioNodeInput::pull(AudioBus* inPlaceBus, size_t framesToProcess) {
  DCHECK(deferredTaskHandler().isAudioThread());

  // Handle single connection case.
  if (numberOfRenderingConnections() == 1 &&
      handler().internalChannelCountMode() == AudioHandler::Max) {
    // The output will optimize processing using inPlaceBus if it's able.
    AudioNodeOutput* output = this->renderingOutput(0);
    return output->pull(inPlaceBus, framesToProcess);
  }

  AudioBus* internalSummingBus = this->internalSummingBus();

  if (!numberOfRenderingConnections()) {
    // At least, generate silence if we're not connected to anything.
    // FIXME: if we wanted to get fancy, we could propagate a 'silent hint' here
    // to optimize the downstream graph processing.
    internalSummingBus->zero();
    return internalSummingBus;
  }

  // Handle multiple connections case.
  sumAllConnections(internalSummingBus, framesToProcess);

  return internalSummingBus;
}

}  // namespace blink
