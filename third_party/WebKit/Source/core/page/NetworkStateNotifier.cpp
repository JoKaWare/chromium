/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
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

#include "core/page/NetworkStateNotifier.h"

#include "core/dom/ExecutionContext.h"
#include "core/dom/ExecutionContextTask.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/page/Page.h"
#include "wtf/Assertions.h"
#include "wtf/Functional.h"
#include "wtf/PtrUtil.h"
#include "wtf/StdLibExtras.h"
#include "wtf/Threading.h"

namespace blink {

NetworkStateNotifier& networkStateNotifier() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(NetworkStateNotifier, networkStateNotifier,
                                  new NetworkStateNotifier);
  return networkStateNotifier;
}

NetworkStateNotifier::ScopedNotifier::ScopedNotifier(
    NetworkStateNotifier& notifier)
    : m_notifier(notifier) {
  DCHECK(isMainThread());
  m_before =
      m_notifier.m_hasOverride ? m_notifier.m_override : m_notifier.m_state;
}

NetworkStateNotifier::ScopedNotifier::~ScopedNotifier() {
  DCHECK(isMainThread());
  const NetworkState& after =
      m_notifier.m_hasOverride ? m_notifier.m_override : m_notifier.m_state;
  if ((after.type != m_before.type ||
       after.maxBandwidthMbps != m_before.maxBandwidthMbps) &&
      m_before.connectionInitialized)
    m_notifier.notifyObservers(after.type, after.maxBandwidthMbps);
  if (after.onLine != m_before.onLine && m_before.onLineInitialized)
    Page::networkStateChanged(after.onLine);
}

void NetworkStateNotifier::setOnLine(bool onLine) {
  DCHECK(isMainThread());
  ScopedNotifier notifier(*this);
  {
    MutexLocker locker(m_mutex);
    m_state.onLineInitialized = true;
    m_state.onLine = onLine;
  }
}

void NetworkStateNotifier::setWebConnection(WebConnectionType type,
                                            double maxBandwidthMbps) {
  DCHECK(isMainThread());
  ScopedNotifier notifier(*this);
  {
    MutexLocker locker(m_mutex);
    m_state.connectionInitialized = true;
    m_state.type = type;
    m_state.maxBandwidthMbps = maxBandwidthMbps;
  }
}

void NetworkStateNotifier::addObserver(NetworkStateObserver* observer,
                                       ExecutionContext* context) {
  ASSERT(context->isContextThread());
  ASSERT(observer);

  MutexLocker locker(m_mutex);
  ObserverListMap::AddResult result = m_observers.insert(context, nullptr);
  if (result.isNewEntry)
    result.storedValue->value = WTF::wrapUnique(new ObserverList);

  ASSERT(result.storedValue->value->observers.find(observer) == kNotFound);
  result.storedValue->value->observers.push_back(observer);
}

void NetworkStateNotifier::removeObserver(NetworkStateObserver* observer,
                                          ExecutionContext* context) {
  ASSERT(context->isContextThread());
  ASSERT(observer);

  ObserverList* observerList = lockAndFindObserverList(context);
  if (!observerList)
    return;

  Vector<NetworkStateObserver*>& observers = observerList->observers;
  size_t index = observers.find(observer);
  if (index != kNotFound) {
    observers[index] = 0;
    observerList->zeroedObservers.push_back(index);
  }

  if (!observerList->iterating && !observerList->zeroedObservers.isEmpty())
    collectZeroedObservers(observerList, context);
}

void NetworkStateNotifier::setOverride(bool onLine,
                                       WebConnectionType type,
                                       double maxBandwidthMbps) {
  DCHECK(isMainThread());
  ScopedNotifier notifier(*this);
  {
    MutexLocker locker(m_mutex);
    m_hasOverride = true;
    m_override.onLineInitialized = true;
    m_override.onLine = onLine;
    m_override.connectionInitialized = true;
    m_override.type = type;
    m_override.maxBandwidthMbps = maxBandwidthMbps;
  }
}

void NetworkStateNotifier::clearOverride() {
  DCHECK(isMainThread());
  ScopedNotifier notifier(*this);
  {
    MutexLocker locker(m_mutex);
    m_hasOverride = false;
  }
}

void NetworkStateNotifier::notifyObservers(WebConnectionType type,
                                           double maxBandwidthMbps) {
  DCHECK(isMainThread());
  for (const auto& entry : m_observers) {
    ExecutionContext* context = entry.key;
    context->postTask(
        TaskType::Networking, BLINK_FROM_HERE,
        createCrossThreadTask(
            &NetworkStateNotifier::notifyObserversOfConnectionChangeOnContext,
            crossThreadUnretained(this), type, maxBandwidthMbps));
  }
}

void NetworkStateNotifier::notifyObserversOfConnectionChangeOnContext(
    WebConnectionType type,
    double maxBandwidthMbps,
    ExecutionContext* context) {
  ObserverList* observerList = lockAndFindObserverList(context);

  // The context could have been removed before the notification task got to
  // run.
  if (!observerList)
    return;

  ASSERT(context->isContextThread());

  observerList->iterating = true;

  for (size_t i = 0; i < observerList->observers.size(); ++i) {
    // Observers removed during iteration are zeroed out, skip them.
    if (observerList->observers[i])
      observerList->observers[i]->connectionChange(type, maxBandwidthMbps);
  }

  observerList->iterating = false;

  if (!observerList->zeroedObservers.isEmpty())
    collectZeroedObservers(observerList, context);
}

NetworkStateNotifier::ObserverList*
NetworkStateNotifier::lockAndFindObserverList(ExecutionContext* context) {
  MutexLocker locker(m_mutex);
  ObserverListMap::iterator it = m_observers.find(context);
  return it == m_observers.end() ? nullptr : it->value.get();
}

void NetworkStateNotifier::collectZeroedObservers(ObserverList* list,
                                                  ExecutionContext* context) {
  ASSERT(context->isContextThread());
  ASSERT(!list->iterating);

  // If any observers were removed during the iteration they will have
  // 0 values, clean them up.
  for (size_t i = 0; i < list->zeroedObservers.size(); ++i)
    list->observers.remove(list->zeroedObservers[i]);

  list->zeroedObservers.clear();

  if (list->observers.isEmpty()) {
    MutexLocker locker(m_mutex);
    m_observers.erase(context);  // deletes list
  }
}

}  // namespace blink
