/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/timing/PerformanceTiming.h"

#include "bindings/core/v8/ScriptValue.h"
#include "bindings/core/v8/V8ObjectBuilder.h"
#include "core/dom/Document.h"
#include "core/dom/DocumentParserTiming.h"
#include "core/dom/DocumentTiming.h"
#include "core/frame/LocalFrame.h"
#include "core/loader/DocumentLoadTiming.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/FrameLoader.h"
#include "core/paint/PaintTiming.h"
#include "core/timing/PerformanceBase.h"
#include "platform/network/ResourceLoadTiming.h"
#include "platform/network/ResourceResponse.h"

namespace blink {

static unsigned long long toIntegerMilliseconds(double seconds)
{
    ASSERT(seconds >= 0);
    double clampedSeconds = PerformanceBase::clampTimeResolution(seconds);
    return static_cast<unsigned long long>(clampedSeconds * 1000.0);
}

static double toDoubleSeconds(unsigned long long integerMilliseconds)
{
    return integerMilliseconds / 1000.0;
}

PerformanceTiming::PerformanceTiming(LocalFrame* frame)
    : DOMWindowProperty(frame)
{
}

unsigned long long PerformanceTiming::navigationStart() const
{
    DocumentLoadTiming* timing = documentLoadTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->navigationStart());
}

unsigned long long PerformanceTiming::unloadEventStart() const
{
    DocumentLoadTiming* timing = documentLoadTiming();
    if (!timing)
        return 0;

    if (timing->hasCrossOriginRedirect() || !timing->hasSameOriginAsPreviousDocument())
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->unloadEventStart());
}

unsigned long long PerformanceTiming::unloadEventEnd() const
{
    DocumentLoadTiming* timing = documentLoadTiming();
    if (!timing)
        return 0;

    if (timing->hasCrossOriginRedirect() || !timing->hasSameOriginAsPreviousDocument())
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->unloadEventEnd());
}

unsigned long long PerformanceTiming::redirectStart() const
{
    DocumentLoadTiming* timing = documentLoadTiming();
    if (!timing)
        return 0;

    if (timing->hasCrossOriginRedirect())
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->redirectStart());
}

unsigned long long PerformanceTiming::redirectEnd() const
{
    DocumentLoadTiming* timing = documentLoadTiming();
    if (!timing)
        return 0;

    if (timing->hasCrossOriginRedirect())
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->redirectEnd());
}

unsigned long long PerformanceTiming::fetchStart() const
{
    DocumentLoadTiming* timing = documentLoadTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->fetchStart());
}

unsigned long long PerformanceTiming::domainLookupStart() const
{
    ResourceLoadTiming* timing = resourceLoadTiming();
    if (!timing)
        return fetchStart();

    // This will be zero when a DNS request is not performed.
    // Rather than exposing a special value that indicates no DNS, we "backfill" with fetchStart.
    double dnsStart = timing->dnsStart();
    if (dnsStart == 0.0)
        return fetchStart();

    return monotonicTimeToIntegerMilliseconds(dnsStart);
}

unsigned long long PerformanceTiming::domainLookupEnd() const
{
    ResourceLoadTiming* timing = resourceLoadTiming();
    if (!timing)
        return domainLookupStart();

    // This will be zero when a DNS request is not performed.
    // Rather than exposing a special value that indicates no DNS, we "backfill" with domainLookupStart.
    double dnsEnd = timing->dnsEnd();
    if (dnsEnd == 0.0)
        return domainLookupStart();

    return monotonicTimeToIntegerMilliseconds(dnsEnd);
}

unsigned long long PerformanceTiming::connectStart() const
{
    DocumentLoader* loader = documentLoader();
    if (!loader)
        return domainLookupEnd();

    ResourceLoadTiming* timing = loader->response().resourceLoadTiming();
    if (!timing)
        return domainLookupEnd();

    // connectStart will be zero when a network request is not made.
    // Rather than exposing a special value that indicates no new connection, we "backfill" with domainLookupEnd.
    double connectStart = timing->connectStart();
    if (connectStart == 0.0 || loader->response().connectionReused())
        return domainLookupEnd();

    // ResourceLoadTiming's connect phase includes DNS, however Navigation Timing's
    // connect phase should not. So if there is DNS time, trim it from the start.
    if (timing->dnsEnd() > 0.0 && timing->dnsEnd() > connectStart)
        connectStart = timing->dnsEnd();

    return monotonicTimeToIntegerMilliseconds(connectStart);
}

