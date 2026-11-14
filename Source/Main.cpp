#include "MainComponent.h"
#include "Diagnostics.h"
#include "Paths.h"

// The app used to be called ClaudeDAW. On the first start under the new name, bring over the
// user's Claude memory and browser-side preferences (tutorial, zoom, chime). Songs and audio stay
// where they are (saved songs point at them). Plug-in lists and old pointers are rebuilt fresh.
static void migrateFromClaudeDAW()
{
    auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    auto oldDir = appData.getChildFile ("ClaudeDAW");
    auto newDir = getUserDataDir();
    auto marker = newDir.getChildFile ("migrated-from-claudedaw.txt");

    if (! oldDir.isDirectory() || marker.existsAsFile())
        return;

    newDir.createDirectory();

    for (auto name : { "claude_memory.md", "WebView2" })
    {
        auto from = oldDir.getChildFile (name), to = newDir.getChildFile (name);
        if (to.exists())
            continue;
        if (from.isDirectory())     from.copyDirectoryTo (to);
        else if (from.existsAsFile()) from.copyFileTo (to);
    }

    marker.replaceWithText ("Settings copied from " + oldDir.getFullPathName() + " on " + juce::Time::getCurrentTime().toString (true, true));
}

class SynthientApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Synthient"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }

    void initialise (const juce::String&) override
    {
        migrateFromClaudeDAW();
        Diagnostics::start();
        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        Diagnostics::stop();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow()
            : DocumentWindow ("Synthient", juce::Colours::black, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (1280, 800);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (SynthientApplication)
