/*
 * Copyright (C) 2012, Intel Corporation. All rights reserved.
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

#include "modules/webaudio/AudioBasicInspectorNode.h"
#include "modules/webaudio/AudioNodeInput.h"
#include "modules/webaudio/AudioNodeOutput.h"
#include "modules/webaudio/BaseAudioContext.h"

namespace blink {

AudioBasicInspectorHandler::AudioBasicInspectorHandler(
    NodeType nodeType,
    AudioNode& node,
    float sampleRate,
    unsigned outputChannelCount)
    : AudioHandler(nodeType, node, sampleRate), m_needAutomaticPull(false) {
  addInput();
  addOutput(outputChannelCount);
}

// We override pullInputs() as an optimization allowing this node to take
// advantage of in-place processing, where the input is simply passed through
// unprocessed to the output.
// Note: this only applies if the input and output channel counts match.
void AudioBasicInspectorHandler::pullInputs(size_t framesToProcess) {
  // Render input stream - try to render directly into output bus for
  // pass-through processing where process() doesn't need to do anything...
  input(0).pull(output(0).bus(), framesToProcess);
}

AudioNode* AudioBasicInspectorNode::connect(AudioNode* destination,
                                            unsigned outputIndex,
                                            unsigned inputIndex,
                                            ExceptionState& exceptionState) {
  DCHECK(isMainThread());

  BaseAudioContext::AutoLocker locker(context());

  AudioNode::connect(destination, outputIndex, inputIndex, exceptionState);
  static_cast<AudioBasicInspectorHandler&>(handler()).updatePullStatus();

  return destination;
}

void AudioBasicInspectorNode::disconnect(unsigned outputIndex,
                                         ExceptionState& exceptionState) {
  DCHECK(isMainThread());

  BaseAudioContext::AutoLocker locker(context());

  AudioNode::disconnect(outputIndex, exceptionState);
  static_cast<AudioBasicInspectorHandler&>(handler()).updatePullStatus();
}

void AudioBasicInspectorHandler::checkNumberOfChannelsForInput(
    AudioNodeInput* input) {
  DCHECK(context()->isAudioThread());
  ASSERT(context()->isGraphOwner());

  DCHECK_EQ(input, &this->input(0));
  if (input != &this->input(0))
    return;

  unsigned numberOfChannels = input->numberOfChannels();

  if (numberOfChannels != output(0).numberOfChannels()) {
    // This will propagate the channel count to any nodes connected further
    // downstream in the graph.
    output(0).setNumberOfChannels(numberOfChannels);
  }

  AudioHandler::checkNumberOfChannelsForInput(input);

  updatePullStatus();
}

void AudioBasicInspectorHandler::updatePullStatus() {
  ASSERT(context()->isGraphOwner());

  if (output(0).isConnected()) {
    // When an AudioBasicInspectorNode is connected to a downstream node, it
    // will get pulled by the downstream node, thus remove it from the context's
    // automatic pull list.
    if (m_needAutomaticPull) {
      context()->deferredTaskHandler().removeAutomaticPullNode(this);
      m_needAutomaticPull = false;
    }
  } else {
    unsigned numberOfInputConnections = input(0).numberOfRenderingConnections();
    if (numberOfInputConnections && !m_needAutomaticPull) {
      // When an AudioBasicInspectorNode is not connected to any downstream node
      // while still connected from upstream node(s), add it to the context's
      // automatic pull list.
      context()->deferredTaskHandler().addAutomaticPullNode(this);
      m_needAutomaticPull = true;
    } else if (!numberOfInputConnections && m_needAutomaticPull) {
      // The AudioBasicInspectorNode is connected to nothing and is
      // not an AnalyserNode, remove it from the context's automatic
      // pull list.  AnalyserNode's need to be pulled even with no
      // inputs so that the internal state gets updated to hold the
      // right time and FFT data.
      context()->deferredTaskHandler().removeAutomaticPullNode(this);
      m_needAutomaticPull = false;
    }
  }
}

}  // namespace blink
