/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2003, 2004, 2006, 2007, 2008 Apple Inc.  All right reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef BidiResolver_h
#define BidiResolver_h

#include "platform/text/BidiCharacterRun.h"
#include "platform/text/BidiContext.h"
#include "platform/text/BidiRunList.h"
#include "platform/text/TextDirection.h"
#include "wtf/Allocator.h"
#include "wtf/HashMap.h"
#include "wtf/Noncopyable.h"
#include "wtf/PassRefPtr.h"
#include "wtf/Vector.h"

namespace blink {

class LayoutObject;

template <class Iterator> class MidpointState final {
    DISALLOW_NEW();
public:
    MidpointState()
    {
        reset();
    }

    void reset()
    {
        m_numMidpoints = 0;
        m_currentMidpoint = 0;
        m_betweenMidpoints = false;
    }

    void startIgnoringSpaces(const Iterator& midpoint)
    {
        ASSERT(!(m_numMidpoints % 2));
        addMidpoint(midpoint);
    }

    void stopIgnoringSpaces(const Iterator& midpoint)
    {
        ASSERT(m_numMidpoints % 2);
        addMidpoint(midpoint);
    }

    // Adding a pair of midpoints before a character will split it out into a new line box.
    void ensureCharacterGetsLineBox(Iterator& textParagraphSeparator)
    {
        startIgnoringSpaces(Iterator(0, textParagraphSeparator.getLineLayoutItem(), textParagraphSeparator.offset() - 1));
        stopIgnoringSpaces(Iterator(0, textParagraphSeparator.getLineLayoutItem(), textParagraphSeparator.offset()));
    }

    void checkMidpoints(Iterator& lBreak)
    {
        // Check to see if our last midpoint is a start point beyond the line break. If so,
        // shave it off the list, and shave off a trailing space if the previous end point doesn't
        // preserve whitespace.
        if (lBreak.getLineLayoutItem() && m_numMidpoints && !(m_numMidpoints % 2)) {
            Iterator* midpointsIterator = m_midpoints.data();
            Iterator& endpoint = midpointsIterator[m_numMidpoints - 2];
            const Iterator& startpoint = midpointsIterator[m_numMidpoints - 1];
            Iterator currpoint = endpoint;
            while (!currpoint.atEnd() && currpoint != startpoint && currpoint != lBreak)
                currpoint.increment();
            if (currpoint == lBreak) {
                // We hit the line break before the start point. Shave off the start point.
                m_numMidpoints--;
                if (endpoint.getLineLayoutItem().style()->collapseWhiteSpace() && endpoint.getLineLayoutItem().isText())
                    endpoint.setOffset(endpoint.offset() - 1);
            }
        }
    }

    Vector<Iterator>& midpoints() { return m_midpoints; }
    const unsigned& numMidpoints() const { return m_numMidpoints; }
    const unsigned& currentMidpoint() const { return m_currentMidpoint; }
    void incrementCurrentMidpoint() { m_currentMidpoint++; }
    const bool& betweenMidpoints() const { return m_betweenMidpoints; }
    void setBetweenMidpoints(bool betweenMidpoint) { m_betweenMidpoints = betweenMidpoint; }
private:
    // The goal is to reuse the line state across multiple
    // lines so we just keep an array around for midpoints and never clear it across multiple
    // lines. We track the number of items and position using the two other variables.
    Vector<Iterator> m_midpoints;
    unsigned m_numMidpoints;
    unsigned m_currentMidpoint;
    bool m_betweenMidpoints;

    void addMidpoint(const Iterator& midpoint)
    {
        if (m_midpoints.size() <= m_numMidpoints)
            m_midpoints.grow(m_numMidpoints + 10);

        Iterator* midpointsIterator = m_midpoints.data();
        midpointsIterator[m_numMidpoints++] = midpoint;
    }
};

// The BidiStatus at a given position (typically the end of a line) can
// be cached and then used to restart bidi resolution at that position.
struct BidiStatus final {
    DISALLOW_NEW();
    BidiStatus()
        : eor(WTF::Unicode::OtherNeutral)
        , lastStrong(WTF::Unicode::OtherNeutral)
        , lastNonNeutral(WTF::Unicode::OtherNeutral)
        , last(WTF::Unicode::OtherNeutral)
    {
    }

    // Creates a BidiStatus representing a new paragraph root with a default direction.
    // Uses TextDirection as it only has two possibilities instead of WTF::Unicode::CharDirection which has 19.
    BidiStatus(TextDirection textDirection, bool isOverride)
    {
        WTF::Unicode::CharDirection direction = textDirection == LTR ? WTF::Unicode::LeftToRight : WTF::Unicode::RightToLeft;
        eor = lastStrong = last = lastNonNeutral = direction;
        context = BidiContext::create(textDirection == LTR ? 0 : 1, direction, isOverride);
    }

    BidiStatus(WTF::Unicode::CharDirection eorDir,
        WTF::Unicode::CharDirection lastStrongDir,
        WTF::Unicode::CharDirection lastNonNeutralDir,
        WTF::Unicode::CharDirection lastDir,
        PassRefPtr<BidiContext> bidiContext)
        : eor(eorDir)
        , lastStrong(lastStrongDir)
        , lastNonNeutral(lastNonNeutralDir)
        , last(lastDir)
        , context(bidiContext)
    {
    }

    // Creates a BidiStatus for Isolates (RLI/LRI).
    // The rule X5a ans X5b of UAX#9: http://unicode.org/reports/tr9/#X5a
    static BidiStatus createForIsolate(TextDirection textDirection, bool isOverride, unsigned char level)
    {
        WTF::Unicode::CharDirection direction;
        if (textDirection == RTL) {
            level = nextGreaterOddLevel(level);
            direction = WTF::Unicode::RightToLeft;
        } else {
            level = nextGreaterEvenLevel(level);
            direction = WTF::Unicode::LeftToRight;
        }
        RefPtr<BidiContext> context = BidiContext::create(level, direction, isOverride, FromStyleOrDOM);

        // This copies BidiStatus and may churn the ref on BidiContext.
        // I doubt it matters.
        return BidiStatus(direction, direction, direction, direction, context.release());
    }

    WTF::Unicode::CharDirection eor;
    WTF::Unicode::CharDirection lastStrong;
    WTF::Unicode::CharDirection lastNonNeutral;
    WTF::Unicode::CharDirection last;
    RefPtr<BidiContext> context;
};

class BidiEmbedding final {
    DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();
public:
    BidiEmbedding(WTF::Unicode::CharDirection direction, BidiEmbeddingSource source)
        : m_direction(direction)
        , m_source(source)
    {
    }

    WTF::Unicode::CharDirection direction() const { return m_direction; }
    BidiEmbeddingSource source() const { return m_source; }
private:
    WTF::Unicode::CharDirection m_direction;
    BidiEmbeddingSource m_source;
};

inline bool operator==(const BidiStatus& status1, const BidiStatus& status2)
{
    return status1.eor == status2.eor && status1.last == status2.last && status1.lastStrong == status2.lastStrong && *(status1.context) == *(status2.context);
}

inline bool operator!=(const BidiStatus& status1, const BidiStatus& status2)
{
    return !(status1 == status2);
}

enum VisualDirectionOverride {
    NoVisualOverride,
    VisualLeftToRightOverride,
    VisualRightToLeftOverride
};

template <class Run>
class BidiBracketData {
public:
    BidiBracketData(UChar32 character,
        Run* run,
        WTF::Unicode::CharDirection lastStrong,
        WTF::Unicode::CharDirection lastDir,
        PassRefPtr<BidiContext> context)
        : m_character(character)
        , m_run(run)
        , m_lastStrong(lastStrong)
        , m_lastDir(lastDir)
        , m_context(context)
        , m_containsEmbeddingStrong(false)
        , m_containsOpositeEmbeddingStrong(false)
    {
    }