unsigned long long PerformanceTiming::connectEnd() const
{
    DocumentLoader* loader = documentLoader();
    if (!loader)
        return connectStart();

    ResourceLoadTiming* timing = loader->response().resourceLoadTiming();
    if (!timing)
        return connectStart();

    // connectEnd will be zero when a network request is not made.
    // Rather than exposing a special value that indicates no new connection, we "backfill" with connectStart.
    double connectEnd = timing->connectEnd();
    if (connectEnd == 0.0 || loader->response().connectionReused())
        return connectStart();

    return monotonicTimeToIntegerMilliseconds(connectEnd);
}

unsigned long long PerformanceTiming::secureConnectionStart() const
{
    DocumentLoader* loader = documentLoader();
    if (!loader)
        return 0;

    ResourceLoadTiming* timing = loader->response().resourceLoadTiming();
    if (!timing)
        return 0;

    double sslStart = timing->sslStart();
    if (sslStart == 0.0)
        return 0;

    return monotonicTimeToIntegerMilliseconds(sslStart);
}

unsigned long long PerformanceTiming::requestStart() const
{
    ResourceLoadTiming* timing = resourceLoadTiming();

    if (!timing || timing->sendStart() == 0.0)
        return connectEnd();

    return monotonicTimeToIntegerMilliseconds(timing->sendStart());
}

unsigned long long PerformanceTiming::responseStart() const
{
    ResourceLoadTiming* timing = resourceLoadTiming();
    if (!timing || timing->receiveHeadersEnd() == 0.0)
        return requestStart();

    // FIXME: Response start needs to be the time of the first received byte.
    // However, the ResourceLoadTiming API currently only supports the time
    // the last header byte was received. For many responses with reasonable
    // sized cookies, the HTTP headers fit into a single packet so this time
    // is basically equivalent. But for some responses, particularly those with
    // headers larger than a single packet, this time will be too late.
    return monotonicTimeToIntegerMilliseconds(timing->receiveHeadersEnd());
}

unsigned long long PerformanceTiming::responseEnd() const
{
    DocumentLoadTiming* timing = documentLoadTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->responseEnd());
}

unsigned long long PerformanceTiming::domLoading() const
{
    const DocumentTiming* timing = documentTiming();
    if (!timing)
        return fetchStart();

    return monotonicTimeToIntegerMilliseconds(timing->domLoading());
}

unsigned long long PerformanceTiming::domInteractive() const
{
    const DocumentTiming* timing = documentTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->domInteractive());
}

unsigned long long PerformanceTiming::domContentLoadedEventStart() const
{
    const DocumentTiming* timing = documentTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->domContentLoadedEventStart());
}

unsigned long long PerformanceTiming::domContentLoadedEventEnd() const
{
    const DocumentTiming* timing = documentTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->domContentLoadedEventEnd());
}

unsigned long long PerformanceTiming::domComplete() const
{
    const DocumentTiming* timing = documentTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->domComplete());
}

unsigned long long PerformanceTiming::loadEventStart() const
{
    DocumentLoadTiming* timing = documentLoadTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->loadEventStart());
}

unsigned long long PerformanceTiming::loadEventEnd() const
{
    DocumentLoadTiming* timing = documentLoadTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->loadEventEnd());
}

unsigned long long PerformanceTiming::firstLayout() const
{
    const DocumentTiming* timing = documentTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->firstLayout());
}

unsigned long long PerformanceTiming::firstPaint() const
{
    const PaintTiming* timing = paintTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->firstPaint());
}

unsigned long long PerformanceTiming::firstTextPaint() const
{
    const PaintTiming* timing = paintTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->firstTextPaint());
}

unsigned long long PerformanceTiming::firstImagePaint() const
{
    const PaintTiming* timing = paintTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->firstImagePaint());
}

unsigned long long PerformanceTiming::firstContentfulPaint() const
{
    const PaintTiming* timing = paintTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->firstContentfulPaint());
}

unsigned long long PerformanceTiming::firstMeaningfulPaint() const
{
    const PaintTiming* timing = paintTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->firstMeaningfulPaint());
}

unsigned long long PerformanceTiming::firstMeaningfulPaintCandidate() const
{
    const PaintTiming* timing = paintTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->firstMeaningfulPaintCandidate());
}

unsigned long long PerformanceTiming::parseStart() const
{
    const DocumentParserTiming* timing = documentParserTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->parserStart());
}

