/*
 * Copyright (c) 2014, Opera Software ASA. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Opera Software ASA nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/timing/SharedWorkerPerformance.h"

#include "bindings/core/v8/ScriptState.h"
#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/loader/DocumentLoadTiming.h"
#include "core/loader/DocumentLoader.h"
#include "core/workers/SharedWorker.h"

namespace blink {

SharedWorkerPerformance::SharedWorkerPerformance(SharedWorker& sharedWorker)
    : Supplement<SharedWorker>(sharedWorker),
      m_timeOrigin(monotonicallyIncreasingTime()) {}

const char* SharedWorkerPerformance::supplementName() {
  return "SharedWorkerPerformance";
}

SharedWorkerPerformance& SharedWorkerPerformance::from(
    SharedWorker& sharedWorker) {
  SharedWorkerPerformance* supplement = static_cast<SharedWorkerPerformance*>(
      Supplement<SharedWorker>::from(sharedWorker, supplementName()));
  if (!supplement) {
    supplement = new SharedWorkerPerformance(sharedWorker);
    provideTo(sharedWorker, supplementName(), supplement);
  }
  return *supplement;
}

double SharedWorkerPerformance::workerStart(ScriptState* scriptState,
                                            SharedWorker& sharedWorker) {
  return SharedWorkerPerformance::from(sharedWorker)
      .getWorkerStart(scriptState->getExecutionContext(), sharedWorker);
}

double SharedWorkerPerformance::getWorkerStart(ExecutionContext* context,
                                               SharedWorker&) const {
  ASSERT(context);
  ASSERT(context->isDocument());
  Document* document = toDocument(context);
  if (!document->loader())
    return 0;

  double navigationStart = document->loader()->timing().navigationStart();
  return m_timeOrigin - navigationStart;
}

}  // namespace blink
