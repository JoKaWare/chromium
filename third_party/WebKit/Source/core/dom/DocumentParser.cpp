/*
 * Copyright (C) 2010 Google, Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/dom/DocumentParser.h"

#include "core/dom/Document.h"
#include "core/dom/DocumentParserClient.h"
#include "core/html/parser/TextResourceDecoder.h"
#include "wtf/Assertions.h"
#include <memory>

namespace blink {

DocumentParser::DocumentParser(Document* document)
    : m_state(ParsingState),
      m_documentWasLoadedAsPartOfNavigation(false),
      m_document(document) {
  DCHECK(document);
}

DocumentParser::~DocumentParser() {}

DEFINE_TRACE(DocumentParser) {
  visitor->trace(m_document);
  visitor->trace(m_clients);
}

void DocumentParser::setDecoder(std::unique_ptr<TextResourceDecoder>) {
  NOTREACHED();
}

TextResourceDecoder* DocumentParser::decoder() {
  return nullptr;
}

void DocumentParser::prepareToStopParsing() {
  DCHECK_EQ(m_state, ParsingState);
  m_state = StoppingState;
}

void DocumentParser::stopParsing() {
  m_state = StoppedState;

  // Clients may be removed while in the loop. Make a snapshot for iteration.
  HeapVector<Member<DocumentParserClient>> clientsSnapshot;
  copyToVector(m_clients, clientsSnapshot);

  for (DocumentParserClient* client : clientsSnapshot) {
    if (!m_clients.contains(client))
      continue;

    client->notifyParserStopped();
  }
}

void DocumentParser::detach() {
  m_state = DetachedState;
  m_document = nullptr;
}

void DocumentParser::suspendScheduledTasks() {}

void DocumentParser::resumeScheduledTasks() {}

void DocumentParser::addClient(DocumentParserClient* client) {
  m_clients.insert(client);
}

void DocumentParser::removeClient(DocumentParserClient* client) {
  m_clients.remove(client);
}

}  // namespace blink