    UChar32 character() const { return m_character; }
    Run* run() const { return m_run; }
    WTF::Unicode::CharDirection lastStrong() const { return m_lastStrong; }
    WTF::Unicode::CharDirection lastDir() const { return m_lastDir; }
    BidiContext* context() const { return m_context.get(); }
    bool lastStrongOpositesEmbeddingStrong() const
    {
        if (!m_context)
            return false;
        switch (m_lastStrong) {
        case WTF::Unicode::LeftToRight:
            return m_context->dir() == WTF::Unicode::RightToLeft;
            break;
        case WTF::Unicode::RightToLeft:
        case WTF::Unicode::RightToLeftArabic:
            return m_context->dir() == WTF::Unicode::LeftToRight;
            break;
        default:
            return false;
        }
    }
    void addContainedStrong(UChar32 character)
    {
        if (!m_context)
            return;
        switch (WTF::Unicode::direction(character)) {
        case WTF::Unicode::LeftToRight:
            if (m_context->dir() == WTF::Unicode::LeftToRight)
                m_containsEmbeddingStrong = true;
            else
                m_containsOpositeEmbeddingStrong = true;
            break;
        case WTF::Unicode::RightToLeft:
        case WTF::Unicode::RightToLeftArabic:
        // In http://unicode.org/reports/tr9/#N0,
        // bidirectional types EuropeanNumber and ArabicNumber are treated as RTL,
        // Storage : | ABC | ( | - | 123 | ) |
        // Display : (123-)ABC
        // If bidirectional types EuropeanNumber is treated as LTR,
        // Storage : | ABC | ( | - | 123 | ) |
        // Display : ABC(-123)
        case WTF::Unicode::EuropeanNumber:
        case WTF::Unicode::ArabicNumber:
            if (m_context->dir() == WTF::Unicode::RightToLeft)
                m_containsEmbeddingStrong = true;
            else
                m_containsOpositeEmbeddingStrong = true;
        default:
            break;
        }
    }
    WTF::Unicode::CharDirection direction() {
        if (m_containsEmbeddingStrong)
            return m_context->dir(); // N0b
        if (m_containsOpositeEmbeddingStrong) {
            return (lastStrongOpositesEmbeddingStrong()) ?
                m_lastStrong : m_context->dir(); // N0c
        }
        return WTF::Unicode::OtherNeutral; // N0d
    }
private:
    UChar32 m_character;
    Run* m_run;
    WTF::Unicode::CharDirection m_lastStrong;
    WTF::Unicode::CharDirection m_lastDir;
    RefPtr<BidiContext> m_context;
    bool m_containsEmbeddingStrong;
    bool m_containsOpositeEmbeddingStrong;
};

template <class Run> class BidiNeutralsSequence {
public:
    BidiNeutralsSequence(WTF::Unicode::CharDirection previousDirection)
        : m_firstRun(0)
        , m_lastRun(0)
        , m_previousDirection(previousDirection)
        , m_nextDirection(WTF::Unicode::OtherNeutral)
    {
    }

    WTF::Unicode::CharDirection resolveDirectionForN1()
    {
        switch (m_previousDirection) {
        case WTF::Unicode::LeftToRight:
            if (m_nextDirection == WTF::Unicode::LeftToRight)
                return WTF::Unicode::LeftToRight;
            break;
        case WTF::Unicode::RightToLeft:
        case WTF::Unicode::EuropeanNumber:
        case WTF::Unicode::ArabicNumber:
        case WTF::Unicode::RightToLeftArabic:
            switch (m_nextDirection) {
            case WTF::Unicode::RightToLeft:
            case WTF::Unicode::EuropeanNumber:
            case WTF::Unicode::ArabicNumber:
            case WTF::Unicode::RightToLeftArabic:
                return WTF::Unicode::RightToLeft;
            default:
                break;
            }
            break;
        default:
            break;
        }
        return WTF::Unicode::OtherNeutral;
    }

    bool resolve(bool force = false)
    {
        if (!force && (!m_firstRun || !m_lastRun
            || m_previousDirection == WTF::Unicode::OtherNeutral
            || m_nextDirection == WTF::Unicode::OtherNeutral
            || !m_pendingBrackets.isEmpty()))
            return false;

        WTF::Unicode::CharDirection resolvedDirection = resolveDirectionForN1();

        Run* current = m_firstRun;
        while (current) {
            if ((current->direction() == LTR && resolvedDirection == WTF::Unicode::RightToLeft)
                || (current->direction() == RTL && resolvedDirection == WTF::Unicode::LeftToRight))
                current->m_level++;

            if (current == m_lastRun)
                break;
            current = current->next();
        }
        return true;
    }

    void appendRun(Run* run, bool isBracket)
    {
        if (isBracket)
            m_pendingBrackets.append(run);
        if (!m_firstRun)
            m_firstRun = run;
        m_lastRun = run;
    }

    void setBracketAsNeutral(Run* run)
    {
        size_t pos = m_pendingBrackets.find(run);
        if (pos != kNotFound)
            m_pendingBrackets.remove(pos);
    }

    void containsUnresolvedBracket(Run* run)
    {
        return m_pendingBrackets.contains(run);
    }

    bool isEmpty() const
    {
        return !m_firstRun;
    }

    WTF::Unicode::CharDirection previousDirection() const
    {
        return m_previousDirection;
    }

    WTF::Unicode::CharDirection nextDirection() const
    {
        return m_nextDirection;
    }

    void setNextDirection(WTF::Unicode::CharDirection nextDirection)
    {
        m_nextDirection = nextDirection;
    }

    Run* lastRun() const
    {
        return m_lastRun;
    }

    bool resolveBracket(Run* run, BidiNeutralsSequence<Run>& newLeftSide, TextDirection direction)
    {
        if (!run)
            return false;
        WTF::Unicode::CharDirection bracketDirection = (direction == LTR) ? WTF::Unicode::LeftToRight : WTF::Unicode::RightToLeft;
        newLeftSide.m_nextDirection = bracketDirection;
        for (size_t i = m_pendingBrackets.size(); i > 0; --i) {
            if (m_pendingBrackets[i - 1] == run) {
                Vector<int> bracketsToRemove;

                if (m_firstRun != run) {
                    Run* current = m_firstRun;
                    while (current != run) {
                        size_t pendingBracketPos = m_pendingBrackets.find(current);
                        newLeftSide.m_previousDirection = m_previousDirection;
                        newLeftSide.appendRun(current, pendingBracketPos != kNotFound);
                        current = current->next();
                        if (!current)
                            break;
                    }
                }

                m_previousDirection = bracketDirection;

                if (run->direction() != direction)
                    run->m_level++;

                m_pendingBrackets.remove(0, i);
                if (run == m_lastRun) {
                    m_firstRun = m_lastRun = 0;
                } else {
                    m_firstRun = run->next();
                    if (!run->next())
                        m_lastRun = 0;
                }

                return true;
            }
        }
        return false;
    }

    void setAllBracketsAsNeutral()
    {
        m_pendingBrackets.clear();
    }

private:
    Run* m_firstRun;
    Run* m_lastRun;
    Vector<Run*> m_pendingBrackets;
    WTF::Unicode::CharDirection m_previousDirection;
    WTF::Unicode::CharDirection m_nextDirection;
};

class NoIsolatedRun {
};

// BidiResolver is WebKit's implementation of the Unicode Bidi Algorithm
// http://unicode.org/reports/tr9
template <class Iterator, class Run, class IsolatedRun = NoIsolatedRun> class BidiResolver final {
    DISALLOW_NEW();
    WTF_MAKE_NONCOPYABLE(BidiResolver);
public:
    BidiResolver()
        : m_direction(WTF::Unicode::OtherNeutral)
        , m_previousDirection(WTF::Unicode::OtherNeutral)
        , m_reachedEndOfLine(false)
        , m_emptyRun(true)
        , m_nestedIsolateCount(0)
        , m_trailingSpaceRun(0)
    {
    }

#if ENABLE(ASSERT)
    ~BidiResolver();
#endif

    const Iterator& position() const { return m_current; }
    Iterator& position() { return m_current; }
    void setPositionIgnoringNestedIsolates(const Iterator& position) { m_current = position; }
    void setPosition(const Iterator& position, unsigned nestedIsolatedCount)
    {
        m_current = position;
        m_nestedIsolateCount = nestedIsolatedCount;
    }

    BidiContext* context() const { return m_status.context.get(); }
    void setContext(PassRefPtr<BidiContext> c) { m_status.context = c; }

    void setLastDir(WTF::Unicode::CharDirection lastDir) { m_status.last = lastDir; }
    void setLastStrongDir(WTF::Unicode::CharDirection lastStrongDir) { m_status.lastStrong = lastStrongDir; }
    void setEorDir(WTF::Unicode::CharDirection eorDir) { m_status.eor = eorDir; }

    WTF::Unicode::CharDirection dir() const { return m_direction; }
    void setDir(WTF::Unicode::CharDirection d) { m_direction = d; }

    const BidiStatus& status() const { return m_status; }
    void setStatus(const BidiStatus s)
    {
        ASSERT(s.context);
        m_status = s;
        m_paragraphDirectionality = s.context->dir() == WTF::Unicode::LeftToRight ? LTR : RTL;
    }

    MidpointState<Iterator>& midpointState() { return m_midpointState; }

    // The current algorithm handles nested isolates one layer of nesting at a time.
    // But when we layout each isolated span, we will walk into (and ignore) all
    // child isolated spans.
    void enterIsolate() { m_nestedIsolateCount++; }
    void exitIsolate() { ASSERT(m_nestedIsolateCount >= 1); m_nestedIsolateCount--; }
    bool inIsolate() const { return m_nestedIsolateCount; }

    void embed(WTF::Unicode::CharDirection, BidiEmbeddingSource);
    bool commitExplicitEmbedding(BidiRunList<Run>&);

    void createBidiRunsForLine(const Iterator& end, VisualDirectionOverride = NoVisualOverride, bool hardLineBreak = false, bool reorderRuns = true);

    BidiRunList<Run>& runs() { return m_runs; }

    // FIXME: This used to be part of deleteRuns() but was a layering violation.
    // It's unclear if this is still needed.
    void markCurrentRunEmpty() { m_emptyRun = true; }

    Vector<IsolatedRun>& isolatedRuns() { return m_isolatedRuns; }

    bool isEndOfLine(const Iterator& end) { return m_current == end || m_current.atEnd(); }

    TextDirection determineParagraphDirectionality(bool* hasStrongDirectionality = 0)
    {
        bool breakOnParagraph = true;
        return determineDirectionalityInternal(breakOnParagraph, hasStrongDirectionality);
    }
    TextDirection determineDirectionality(bool* hasStrongDirectionality = 0)
    {
        bool breakOnParagraph = false;
        return determineDirectionalityInternal(breakOnParagraph, hasStrongDirectionality);
    }

    void setMidpointStateForIsolatedRun(Run&, const MidpointState<Iterator>&);
    MidpointState<Iterator> midpointStateForIsolatedRun(Run&);

    Iterator endOfLine() const { return m_endOfLine; }

    Run* trailingSpaceRun() const { return m_trailingSpaceRun; }

protected:
    void increment() { m_current.increment(); }
    // FIXME: Instead of InlineBidiResolvers subclassing this method, we should
    // pass in some sort of Traits object which knows how to create runs for appending.
    void appendRun(BidiRunList<Run>&);
    void appendNeutralRun(BidiRunList<Run>&);

    Run* addTrailingRun(BidiRunList<Run>&, int, int, Run*, BidiContext*, TextDirection) const { return 0; }
    Iterator m_current;
    // sor and eor are "start of run" and "end of run" respectively and correpond
    // to abreviations used in UBA spec: http://unicode.org/reports/tr9/#BD7
    Iterator m_sor; // Points to the first character in the current run.
    Iterator m_eor; // Points to the last character in the current run.
    Iterator m_last;
    BidiStatus m_status;
    WTF::Unicode::CharDirection m_direction;
    WTF::Unicode::CharDirection m_previousDirection;
    // m_endOfRunAtEndOfLine is "the position last eor in the end of line"
    Iterator m_endOfRunAtEndOfLine;
    Iterator m_endOfLine;
    bool m_reachedEndOfLine;
    Iterator m_lastBeforeET; // Before a EuropeanNumberTerminator
    bool m_emptyRun;

    // FIXME: This should not belong to the resolver, but rather be passed
    // into createBidiRunsForLine by the caller.
    BidiRunList<Run> m_runs;

    MidpointState<Iterator> m_midpointState;

    unsigned m_nestedIsolateCount;
    Vector<IsolatedRun> m_isolatedRuns;
    Run* m_trailingSpaceRun;
    TextDirection m_paragraphDirectionality;

private:
    void raiseExplicitEmbeddingLevel(BidiRunList<Run>&, WTF::Unicode::CharDirection from, WTF::Unicode::CharDirection to);
    void lowerExplicitEmbeddingLevel(BidiRunList<Run>&, WTF::Unicode::CharDirection from);
    void checkDirectionInLowerRaiseEmbeddingLevel();

    void updateStatusLastFromCurrentDirection(WTF::Unicode::CharDirection);
    void reorderRunsFromLevels(BidiRunList<Run>&) const;

    bool needsToApplyL1Rule(BidiRunList<Run>&) { return false; }
    int findFirstTrailingSpaceAtRun(Run*) { return 0; }
    // http://www.unicode.org/reports/tr9/#L1
    void applyL1Rule(BidiRunList<Run>&);

    TextDirection determineDirectionalityInternal(bool breakOnParagraph, bool* hasStrongDirectionality);

    void pushCurrentBracketOpen();
    void tryPopCurrentBracketClose();
    void addBracketContainedStrong();

    Vector<BidiEmbedding, 8> m_currentExplicitEmbeddingSequence;
    HashMap<Run *, MidpointState<Iterator>> m_midpointStateForIsolatedRun;

    // list of (bracket opening, position iterator
    Vector<BidiBracketData<Run>> m_unclosedBrackets;
    // list of pending neutrals to resolve
    Vector<BidiNeutralsSequence<Run>> m_unresolvedNeutralsSequences;
};

#if ENABLE(ASSERT)
template <class Iterator, class Run, class IsolatedRun>
BidiResolver<Iterator, Run, IsolatedRun>::~BidiResolver()
{
    // The owner of this resolver should have handled the isolated runs.
    ASSERT(m_isolatedRuns.isEmpty());
    ASSERT(m_unresolvedNeutralsSequences.isEmpty());
    ASSERT(!m_runs.runCount());
}
#endif

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::appendRun(BidiRunList<Run>& runs)
{
    if (!m_emptyRun && !m_eor.atEnd()) {
        unsigned startOffset = m_sor.offset();
        unsigned endOffset = m_eor.offset();

        if (!m_endOfRunAtEndOfLine.atEnd() && endOffset >= m_endOfRunAtEndOfLine.offset()) {
            m_reachedEndOfLine = true;
            endOffset = m_endOfRunAtEndOfLine.offset();
        }

        // m_eor and m_endOfRunAtEndOfLine are inclusive while BidiRun's stop is
        // exclusive so offset needs to be increased by one.
        endOffset += 1;

        // Append BidiRun objects, at most 64K chars at a time, until all
        // text between |startOffset| and |endOffset| is represented.
        while (startOffset < endOffset) {
            unsigned end = endOffset;
            const int limit = USHRT_MAX; // InlineTextBox stores text length as unsigned short.
            if (end - startOffset > limit)
                end = startOffset + limit;
            if (!WTF::Unicode::directionIsNeutral(m_direction)
                && !m_unresolvedNeutralsSequences.isEmpty()
                && WTF::Unicode::directionIsNeutral(m_unresolvedNeutralsSequences.last().nextDirection())) {
                m_unresolvedNeutralsSequences.last().setNextDirection(m_direction);
                if (m_unresolvedNeutralsSequences.last().resolve())
                    m_unresolvedNeutralsSequences.removeLast();
            }
            runs.addRun(new Run(startOffset, end, context(), m_direction));
            if (!WTF::Unicode::directionIsNeutral(m_direction))
                m_status.lastNonNeutral = m_direction;
            startOffset = end;
        }

        m_eor.increment();
        m_sor = m_eor;
    }

    m_direction = WTF::Unicode::OtherNeutral;
    m_status.eor = WTF::Unicode::OtherNeutral;
}

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::appendNeutralRun(BidiRunList<Run>& runs)
{
    if (m_eor != m_last
        && m_status.eor != WTF::Unicode::OtherNeutral)
        appendRun(runs);

    m_eor = m_last;
    m_direction = WTF::Unicode::OtherNeutral;

    if (m_unresolvedNeutralsSequences.isEmpty()
        || !WTF::Unicode::directionIsNeutral(m_unresolvedNeutralsSequences.last().nextDirection())) {
        m_unresolvedNeutralsSequences.append(BidiNeutralsSequence<Run>(m_status.lastNonNeutral));
    }
    appendRun(runs);
    switch (WTF::Unicode::pairedBracketType(m_last.current())) {
    case WTF::Unicode::PairedBracketOpen:
        m_unresolvedNeutralsSequences.last().appendRun(runs.lastRun(), true);
        pushCurrentBracketOpen();
        break;
    case WTF::Unicode::PairedBracketClose:
        m_unresolvedNeutralsSequences.last().appendRun(runs.lastRun(), true);
        tryPopCurrentBracketClose();
        break;
    default:
        m_unresolvedNeutralsSequences.last().appendRun(runs.lastRun(), false);
        break;
    }
}

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::embed(WTF::Unicode::CharDirection dir, BidiEmbeddingSource source)
{
    // Isolated spans compute base directionality during their own UBA run.
    // Do not insert fake embed characters once we enter an isolated span.
    ASSERT(!inIsolate());
    using namespace WTF::Unicode;

    ASSERT(dir == PopDirectionalFormat || dir == LeftToRightEmbedding || dir == LeftToRightOverride || dir == RightToLeftEmbedding || dir == RightToLeftOverride);
    m_currentExplicitEmbeddingSequence.append(BidiEmbedding(dir, source));
}

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::checkDirectionInLowerRaiseEmbeddingLevel()
{
    using namespace WTF::Unicode;

    ASSERT(m_status.eor != OtherNeutral || m_eor.atEnd());
    ASSERT(m_status.last != NonSpacingMark
        && m_status.last != BoundaryNeutral
        && m_status.last != RightToLeftEmbedding
        && m_status.last != LeftToRightEmbedding
        && m_status.last != RightToLeftOverride
        && m_status.last != LeftToRightOverride
        && m_status.last != PopDirectionalFormat);
    if (WTF::Unicode::directionIsNeutral(m_direction))
        m_direction = m_status.lastStrong == LeftToRight ? LeftToRight : RightToLeft;
}

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::lowerExplicitEmbeddingLevel(BidiRunList<Run>& runs, WTF::Unicode::CharDirection from)
{
    using namespace WTF::Unicode;

    if (!m_emptyRun && m_eor != m_last) {
        checkDirectionInLowerRaiseEmbeddingLevel();
        // bidi.sor ... bidi.eor ... bidi.last eor; need to append the bidi.sor-bidi.eor run or extend it through bidi.last
        if (from == LeftToRight) {
            // bidi.sor ... bidi.eor ... bidi.last L
            if (m_status.eor == EuropeanNumber) {
                if (m_status.lastStrong != LeftToRight) {
                    m_direction = EuropeanNumber;
                    appendRun(runs);
                }
            } else if (m_status.eor == ArabicNumber) {
                m_direction = ArabicNumber;
                appendRun(runs);
            } else if (m_status.lastStrong != LeftToRight) {
                appendRun(runs);
                m_direction = LeftToRight;
            }
        } else if (m_status.eor == EuropeanNumber || m_status.eor == ArabicNumber || m_status.lastStrong == LeftToRight) {
            appendRun(runs);
            m_direction = RightToLeft;
        }
        m_eor = m_last;
    }

    appendRun(runs);
    m_emptyRun = true;

    // sor for the new run is determined by the higher level (rule X10)
    setLastDir(from);
    setLastStrongDir(from);
    m_eor = Iterator();
}

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::raiseExplicitEmbeddingLevel(BidiRunList<Run>& runs, WTF::Unicode::CharDirection from, WTF::Unicode::CharDirection to)
{
    using namespace WTF::Unicode;

    if (m_status.last == OtherNeutral) {
        appendNeutralRun(runs);
    } else {
        if (!m_emptyRun && m_eor != m_last) {
            checkDirectionInLowerRaiseEmbeddingLevel();
            // bidi.sor ... bidi.eor ... bidi.last eor; need to append the bidi.sor-bidi.eor run or extend it through bidi.last
            if (to == LeftToRight) {
                // bidi.sor ... bidi.eor ... bidi.last L
                if (m_status.eor == EuropeanNumber) {
                    if (m_status.lastStrong != LeftToRight) {
                        m_direction = EuropeanNumber;
                        appendRun(runs);
                    }
                } else if (m_status.eor == ArabicNumber) {
                    m_direction = ArabicNumber;
                    appendRun(runs);
                } else if (m_status.lastStrong != LeftToRight && from == LeftToRight) {
                    appendRun(runs);
                    m_direction = LeftToRight;
                }
            } else if (m_status.eor == ArabicNumber
                || (m_status.eor == EuropeanNumber && (m_status.lastStrong != LeftToRight || from == RightToLeft))
                || (m_status.eor != EuropeanNumber && m_status.lastStrong == LeftToRight && from == RightToLeft)) {
                appendRun(runs);
                m_direction = RightToLeft;
            }
            m_eor = m_last;
        }
        appendRun(runs);
    }
    m_emptyRun = true;

    setLastDir(to);
    setLastStrongDir(to);
    m_eor = Iterator();
}

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::applyL1Rule(BidiRunList<Run>& runs)
{
    ASSERT(runs.runCount());
    if (!needsToApplyL1Rule(runs))
        return;

    Run* trailingSpaceRun = runs.logicallyLastRun();

    int firstSpace = findFirstTrailingSpaceAtRun(trailingSpaceRun);
    if (firstSpace == trailingSpaceRun->stop())
        return;

    bool shouldReorder = trailingSpaceRun != (m_paragraphDirectionality == LTR ? runs.lastRun() : runs.firstRun());
    if (firstSpace != trailingSpaceRun->start()) {
        BidiContext* baseContext = context();
        while (BidiContext* parent = baseContext->parent())
            baseContext = parent;

        m_trailingSpaceRun = addTrailingRun(runs, firstSpace, trailingSpaceRun->m_stop, trailingSpaceRun, baseContext, m_paragraphDirectionality);
        ASSERT(m_trailingSpaceRun);
        trailingSpaceRun->m_stop = firstSpace;
        return;
    }
    if (!shouldReorder) {
        m_trailingSpaceRun = trailingSpaceRun;
        return;
    }

    if (m_paragraphDirectionality == LTR) {
        runs.moveRunToEnd(trailingSpaceRun);
        trailingSpaceRun->m_level = 0;
    } else {
        runs.moveRunToBeginning(trailingSpaceRun);
        trailingSpaceRun->m_level = 1;
    }
    m_trailingSpaceRun = trailingSpaceRun;
}

template <class Iterator, class Run, class IsolatedRun>
bool BidiResolver<Iterator, Run, IsolatedRun>::commitExplicitEmbedding(BidiRunList<Run>& runs)
{
    // When we're "inIsolate()" we're resolving the parent context which
    // ignores (skips over) the isolated content, including embedding levels.
    // We should never accrue embedding levels while skipping over isolated content.
    ASSERT(!inIsolate() || m_currentExplicitEmbeddingSequence.isEmpty());

    using namespace WTF::Unicode;

    unsigned char fromLevel = context()->level();
    RefPtr<BidiContext> toContext = context();

    for (size_t i = 0; i < m_currentExplicitEmbeddingSequence.size(); ++i) {
        BidiEmbedding embedding = m_currentExplicitEmbeddingSequence[i];
        if (embedding.direction() == PopDirectionalFormat) {
            if (BidiContext* parentContext = toContext->parent())
                toContext = parentContext;
        } else {
            CharDirection direction = (embedding.direction() == RightToLeftEmbedding || embedding.direction() == RightToLeftOverride) ? RightToLeft : LeftToRight;
            bool override = embedding.direction() == LeftToRightOverride || embedding.direction() == RightToLeftOverride;
            unsigned char level = toContext->level();
            if (direction == RightToLeft)
                level = nextGreaterOddLevel(level);
            else
                level = nextGreaterEvenLevel(level);
            if (level < BidiContext::kMaxLevel)
                toContext = BidiContext::create(level, direction, override, embedding.source(), toContext.get());
        }
    }

    unsigned char toLevel = toContext->level();

    if (toLevel > fromLevel) {
        raiseExplicitEmbeddingLevel(runs, fromLevel % 2 ? RightToLeft : LeftToRight, toLevel % 2 ? RightToLeft : LeftToRight);
    } else if (toLevel < fromLevel) {
        lowerExplicitEmbeddingLevel(runs, fromLevel % 2 ? RightToLeft : LeftToRight);
        // clean unclosed brackets of this context
        while (!m_unclosedBrackets.isEmpty()
            && m_unclosedBrackets.last().context()
            && m_unclosedBrackets.last().context()->level() > toLevel)
            m_unclosedBrackets.removeLast();
    }

    setContext(toContext);

    m_currentExplicitEmbeddingSequence.clear();

    return fromLevel != toLevel;
}

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::pushCurrentBracketOpen()
{
    m_unclosedBrackets.append(BidiBracketData<Run>(m_last.current(),
        m_runs.lastRun(), m_status.lastStrong, m_status.last,
        m_status.context));
    addBracketContainedStrong();
}

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::addBracketContainedStrong()
{
    if (m_current.atEnd())
        return;
    for (size_t i = m_unclosedBrackets.size() ; i > 0; --i) {
        if (m_unclosedBrackets[i - 1].context() != context())
            break;
        m_unclosedBrackets[i - 1].addContainedStrong(m_current.current());
    }
}

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::tryPopCurrentBracketClose()
{
    // 1. traverse stack to find bracket to pop
    bool openBracketFound = false;
    size_t openBracket;
    for (size_t i = m_unclosedBrackets.size(); i > 0; --i) {
        if (m_unclosedBrackets[i - 1].context() != context())
            break;
        if (m_last.current() == WTF::Unicode::toPairedBracket(m_unclosedBrackets[i - 1].character())) {
            openBracketFound = true;
            openBracket = i - 1;
            break;
        }
    }
    if (!openBracketFound) {
        m_unresolvedNeutralsSequences.last().setBracketAsNeutral(m_runs.lastRun());
        return;
    }

    // 2. pop all the elements on top of the found element
    if ((openBracket + 1) < m_unclosedBrackets.size()) {
        while (m_unclosedBrackets.size() > openBracket + 1) {
            for (size_t j = m_unresolvedNeutralsSequences.size() ; j > 0 ; --j) {
                m_unresolvedNeutralsSequences[j-1].setBracketAsNeutral(m_unclosedBrackets.last().run());
                if (m_unresolvedNeutralsSequences[j-1].resolve())
                    m_unresolvedNeutralsSequences.remove(j-1);
            }
            m_unclosedBrackets.removeLast();
        }
    }

    // 3. recreate runs
    // we know we are processing runs for the same context (though we need to be careful as there could be
    // other runs in the middle
    // 3.1 decide brackets direction
    // 3.2 fix runs accordingly

    // 3.1
    WTF::Unicode::CharDirection bracketsDirection = m_unclosedBrackets[openBracket].direction();

    // 3.2 fix opening bracket run
    if (bracketsDirection != WTF::Unicode::OtherNeutral) {

        // Update opening bracket direction
        TextDirection bracketRunDirection = LTR;
        bool bracketRunDirectionSet = false;
        switch (bracketsDirection) {
        case WTF::Unicode::LeftToRight:
            bracketRunDirection = LTR;
            bracketRunDirectionSet = true;
            m_status.lastNonNeutral = bracketsDirection;
            break;
        case WTF::Unicode::RightToLeft:
        case WTF::Unicode::RightToLeftArabic:
            bracketRunDirection = RTL;
            bracketRunDirectionSet = true;
            m_status.lastNonNeutral = bracketsDirection;
            break;
        default:
            break;
        }
        if (bracketRunDirectionSet) {
            // Resolve bracket on closing neutral
            BidiNeutralsSequence<Run> beforeClosingBracketNeutrals(m_unresolvedNeutralsSequences.last().previousDirection());
            if (m_runs.runCount())
                m_unresolvedNeutralsSequences.last().resolveBracket(m_runs.lastRun(), beforeClosingBracketNeutrals, bracketRunDirection);
            if (!beforeClosingBracketNeutrals.isEmpty() && !beforeClosingBracketNeutrals.resolve())
                m_unresolvedNeutralsSequences.insert(m_unresolvedNeutralsSequences.size() - 1, beforeClosingBracketNeutrals);
            if (m_unresolvedNeutralsSequences.last().isEmpty())
                m_unresolvedNeutralsSequences.removeLast();

            // Resolve bracket on opening neutral
            for (size_t j = m_unresolvedNeutralsSequences.size() ; j > 0 ; --j) {
                BidiNeutralsSequence<Run> beforeOpeningBracketNeutrals(m_unresolvedNeutralsSequences[j - 1].previousDirection());
                if (m_unresolvedNeutralsSequences[j - 1].resolveBracket(m_unclosedBrackets[openBracket].run(), beforeOpeningBracketNeutrals, bracketRunDirection)) {
                    if (m_unresolvedNeutralsSequences[j - 1].isEmpty() || m_unresolvedNeutralsSequences[j - 1].resolve())
                        m_unresolvedNeutralsSequences.remove(j - 1);
                    if (!beforeOpeningBracketNeutrals.isEmpty() && !beforeOpeningBracketNeutrals.resolve())
                        m_unresolvedNeutralsSequences.insert(j - 1, beforeOpeningBracketNeutrals);
                    break;
                }
            }
        }

        // Force m_direction to still be emulated bracket direction, to apply N1/N2 properly
        m_direction = bracketsDirection;
        m_status.lastStrong = m_unclosedBrackets[openBracket].lastStrong();
    } else {
        for (size_t j = m_unresolvedNeutralsSequences.size() ; j > 0 ; --j) {
            m_unresolvedNeutralsSequences[j - 1].setBracketAsNeutral(m_runs.lastRun());
            m_unresolvedNeutralsSequences[j - 1].setBracketAsNeutral(m_unclosedBrackets.last().run());
            if (m_unresolvedNeutralsSequences[j - 1].resolve())
                m_unresolvedNeutralsSequences.remove(j - 1);
        }
    }

    // 4. pop found bracket
    m_unclosedBrackets.removeLast();
}

template <class Iterator, class Run, class IsolatedRun>
inline void BidiResolver<Iterator, Run, IsolatedRun>::updateStatusLastFromCurrentDirection(WTF::Unicode::CharDirection dirCurrent)
{
    using namespace WTF::Unicode;
    switch (dirCurrent) {
    case EuropeanNumberTerminator:
        if (m_status.last != EuropeanNumber)
            m_status.last = EuropeanNumberTerminator;
        break;
    case EuropeanNumberSeparator:
    case CommonNumberSeparator:
    case SegmentSeparator:
    case WhiteSpaceNeutral:
    case OtherNeutral:
        switch (m_status.last) {
        case LeftToRight:
        case RightToLeft:
        case RightToLeftArabic:
        case EuropeanNumber:
        case ArabicNumber:
            m_status.last = dirCurrent;
            break;
        default:
            m_status.last = OtherNeutral;
        }
        break;
    case NonSpacingMark:
    case BoundaryNeutral:
    case RightToLeftEmbedding:
    case LeftToRightEmbedding:
    case RightToLeftOverride:
    case LeftToRightOverride:
    case PopDirectionalFormat:
        // ignore these
        break;
    case EuropeanNumber:
        if (m_status.lastStrong == RightToLeftArabic) {
            m_status.last = ArabicNumber;
            break;
        }
    default:
        m_status.last = dirCurrent;
    }
}

template <class Iterator, class Run, class IsolatedRun>
inline void BidiResolver<Iterator, Run, IsolatedRun>::reorderRunsFromLevels(BidiRunList<Run>& runs) const
{
    unsigned char levelLow = BidiContext::kMaxLevel;
    unsigned char levelHigh = 0;
    for (Run* run = runs.firstRun(); run; run = run->next()) {
        levelHigh = std::max(run->level(), levelHigh);
        levelLow = std::min(run->level(), levelLow);
    }

    // This implements reordering of the line (L2 according to Bidi spec):
    // http://unicode.org/reports/tr9/#L2
    // L2. From the highest level found in the text to the lowest odd level on each line,
    // reverse any contiguous sequence of characters that are at that level or higher.

    // Reversing is only done up to the lowest odd level.
    if (!(levelLow % 2))
        levelLow++;

    unsigned count = runs.runCount() - 1;

    while (levelHigh >= levelLow) {
        unsigned i = 0;
        Run* run = runs.firstRun();
        while (i < count) {
            for (;i < count && run && run->level() < levelHigh; i++)
                run = run->next();
            unsigned start = i;
            for (;i <= count && run && run->level() >= levelHigh; i++)
                run = run->next();
            unsigned end = i - 1;
            runs.reverseRuns(start, end);
        }
        levelHigh--;
    }
}

template <class Iterator, class Run, class IsolatedRun>
TextDirection BidiResolver<Iterator, Run, IsolatedRun>::determineDirectionalityInternal(
    bool breakOnParagraph, bool* hasStrongDirectionality)
{
    while (!m_current.atEnd()) {
        if (inIsolate()) {
            increment();
            continue;
        }
        if (breakOnParagraph && m_current.atParagraphSeparator())
            break;
        UChar32 current = m_current.current();
        if (UNLIKELY(U16_IS_SURROGATE(current))) {
            increment();
            // If this not the high part of the surrogate pair, then drop it and move to the next.
            if (!U16_IS_SURROGATE_LEAD(current))
                continue;
            UChar high = static_cast<UChar>(current);
            if (m_current.atEnd())
                continue;
            UChar low = m_current.current();
            // Verify the low part. If invalid, then assume an invalid surrogate pair and retry.
            if (!U16_IS_TRAIL(low))
                continue;
            current = U16_GET_SUPPLEMENTARY(high, low);
        }
        WTF::Unicode::CharDirection charDirection = WTF::Unicode::direction(current);
        if (charDirection == WTF::Unicode::LeftToRight) {
            if (hasStrongDirectionality)
                *hasStrongDirectionality = true;
            return LTR;
        }
        if (charDirection == WTF::Unicode::RightToLeft || charDirection == WTF::Unicode::RightToLeftArabic) {
            if (hasStrongDirectionality)
                *hasStrongDirectionality = true;
            return RTL;
        }
        increment();
    }
    if (hasStrongDirectionality)
        *hasStrongDirectionality = false;
    return LTR;
}

inline TextDirection directionForCharacter(UChar32 character)
{
    WTF::Unicode::CharDirection charDirection = WTF::Unicode::direction(character);
    if (charDirection == WTF::Unicode::RightToLeft || charDirection == WTF::Unicode::RightToLeftArabic)
        return RTL;
    return LTR;
}

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::createBidiRunsForLine(const Iterator& end, VisualDirectionOverride override, bool hardLineBreak, bool reorderRuns)
{
    using namespace WTF::Unicode;

    ASSERT(m_direction == OtherNeutral);
    m_trailingSpaceRun = 0;

    m_endOfLine = end;

    if (override != NoVisualOverride) {
        m_emptyRun = false;
        m_sor = m_current;
        m_eor = Iterator();
        while (m_current != end && !m_current.atEnd()) {
            m_eor = m_current;
            increment();
        }
        m_direction = override == VisualLeftToRightOverride ? LeftToRight : RightToLeft;
        appendRun(m_runs);
        m_runs.setLogicallyLastRun(m_runs.lastRun());
        if (override == VisualRightToLeftOverride && m_runs.runCount())
            m_runs.reverseRuns(0, m_runs.runCount() - 1);
        return;
    }

    m_emptyRun = true;

    m_eor = Iterator();

    m_last = m_current;
    bool lastLineEnded = false;
    BidiResolver<Iterator, Run, IsolatedRun> stateAtEnd;

    while (true) {
        if (inIsolate() && m_emptyRun) {
            m_sor = m_current;
            m_emptyRun = false;
        }

        if (!lastLineEnded && isEndOfLine(end)) {
            if (m_emptyRun)
                break;

            stateAtEnd.m_status = m_status;
            stateAtEnd.m_sor = m_sor;
            stateAtEnd.m_eor = m_eor;
            stateAtEnd.m_last = m_last;
            stateAtEnd.m_reachedEndOfLine = m_reachedEndOfLine;
            stateAtEnd.m_lastBeforeET = m_lastBeforeET;
            stateAtEnd.m_emptyRun = m_emptyRun;
            m_endOfRunAtEndOfLine = m_last;
            lastLineEnded = true;
        }
        CharDirection dirCurrent;
        if (lastLineEnded && (hardLineBreak || m_current.atEnd())) {
            BidiContext* c = context();
            if (hardLineBreak) {
                // A deviation from the Unicode Bidi Algorithm in order to match
                // WinIE and user expectations: hard line breaks reset bidi state
                // coming from unicode bidi control characters, but not those from
                // DOM nodes with specified directionality
                stateAtEnd.setContext(c->copyStackRemovingUnicodeEmbeddingContexts());

                dirCurrent = stateAtEnd.context()->dir();
                stateAtEnd.setEorDir(dirCurrent);
                stateAtEnd.setLastDir(dirCurrent);
                stateAtEnd.setLastStrongDir(dirCurrent);
            } else {
                while (c->parent())
                    c = c->parent();
                dirCurrent = c->dir();
            }
        } else {
            dirCurrent = m_current.direction();
            if (context()->override()
                && dirCurrent != RightToLeftEmbedding
                && dirCurrent != LeftToRightEmbedding
                && dirCurrent != RightToLeftOverride
                && dirCurrent != LeftToRightOverride
                && dirCurrent != PopDirectionalFormat)
                dirCurrent = context()->dir();
            else if (dirCurrent == NonSpacingMark)
                dirCurrent = m_status.last;
        }

        // We ignore all character directionality while in unicode-bidi: isolate spans.
        // We'll handle ordering the isolated characters in a second pass.
        if (inIsolate())
            dirCurrent = OtherNeutral;

        switch (dirCurrent) {

        // embedding and overrides (X1-X9 in the Bidi specs)
        case RightToLeftEmbedding:
        case LeftToRightEmbedding:
        case RightToLeftOverride:
        case LeftToRightOverride:
        case PopDirectionalFormat:
            embed(dirCurrent, FromUnicode);
            commitExplicitEmbedding(m_runs);
            break;

        // strong types
        case LeftToRight:
            addBracketContainedStrong();
            switch (m_status.last) {
            case RightToLeft:
            case RightToLeftArabic:
            case EuropeanNumber:
            case ArabicNumber:
                if (m_status.last != EuropeanNumber || m_status.lastStrong != LeftToRight)
                    appendRun(m_runs);
                break;
            case LeftToRight:
                break;
            case EuropeanNumberSeparator:
            case EuropeanNumberTerminator:
            case CommonNumberSeparator:
            case BoundaryNeutral:
            case BlockSeparator:
            case SegmentSeparator:
            case WhiteSpaceNeutral:
                if (m_status.eor == EuropeanNumber) {
                    if (m_status.lastStrong != LeftToRight) {
                        // the numbers need to be on a higher embedding level, so let's close that run
                        m_direction = EuropeanNumber;
                        appendRun(m_runs);
                        if (context()->dir() != LeftToRight) {
                            // the neutrals take the embedding direction, which is R
                            m_eor = m_last;
                            m_direction = RightToLeft;
                            appendRun(m_runs);
                        }
                    }
                } else if (m_status.eor == ArabicNumber) {
                    // Arabic numbers are always on a higher embedding level, so let's close that run
                    m_direction = ArabicNumber;
                    appendRun(m_runs);
                    if (context()->dir() != LeftToRight) {
                        // the neutrals take the embedding direction, which is R
                        m_eor = m_last;
                        m_direction = RightToLeft;
                        appendRun(m_runs);
                    }
                } else if (m_status.lastStrong != LeftToRight) {
                    // last stuff takes embedding dir
                    if (context()->dir() == RightToLeft) {
                        m_eor = m_last;
                        m_direction = RightToLeft;
                    }
                    appendRun(m_runs);
                }
                break;
            case OtherNeutral:
                appendNeutralRun(m_runs);
                break;
            default:
                break;
            }
            m_eor = m_current;
            m_status.eor = LeftToRight;
            m_status.lastStrong = LeftToRight;
            m_direction = LeftToRight;
            break;
        case RightToLeftArabic:
        case RightToLeft:
            addBracketContainedStrong();
            switch (m_status.last) {
            case LeftToRight:
            case EuropeanNumber:
            case ArabicNumber:
                appendRun(m_runs);
            case RightToLeft:
            case RightToLeftArabic:
                break;
            case EuropeanNumberSeparator:
            case EuropeanNumberTerminator:
            case CommonNumberSeparator:
            case BoundaryNeutral:
            case BlockSeparator:
            case SegmentSeparator:
            case WhiteSpaceNeutral:
                if (m_status.eor == EuropeanNumber) {
                    if (m_status.lastStrong == LeftToRight && context()->dir() == LeftToRight)
                        m_eor = m_last;
                    appendRun(m_runs);
                } else if (m_status.eor == ArabicNumber) {
                    appendRun(m_runs);
                } else if (m_status.lastStrong == LeftToRight) {
                    if (context()->dir() == LeftToRight)
                        m_eor = m_last;
                    appendRun(m_runs);
                }
                break;
            case OtherNeutral:
                appendNeutralRun(m_runs);
                break;
            default:
                break;
            }
            m_eor = m_current;
            m_status.eor = RightToLeft;
            m_status.lastStrong = dirCurrent;
            m_direction = RightToLeft;
            break;

            // weak types:

        case EuropeanNumber:
            if (m_status.last == OtherNeutral && m_status.lastStrong != RightToLeftArabic) {
                addBracketContainedStrong();
                appendNeutralRun(m_runs);
                m_eor = m_current;
                m_status.eor = EuropeanNumber;
                if (m_status.lastStrong != RightToLeft && m_status.lastStrong != RightToLeftArabic)
                    m_direction = LeftToRight;
                else
                    m_direction = EuropeanNumber;
                break;
            }
            else if (m_status.lastStrong != RightToLeftArabic) {
                // if last strong was AL change EN to AN
                switch (m_status.last) {
                case EuropeanNumber:
                case LeftToRight:
                    break;
                case RightToLeft:
                case RightToLeftArabic:
                case ArabicNumber:
                    m_eor = m_last;
                    appendRun(m_runs);
                    m_direction = EuropeanNumber;
                    break;
                case EuropeanNumberSeparator:
                case CommonNumberSeparator:
                    if (m_status.eor == EuropeanNumber)
                        break;
                case EuropeanNumberTerminator:
                case BoundaryNeutral:
                case BlockSeparator:
                case SegmentSeparator:
                case WhiteSpaceNeutral:
                    if (m_status.eor == EuropeanNumber) {
                        if (m_status.lastStrong == RightToLeft) {
                            // ENs on both sides behave like Rs, so the neutrals should be R.
                            // Terminate the EN run.
                            appendRun(m_runs);
                            // Make an R run.
                            m_eor = m_status.last == EuropeanNumberTerminator ? m_lastBeforeET : m_last;
                            m_direction = RightToLeft;
                            appendRun(m_runs);
                            // Begin a new EN run.
                            m_direction = EuropeanNumber;
                        }
                    } else if (m_status.eor == ArabicNumber) {
                        // Terminate the AN run.
                        appendRun(m_runs);
                        if (m_status.lastStrong == RightToLeft || context()->dir() == RightToLeft) {
                            // Make an R run.
                            m_eor = m_status.last == EuropeanNumberTerminator ? m_lastBeforeET : m_last;
                            m_direction = RightToLeft;
                            appendRun(m_runs);
                            // Begin a new EN run.
                            m_direction = EuropeanNumber;
                        }
                    } else if (m_status.lastStrong == RightToLeft) {
                        // Extend the R run to include the neutrals.
                        m_eor = m_status.last == EuropeanNumberTerminator ? m_lastBeforeET : m_last;
                        m_direction = RightToLeft;
                        appendRun(m_runs);
                        // Begin a new EN run.
                        m_direction = EuropeanNumber;
                    }
                default:
                    break;
                }
                m_eor = m_current;
                m_status.eor = EuropeanNumber;
                if (m_direction == OtherNeutral)
                    m_direction = LeftToRight;
                break;
            }
        case ArabicNumber:
            addBracketContainedStrong();
            dirCurrent = ArabicNumber;
            switch (m_status.last) {
            case LeftToRight:
                if (context()->dir() == LeftToRight)
                    appendRun(m_runs);
                break;
            case ArabicNumber:
                break;
            case RightToLeft:
            case RightToLeftArabic:
            case EuropeanNumber:
                m_eor = m_last;
                appendRun(m_runs);
                break;
            case CommonNumberSeparator:
                if (m_status.eor == ArabicNumber)
                    break;
            case EuropeanNumberSeparator:
            case EuropeanNumberTerminator:
            case BoundaryNeutral:
            case BlockSeparator:
            case SegmentSeparator:
            case WhiteSpaceNeutral:
                if (m_status.eor == ArabicNumber
                    || (m_status.eor == EuropeanNumber && (m_status.lastStrong == RightToLeft || context()->dir() == RightToLeft))
                    || (m_status.eor != EuropeanNumber && m_status.lastStrong == LeftToRight && context()->dir() == RightToLeft)) {
                    // Terminate the run before the neutrals.
                    appendRun(m_runs);

                    // Begin an R run for the neutrals.
                    m_direction = RightToLeft;
                } else if (m_direction == OtherNeutral) {
                    m_direction = m_status.lastStrong == LeftToRight ? LeftToRight : RightToLeft;
                }
                m_eor = m_last;
                appendRun(m_runs);
                break;
            case OtherNeutral:
                appendNeutralRun(m_runs);
                break;
            default:
                break;
            }
            m_eor = m_current;
            m_status.eor = ArabicNumber;
            if (m_direction == OtherNeutral)
                m_direction = ArabicNumber;
            break;
        case EuropeanNumberSeparator:
        case CommonNumberSeparator:
            if (m_status.last == OtherNeutral)
                appendNeutralRun(m_runs);
            break;
        case EuropeanNumberTerminator:
            if (m_status.last == EuropeanNumber) {
                dirCurrent = EuropeanNumber;
                m_eor = m_current;
                m_status.eor = dirCurrent;
            } else if (m_status.last != EuropeanNumberTerminator) {
                if (directionIsNeutral(m_status.last))
                    appendNeutralRun(m_runs);
                else
                    appendRun(m_runs);
                m_lastBeforeET = m_emptyRun ? m_eor : m_last;
            }
            break;
        case BoundaryNeutral:
            if (directionIsNeutral(m_status.last))
                appendNeutralRun(m_runs);
            if (m_eor == m_last)
                m_eor = m_current;
            break;
        // neutrals
        case BlockSeparator:
        case SegmentSeparator:
        case WhiteSpaceNeutral:
            if (m_status.last == OtherNeutral && pairedBracketType(m_last.current()) != PairedBracketNone)
                appendNeutralRun(m_runs);
            break;
        case OtherNeutral:
            if ((directionIsNeutral(m_status.last) && m_direction != OtherNeutral) ||
                (m_status.last == OtherNeutral && pairedBracketType(m_last.current()) != PairedBracketNone))
                appendNeutralRun(m_runs);
            break;
        default:
            break;
        }

        if (lastLineEnded && m_eor == m_current) {
            if (!m_reachedEndOfLine) {
                m_eor = m_endOfRunAtEndOfLine;
                switch (m_status.eor) {
                case LeftToRight:
                case RightToLeft:
                case ArabicNumber:
                    m_direction = m_status.eor;
                    break;
                case EuropeanNumber:
                    m_direction = m_status.lastStrong == LeftToRight ? LeftToRight : EuropeanNumber;
                    break;
                default:
                    ASSERT_NOT_REACHED();
                }
                appendRun(m_runs);
            }
            // TODO : There is no way to handle bidilines with linebreak condition.
            m_current = end;
            m_status = stateAtEnd.m_status;
            m_sor = stateAtEnd.m_sor;
            m_eor = stateAtEnd.m_eor;
            m_last = stateAtEnd.m_last;
            m_reachedEndOfLine = stateAtEnd.m_reachedEndOfLine;
            m_lastBeforeET = stateAtEnd.m_lastBeforeET;
            m_emptyRun = stateAtEnd.m_emptyRun;
            m_direction = OtherNeutral;
            break;
        }

        updateStatusLastFromCurrentDirection(dirCurrent);
        m_last = m_current;

        if (m_emptyRun) {
            m_sor = m_current;
            m_emptyRun = false;
        }

        increment();
        if (!m_currentExplicitEmbeddingSequence.isEmpty()) {
            bool committed = commitExplicitEmbedding(m_runs);
            if (committed && lastLineEnded) {
                m_current = end;
                m_status = stateAtEnd.m_status;
                m_sor = stateAtEnd.m_sor;
                m_eor = stateAtEnd.m_eor;
                m_last = stateAtEnd.m_last;
                m_reachedEndOfLine = stateAtEnd.m_reachedEndOfLine;
                m_lastBeforeET = stateAtEnd.m_lastBeforeET;
                m_emptyRun = stateAtEnd.m_emptyRun;
                m_direction = OtherNeutral;
                break;
            }
        }
    }

    while (!m_unresolvedNeutralsSequences.isEmpty()) {
        m_unresolvedNeutralsSequences.last().setAllBracketsAsNeutral();
        if (hardLineBreak || m_current.atEnd())
            m_unresolvedNeutralsSequences.last().resolve(true);
        m_unresolvedNeutralsSequences.removeLast();
    }

    m_runs.setLogicallyLastRun(m_runs.lastRun());
    if (reorderRuns)
        reorderRunsFromLevels(m_runs);
    m_endOfRunAtEndOfLine = Iterator();
    m_endOfLine = Iterator();

    if (!hardLineBreak && m_runs.runCount())
        applyL1Rule(m_runs);
}

template <class Iterator, class Run, class IsolatedRun>
void BidiResolver<Iterator, Run, IsolatedRun>::setMidpointStateForIsolatedRun(Run& run, const MidpointState<Iterator>& midpoint)
{
    ASSERT(!m_midpointStateForIsolatedRun.contains(&run));
    m_midpointStateForIsolatedRun.add(&run, midpoint);
}

template<class Iterator, class Run, class IsolatedRun>
MidpointState<Iterator> BidiResolver<Iterator, Run, IsolatedRun>::midpointStateForIsolatedRun(Run& run)
{
    return m_midpointStateForIsolatedRun.take(&run);
}

} // namespace blink

#endif // BidiResolver_h
