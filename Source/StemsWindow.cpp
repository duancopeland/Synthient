#include "StemsWindow.h"

void flashTaskbarButton (void* nativeWindowHandle);   // WinNotify.cpp

namespace
{
    const juce::Colour background (0xff15131c), panel (0xff221f2b), text (0xffe8e4f0), muted (0xff9a93a8), accent (0xffff4fa3);
}

class StemsWindow::Content  : public juce::Component
{
public:
    Content (StemsWindow& w, const juce::File& source, const juce::File& outDir) : window (w), folder (outDir)
    {
        title.setText ("Splitting " + source.getFileName() + " into drums, bass, vocals and other", juce::dontSendNotification);
        title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        status.setText ("Starting…", juce::dontSendNotification);
        hint.setText ("You can keep working in Synthient while this runs.", juce::dontSendNotification);

        for (auto* l : { &title, &status, &hint })
        {
            l->setColour (juce::Label::textColourId, l == &title ? text : muted);
            addAndMakeVisible (*l);
        }

        bar.setColour (juce::ProgressBar::foregroundColourId, accent);
        bar.setColour (juce::ProgressBar::backgroundColourId, panel);
        addAndMakeVisible (bar);

        for (auto* b : { &cancel, &addTracks, &openFolder })
        {
            b->setColour (juce::TextButton::buttonColourId, panel);
            b->setColour (juce::TextButton::textColourOffId, text);
            addAndMakeVisible (*b);
        }

        addTracks.setColour (juce::TextButton::buttonColourId, accent.darker (0.3f));
        addTracks.setVisible (false);
        openFolder.setVisible (false);

        cancel.onClick = [this] { window.closeButtonPressed(); };
        openFolder.onClick = [this] { folder.startAsProcess(); };
        addTracks.onClick = [this]
        {
            auto error = window.callbacks.addAsTracks ? window.callbacks.addAsTracks (stems) : juce::String();
            status.setText (error.isEmpty() ? "Added the stems as new tracks." : error, juce::dontSendNotification);
            addTracks.setEnabled (error.isNotEmpty());
        };

        setSize (520, 190);
    }

    void paint (juce::Graphics& g) override { g.fillAll (background); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        title.setBounds (r.removeFromTop (26));
        r.removeFromTop (6);
        bar.setBounds (r.removeFromTop (22));
        r.removeFromTop (6);
        status.setBounds (r.removeFromTop (22));
        hint.setBounds (r.removeFromTop (22));
        auto buttons = r.removeFromBottom (30);
        cancel.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        openFolder.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (8);
        addTracks.setBounds (buttons.removeFromRight (160));
    }

    void line (const juce::var& json)
    {
        if (json.hasProperty ("progress"))
        {
            progress = (double) json["progress"];
            status.setText ("Separating… " + juce::String (juce::roundToInt (progress * 100)) + "%", juce::dontSendNotification);
        }
        else if (json.hasProperty ("status") && ! json.hasProperty ("stems"))
        {
            status.setText (json["status"].toString(), juce::dontSendNotification);
        }
    }

    void finished (const juce::var& json)
    {
        running = false;
        cancel.setButtonText ("Close");

        if (json.hasProperty ("error"))
        {
            progress = 0.0;
            status.setText ("Couldn't separate: " + json["error"].toString(), juce::dontSendNotification);
            hint.setText ({}, juce::dontSendNotification);
            return;
        }

        progress = 1.0;
        stems = json["stems"];
        status.setText ("Done. Stems saved to " + folder.getFullPathName(), juce::dontSendNotification);
        hint.setText ("Add them as tracks (they start at bar 1 and keep their own tempo), or open the folder.", juce::dontSendNotification);
        addTracks.setVisible (true);
        openFolder.setVisible (true);

        if (auto* peer = window.getPeer())
            flashTaskbarButton (peer->getNativeHandle());
    }

    bool running = true;

private:
    StemsWindow& window;
    juce::File folder;
    juce::var stems;
    double progress = -1.0; // indeterminate until the first chunk finishes
    juce::Label title, status, hint;
    juce::ProgressBar bar { progress };
    juce::TextButton cancel { "Cancel" }, addTracks { "Add stems as tracks" }, openFolder { "Open folder" };
};

StemsWindow::StemsWindow (const juce::File& source, const juce::File& outDir, Callbacks cb)
    : DocumentWindow ("Stems - " + source.getFileNameWithoutExtension(), background, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
      callbacks (std::move (cb))
{
    content = new Content (*this, source, outDir);
    setContentOwned (content, true);
    setUsingNativeTitleBar (true);
    setResizable (false, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);

    juce::Component::SafePointer<StemsWindow> safe (this);

    Sidecar::runStreaming ({ "separate_stems", source.getFullPathName(), "--out-dir", outDir.getFullPathName() }, job,
        [safe] (juce::var json) { if (safe != nullptr) safe->content->line (json); },
        [safe] (juce::var json)
        {
            if (safe == nullptr)
                return;
            safe->content->finished (json);
            if (! json.hasProperty ("error") && safe->callbacks.finished)
                safe->callbacks.finished (json["stems"]);
        });
}

StemsWindow::~StemsWindow()
{
    job->cancel(); // no-op once finished
}

void StemsWindow::closeButtonPressed()
{
    if (content->running)
        job->cancel();

    if (callbacks.closed)
        callbacks.closed (*this);
}