unsigned long long PerformanceTiming::parseStop() const
{
    const DocumentParserTiming* timing = documentParserTiming();
    if (!timing)
        return 0;

    return monotonicTimeToIntegerMilliseconds(timing->parserStop());
}

unsigned long long PerformanceTiming::parseBlockedOnScriptLoadDuration() const
{
    const DocumentParserTiming* timing = documentParserTiming();
    if (!timing)
        return 0;

    return toIntegerMilliseconds(timing->parserBlockedOnScriptLoadDuration());
}

unsigned long long PerformanceTiming::parseBlockedOnScriptLoadFromDocumentWriteDuration() const
{
    const DocumentParserTiming* timing = documentParserTiming();
    if (!timing)
        return 0;

    return toIntegerMilliseconds(timing->parserBlockedOnScriptLoadFromDocumentWriteDuration());
}

DocumentLoader* PerformanceTiming::documentLoader() const
{
    if (!m_frame)
        return nullptr;

    return m_frame->loader().documentLoader();
}

const DocumentTiming* PerformanceTiming::documentTiming() const
{
    if (!m_frame)
        return nullptr;

    Document* document = m_frame->document();
    if (!document)
        return nullptr;

    return &document->timing();
}

const PaintTiming* PerformanceTiming::paintTiming() const
{
    if (!m_frame)
        return nullptr;

    Document* document = m_frame->document();
    if (!document)
        return nullptr;

    return &PaintTiming::from(*document);
}

const DocumentParserTiming* PerformanceTiming::documentParserTiming() const
{
    if (!m_frame)
        return nullptr;

    Document* document = m_frame->document();
    if (!document)
        return nullptr;

    return &DocumentParserTiming::from(*document);
}

DocumentLoadTiming* PerformanceTiming::documentLoadTiming() const
{
    DocumentLoader* loader = documentLoader();
    if (!loader)
        return nullptr;

    return &loader->timing();
}

ResourceLoadTiming* PerformanceTiming::resourceLoadTiming() const
{
    DocumentLoader* loader = documentLoader();
    if (!loader)
        return nullptr;

    return loader->response().resourceLoadTiming();
}

ScriptValue PerformanceTiming::toJSONForBinding(ScriptState* scriptState) const
{
    V8ObjectBuilder result(scriptState);
    result.addNumber("navigationStart", navigationStart());
    result.addNumber("unloadEventStart", unloadEventStart());
    result.addNumber("unloadEventEnd", unloadEventEnd());
    result.addNumber("redirectStart", redirectStart());
    result.addNumber("redirectEnd", redirectEnd());
    result.addNumber("fetchStart", fetchStart());
    result.addNumber("domainLookupStart", domainLookupStart());
    result.addNumber("domainLookupEnd", domainLookupEnd());
    result.addNumber("connectStart", connectStart());
    result.addNumber("connectEnd", connectEnd());
    result.addNumber("secureConnectionStart", secureConnectionStart());
    result.addNumber("requestStart", requestStart());
    result.addNumber("responseStart", responseStart());
    result.addNumber("responseEnd", responseEnd());
    result.addNumber("domLoading", domLoading());
    result.addNumber("domInteractive", domInteractive());
    result.addNumber("domContentLoadedEventStart", domContentLoadedEventStart());
    result.addNumber("domContentLoadedEventEnd", domContentLoadedEventEnd());
    result.addNumber("domComplete", domComplete());
    result.addNumber("loadEventStart", loadEventStart());
    result.addNumber("loadEventEnd", loadEventEnd());
    return result.scriptValue();
}

unsigned long long PerformanceTiming::monotonicTimeToIntegerMilliseconds(double monotonicSeconds) const
{
    ASSERT(monotonicSeconds >= 0);
    const DocumentLoadTiming* timing = documentLoadTiming();
    if (!timing)
        return 0;

    return toIntegerMilliseconds(timing->monotonicTimeToPseudoWallTime(monotonicSeconds));
}

double PerformanceTiming::integerMillisecondsToMonotonicTime(unsigned long long integerMilliseconds) const
{
    const DocumentLoadTiming* timing = documentLoadTiming();
    if (!timing)
        return 0;

    return timing->pseudoWallTimeToMonotonicTime(toDoubleSeconds(integerMilliseconds));
}

DEFINE_TRACE(PerformanceTiming)
{
    DOMWindowProperty::trace(visitor);
}

} // namespace blink
