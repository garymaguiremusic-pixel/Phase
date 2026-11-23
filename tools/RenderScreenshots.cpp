// Developer tool: runs the real processor + editor with synthetic audio and a fake
// host transport, then saves PNG screenshots of both modes.
//   PhaseCheckScreenshots <output-folder>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Synth.h"

#include <juce_gui_extra/juce_gui_extra.h>

using namespace pc;

namespace
{
    struct FakePlayHead : juce::AudioPlayHead
    {
        bool playing = false;
        double bpm = 138.0, ppq = 0.0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setIsPlaying (playing);
            if (playing)
            {
                p.setBpm (bpm);
                p.setPpqPosition (ppq);
                p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            }
            return p;
        }
    };

    void configure (PhaseCheckProcessor& proc, double fs)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        const bool ok = proc.setBusesLayout (l);
        proc.setRateAndBufferSizeDetails (fs, 512);
        std::printf ("layout ok=%d ins=%d outs=%d\n", (int) ok, proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
    }

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    void run (PhaseCheckProcessor& proc, FakePlayHead& head, const std::vector<float>& kick,
              const std::vector<float>& sc, double fs, double startOffsetSec)
    {
        const int block = 512;
        juce::AudioBuffer<float> buf (4, block);
        juce::MidiBuffer midi;
        const double spq = fs * 60.0 / head.bpm;

        for (size_t pos = 0; pos + block <= kick.size(); pos += block)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                buf.copyFrom (ch, 0, kick.data() + pos, block);
                buf.copyFrom (ch + 2, 0, sc.data() + pos, block);
            }
            head.ppq = ((double) pos - startOffsetSec * fs) / spq;
            proc.processBlock (buf, midi);
            juce::Thread::sleep (3);
            if ((pos / block) % 16 == 0) pump (1);
        }
    }

    void snap (juce::Component& c, const juce::File& file)
    {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (img, out);
        std::printf ("wrote %s (%dx%d)\n", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    const double fs = 48000.0;

    // ---------------- Hit mode: kick vs. inverted copy delayed 1.8 ms ----------------
    {
        PhaseCheckProcessor proc;
        FakePlayHead head;
        proc.setPlayHead (&head);
        configure (proc, fs);
        proc.prepareToPlay (fs, 512);

        auto kick = synth::kickTrain (fs, 5.0, 0.5);
        auto sc = synth::delayed (kick, 1.8e-3 * fs);
        for (auto& v : sc) v = -0.8f * v;

        run (proc, head, kick, sc, fs, 0.0);
        pump (400);

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        pump (600);
        snap (*ed, outDir.getChildFile ("hit-mode.png"));

        // apply the suggested fix: flip polarity
        if (auto* p = proc.getState().getParameter (ParamID::invert)) p->setValueNotifyingHost (1.0f);
        run (proc, head, kick, sc, fs, 0.0);
        pump (600);
        snap (*ed, outDir.getChildFile ("hit-mode-flipped.png"));

        // 75 % window size
        ed->setSize (780, 570);
        pump (300);
        snap (*ed, outDir.getChildFile ("hit-mode-75pct.png"));
        ed.reset();
        proc.releaseResources();
    }

    // ---------------- Overlap mode: 138 BPM trance pattern ----------------
    {
        PhaseCheckProcessor proc;
        FakePlayHead head;
        head.playing = true;
        proc.setPlayHead (&head);
        configure (proc, fs);
        proc.prepareToPlay (fs, 512);
        if (auto* p = proc.getState().getParameter (ParamID::mode)) p->setValueNotifyingHost (1.0f);

        synth::OverlapScene scene;
        scene.fs = fs;
        scene.bars = 5;
        scene.render();
        run (proc, head, scene.kick, scene.bass, fs, scene.start);
        pump (400);

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        pump (600);
        snap (*ed, outDir.getChildFile ("overlap-mode.png"));
        ed.reset();
        proc.releaseResources();
    }

    // ---------------- No sidechain ----------------
    {
        PhaseCheckProcessor proc;
        FakePlayHead head;
        proc.setPlayHead (&head);
        configure (proc, fs);
        proc.prepareToPlay (fs, 512);
        auto kick = synth::kickTrain (fs, 2.5, 0.5);
        std::vector<float> silent (kick.size(), 0.0f);
        run (proc, head, kick, silent, fs, 0.0);
        pump (300);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        pump (400);
        snap (*ed, outDir.getChildFile ("no-sidechain.png"));
        ed.reset();
        proc.releaseResources();
    }

    return 0;
}
