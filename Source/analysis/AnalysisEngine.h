#pragma once

#include "AnalysisCore.h"

#include <juce_core/juce_core.h>

#include <atomic>

namespace pc
{

/** Lock-free hand-off from the audio thread to a background analysis thread.

    Audio thread: push() copies the three mono streams into a single-producer /
    single-consumer FIFO, then a small "stamp" (block length, transport, kick
    state) into a second FIFO. It never allocates, locks or blocks; if the
    FIFO is full the block is dropped and the gap is flagged.

    Worker thread: drains both FIFOs into AnalysisCore and publishes a snapshot
    about 30 times a second. The GUI only ever reads that snapshot. */
class AnalysisEngine : private juce::Thread
{
public:
    AnalysisEngine() : juce::Thread ("Phase Check analysis") {}
    ~AnalysisEngine() override { stop(); }

    void prepare (double sampleRate)
    {
        stop();
        core.prepare (sampleRate);

        int size = 1;
        while (size < (int) (sampleRate * 1.0)) size <<= 1;
        sampleFifo.setTotalSize (size);
        bufP.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);
        bufS.assign ((size_t) size, 0.0f);

        stampFifo.setTotalSize ((int) stamps.size());
        sampleFifo.reset();
        stampFifo.reset();
        dropped = false;

        startThread (juce::Thread::Priority::low);
    }

    void stop() { stopThread (2000); }

    /** Audio thread. Real-time safe. */
    void push (const float* processed, const float* raw, const float* sidechain, int n,
               const TransportInfo& transport, const KickState& state) noexcept
    {
        if (n <= 0 || bufP.empty())
            return;

        if (sampleFifo.getFreeSpace() < n || stampFifo.getFreeSpace() < 1)
        {
            dropped = true;
            return;
        }

        {
            const auto w = sampleFifo.write (n);
            copyIn (w.startIndex1, w.blockSize1, 0, processed, raw, sidechain);
            copyIn (w.startIndex2, w.blockSize2, w.blockSize1, processed, raw, sidechain);
        }
        {
            const auto w = stampFifo.write (1);
            auto& st = stamps[(size_t) (w.blockSize1 > 0 ? w.startIndex1 : w.startIndex2)];
            st.n = n;
            st.transport = transport;
            st.state = state;
            st.gapBefore = dropped;
            dropped = false;
        }
    }

    void requestReset() noexcept { core.requestReset(); }

    std::shared_ptr<const Snapshot> getSnapshot() const { return core.getSnapshot(); }

private:
    struct Stamp
    {
        int n = 0;
        TransportInfo transport;
        KickState state;
        bool gapBefore = false;
    };

    void copyIn (int start, int size, int srcOffset, const float* p, const float* r, const float* s) noexcept
    {
        if (size <= 0) return;
        std::copy (p + srcOffset, p + srcOffset + size, bufP.begin() + start);
        std::copy (r + srcOffset, r + srcOffset + size, bufR.begin() + start);
        std::copy (s + srcOffset, s + srcOffset + size, bufS.begin() + start);
    }

    void run() override
    {
        std::vector<float> p (8192), r (8192), s (8192);
        auto lastUpdate = juce::Time::getMillisecondCounter();
        auto lastAudio = lastUpdate;
        bool idle = false;

        while (! threadShouldExit())
        {
            bool gotAudio = false;

            while (stampFifo.getNumReady() > 0)
            {
                Stamp st;
                {
                    const auto rd = stampFifo.read (1);
                    st = stamps[(size_t) (rd.blockSize1 > 0 ? rd.startIndex1 : rd.startIndex2)];
                }

                if (st.gapBefore)
                    core.discontinuity();

                int remaining = st.n;
                while (remaining > 0)
                {
                    const int chunk = std::min (remaining, (int) p.size());
                    if (sampleFifo.getNumReady() < chunk) { remaining = 0; break; }
                    {
                        const auto rd = sampleFifo.read (chunk);
                        copyOut (rd.startIndex1, rd.blockSize1, 0, p, r, s);
                        copyOut (rd.startIndex2, rd.blockSize2, rd.blockSize1, p, r, s);
                    }
                    core.push (p.data(), r.data(), s.data(), chunk, st.transport, st.state);
                    remaining -= chunk;
                }
                gotAudio = true;
            }

            const auto now = juce::Time::getMillisecondCounter();
            if (gotAudio) lastAudio = now;

            // The host stopped calling us: don't keep showing numbers as live.
            const bool nowIdle = now - lastAudio > 1000;
            if (nowIdle != idle)
            {
                idle = nowIdle;
                core.setAudioIdle (idle);
            }

            if (now - lastUpdate >= 33)
            {
                lastUpdate = now;
                core.update();
            }

            wait (8);
        }
    }

    void copyOut (int start, int size, int dstOffset, std::vector<float>& p, std::vector<float>& r, std::vector<float>& s)
    {
        if (size <= 0) return;
        std::copy (bufP.begin() + start, bufP.begin() + start + size, p.begin() + dstOffset);
        std::copy (bufR.begin() + start, bufR.begin() + start + size, r.begin() + dstOffset);
        std::copy (bufS.begin() + start, bufS.begin() + start + size, s.begin() + dstOffset);
    }

    AnalysisCore core;

    juce::AbstractFifo sampleFifo { 1024 };
    std::vector<float> bufP, bufR, bufS;

    juce::AbstractFifo stampFifo { 1024 };
    std::array<Stamp, 1024> stamps {};

    bool dropped = false;   // audio thread only
};

} // namespace pc
