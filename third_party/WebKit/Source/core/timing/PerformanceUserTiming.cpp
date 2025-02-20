/*
 * Copyright (C) 2012 Intel Inc. All rights reserved.
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

#include "core/timing/PerformanceUserTiming.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/dom/ExceptionCode.h"
#include "core/timing/PerformanceBase.h"
#include "core/timing/PerformanceMark.h"
#include "core/timing/PerformanceMeasure.h"
#include "platform/Histogram.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "public/platform/Platform.h"
#include "wtf/text/StringHash.h"

namespace blink {

namespace {

using RestrictedKeyMap = HashMap<String, NavigationTimingFunction>;

RestrictedKeyMap* createRestrictedKeyMap() {
  RestrictedKeyMap* map = new RestrictedKeyMap();
  map->insert("navigationStart", &PerformanceTiming::navigationStart);
  map->insert("unloadEventStart", &PerformanceTiming::unloadEventStart);
  map->insert("unloadEventEnd", &PerformanceTiming::unloadEventEnd);
  map->insert("redirectStart", &PerformanceTiming::redirectStart);
  map->insert("redirectEnd", &PerformanceTiming::redirectEnd);
  map->insert("fetchStart", &PerformanceTiming::fetchStart);
  map->insert("domainLookupStart", &PerformanceTiming::domainLookupStart);
  map->insert("domainLookupEnd", &PerformanceTiming::domainLookupEnd);
  map->insert("connectStart", &PerformanceTiming::connectStart);
  map->insert("connectEnd", &PerformanceTiming::connectEnd);
  map->insert("secureConnectionStart",
              &PerformanceTiming::secureConnectionStart);
  map->insert("requestStart", &PerformanceTiming::requestStart);
  map->insert("responseStart", &PerformanceTiming::responseStart);
  map->insert("responseEnd", &PerformanceTiming::responseEnd);
  map->insert("domLoading", &PerformanceTiming::domLoading);
  map->insert("domInteractive", &PerformanceTiming::domInteractive);
  map->insert("domContentLoadedEventStart",
              &PerformanceTiming::domContentLoadedEventStart);
  map->insert("domContentLoadedEventEnd",
              &PerformanceTiming::domContentLoadedEventEnd);
  map->insert("domComplete", &PerformanceTiming::domComplete);
  map->insert("loadEventStart", &PerformanceTiming::loadEventStart);
  map->insert("loadEventEnd", &PerformanceTiming::loadEventEnd);
  return map;
}

const RestrictedKeyMap& restrictedKeyMap() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(RestrictedKeyMap, map,
                                  createRestrictedKeyMap());
  return map;
}

}  // namespace

UserTiming::UserTiming(PerformanceBase& performance)
    : m_performance(&performance) {}

static void insertPerformanceEntry(PerformanceEntryMap& performanceEntryMap,
                                   PerformanceEntry& entry) {
  PerformanceEntryMap::iterator it = performanceEntryMap.find(entry.name());
  if (it != performanceEntryMap.end()) {
    it->value.push_back(&entry);
  } else {
    PerformanceEntryVector vector(1);
    vector[0] = Member<PerformanceEntry>(entry);
    performanceEntryMap.set(entry.name(), vector);
  }
}

static void clearPeformanceEntries(PerformanceEntryMap& performanceEntryMap,
                                   const String& name) {
  if (name.isNull()) {
    performanceEntryMap.clear();
    return;
  }

  if (performanceEntryMap.contains(name))
    performanceEntryMap.erase(name);
}

PerformanceEntry* UserTiming::mark(const String& markName,
                                   ExceptionState& exceptionState) {
  if (restrictedKeyMap().contains(markName)) {
    exceptionState.throwDOMException(
        SyntaxError, "'" + markName +
                         "' is part of the PerformanceTiming interface, and "
                         "cannot be used as a mark name.");
    return nullptr;
  }

  TRACE_EVENT_COPY_MARK("blink.user_timing", markName.utf8().data());
  double startTime = m_performance->now();
  PerformanceEntry* entry = PerformanceMark::create(markName, startTime);
  insertPerformanceEntry(m_marksMap, *entry);
  DEFINE_THREAD_SAFE_STATIC_LOCAL(
      CustomCountHistogram, userTimingMarkHistogram,
      new CustomCountHistogram("PLT.UserTiming_Mark", 0, 600000, 100));
  userTimingMarkHistogram.count(static_cast<int>(startTime));
  return entry;
}

void UserTiming::clearMarks(const String& markName) {
  clearPeformanceEntries(m_marksMap, markName);
}

double UserTiming::findExistingMarkStartTime(const String& markName,
                                             ExceptionState& exceptionState) {
  if (m_marksMap.contains(markName))
    return m_marksMap.get(markName).back()->startTime();

  if (restrictedKeyMap().contains(markName) && m_performance->timing()) {
    double value = static_cast<double>(
        (m_performance->timing()->*(restrictedKeyMap().get(markName)))());
    if (!value) {
      exceptionState.throwDOMException(
          InvalidAccessError, "'" + markName +
                                  "' is empty: either the event hasn't "
                                  "happened yet, or it would provide "
                                  "cross-origin timing information.");
      return 0.0;
    }
    return value - m_performance->timing()->navigationStart();
  }

  exceptionState.throwDOMException(
      SyntaxError, "The mark '" + markName + "' does not exist.");
  return 0.0;
}

PerformanceEntry* UserTiming::measure(const String& measureName,
                                      const String& startMark,
                                      const String& endMark,
                                      ExceptionState& exceptionState) {
  double startTime = 0.0;
  double endTime = 0.0;

  if (startMark.isNull()) {
    endTime = m_performance->now();
  } else if (endMark.isNull()) {
    endTime = m_performance->now();
    startTime = findExistingMarkStartTime(startMark, exceptionState);
    if (exceptionState.hadException())
      return nullptr;
  } else {
    endTime = findExistingMarkStartTime(endMark, exceptionState);
    if (exceptionState.hadException())
      return nullptr;
    startTime = findExistingMarkStartTime(startMark, exceptionState);
    if (exceptionState.hadException())
      return nullptr;
  }

  // User timing events are stored as integer milliseconds from the start of
  // navigation, whereas trace events accept double seconds based off of
  // CurrentTime::monotonicallyIncreasingTime().
  double startTimeMonotonic = m_performance->timeOrigin() + startTime / 1000.0;
  double endTimeMonotonic = m_performance->timeOrigin() + endTime / 1000.0;

  TRACE_EVENT_COPY_NESTABLE_ASYNC_BEGIN_WITH_TIMESTAMP0(
      "blink.user_timing", measureName.utf8().data(),
      WTF::StringHash::hash(measureName),
      TraceEvent::toTraceTimestamp(startTimeMonotonic));
  TRACE_EVENT_COPY_NESTABLE_ASYNC_END_WITH_TIMESTAMP0(
      "blink.user_timing", measureName.utf8().data(),
      WTF::StringHash::hash(measureName),
      TraceEvent::toTraceTimestamp(endTimeMonotonic));

  PerformanceEntry* entry =
      PerformanceMeasure::create(measureName, startTime, endTime);
  insertPerformanceEntry(m_measuresMap, *entry);
  if (endTime >= startTime) {
    DEFINE_THREAD_SAFE_STATIC_LOCAL(
        CustomCountHistogram, measureDurationHistogram,
        new CustomCountHistogram("PLT.UserTiming_MeasureDuration", 0, 600000,
                                 100));
    measureDurationHistogram.count(static_cast<int>(endTime - startTime));
  }
  return entry;
}

void UserTiming::clearMeasures(const String& measureName) {
  clearPeformanceEntries(m_measuresMap, measureName);
}

static PerformanceEntryVector convertToEntrySequence(
    const PerformanceEntryMap& performanceEntryMap) {
  PerformanceEntryVector entries;

  for (const auto& entry : performanceEntryMap)
    entries.appendVector(entry.value);

  return entries;
}

static PerformanceEntryVector getEntrySequenceByName(
    const PerformanceEntryMap& performanceEntryMap,
    const String& name) {
  PerformanceEntryVector entries;

  PerformanceEntryMap::const_iterator it = performanceEntryMap.find(name);
  if (it != performanceEntryMap.end())
    entries.appendVector(it->value);

  return entries;
}

PerformanceEntryVector UserTiming::getMarks() const {
  return convertToEntrySequence(m_marksMap);
}

PerformanceEntryVector UserTiming::getMarks(const String& name) const {
  return getEntrySequenceByName(m_marksMap, name);
}

PerformanceEntryVector UserTiming::getMeasures() const {
  return convertToEntrySequence(m_measuresMap);
}

PerformanceEntryVector UserTiming::getMeasures(const String& name) const {
  return getEntrySequenceByName(m_measuresMap, name);
}

DEFINE_TRACE(UserTiming) {
  visitor->trace(m_performance);
  visitor->trace(m_marksMap);
  visitor->trace(m_measuresMap);
}

}  // namespace blink
