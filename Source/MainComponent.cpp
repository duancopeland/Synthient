#include "MainComponent.h"
#include "ClaudeClient.h"
#include "Sidecar.h"
#include "StemsWindow.h"
#include "Diagnostics.h"
#include "Bridge.h"
#include "Paths.h"
#include "Sampler/ClaudeSamplerPlugin.h"

void flashTaskbarButton (void* nativeWindowHandle);   // WinNotify.cpp

using namespace juce;
using namespace std::literals;
#include "PluginWindow.h"

namespace
{
    String mimeTypeFor (const File& f)
    {
        auto ext = f.getFileExtension().toLowerCase();
        if (ext == ".html") return "text/html";
        if (ext == ".js")   return "text/javascript";
        if (ext == ".css")  return "text/css";
        if (ext == ".json") return "application/json";
        if (ext == ".svg")  return "image/svg+xml";
        if (ext == ".png")  return "image/png";
        return "application/octet-stream";
    }

    var result (const String& error, const var& extra = {})
    {
        auto obj = new DynamicObject();
        obj->setProperty ("ok", error.isEmpty());
        if (error.isNotEmpty())   obj->setProperty ("error", error);
        if (! extra.isVoid())     obj->setProperty ("value", extra);
        return var (obj);
    }

    File getAppDataDir()
    {
        return getUserDataDir();
    }

    var makeFileList (const File& folder, const StringArray& files)
    {
        auto obj = new DynamicObject();
        obj->setProperty ("folder", folder.getFullPathName());
        Array<var> list;
        for (auto& f : files)
            list.add (f);
        obj->setProperty ("files", list);
        return var (obj);
    }

    // The downloaded instrument library: a path saved in library_path.txt, else next to the app.
    File getLibraryDir()
    {
        auto configured = getAppDataDir().getChildFile ("library_path.txt");
        if (configured.existsAsFile())
        {
            File dir (configured.loadFileAsString().trim());
            if (dir.isDirectory())
                return dir;
        }
        return getAppFolder ("library");
    }

    File getDocumentsDir()
    {
        // Test copies (SYNTHIENT_DATA_DIR set) keep their documents inside that folder too.
        auto d = SystemStats::getEnvironmentVariable ("SYNTHIENT_DATA_DIR", {}).isNotEmpty()
                   ? getUserDataDir().getChildFile ("Documents")
                   : File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Synthient");
        d.createDirectory();
        return d;
    }
}

//==============================================================================
MainComponent::MainComponent()
{
    // Engine settings (Settings.xml) go in the same data folder as everything else.
    struct Storage : te::PropertyStorage
    {
        Storage() : te::PropertyStorage ("Synthient") {}
        File getAppPrefsFolder() override { auto d = getUserDataDir(); d.createDirectory(); return d; }
    };
    engine = std::make_unique<te::Engine> (std::make_unique<Storage>(), std::make_unique<ExtendedUIBehaviour>(), nullptr);
    // Must be registered before any Edit is created or loaded, or songs using it can't be read.
    engine->getPluginManager().createBuiltInType<ClaudeSamplerPlugin>();
    ClaudeSamplerPlugin::libraryRoot = [] { return getLibraryDir(); };
    prepareDefaultScanPaths();
    scanBundledPlugins();   // so the first song can use Surge XT instead of falling back to 4OSC
    song = std::make_unique<Song> (*engine);

    recoveryAvailable = getLockFile().existsAsFile() && getAutosaveFile().existsAsFile();
    getLockFile().getParentDirectory().createDirectory();
    getLockFile().replaceWithText ("running");
    startTimer (120 * 1000);

    // Requests from Claude Code go to the UI, which runs them like the chat does (app.js "bridgeCall").
    bridge = std::make_unique<ToolBridge> ([this] (int id, const var& request)
    {
        auto obj = new DynamicObject();
        obj->setProperty ("type", "bridgeCall");
        obj->setProperty ("id", id);
        obj->setProperty ("name", request["name"]);
        obj->setProperty ("input", request["input"]);
        emitToUi (var (obj));
    });

    browser = std::make_unique<WebBrowserComponent> (createBrowserOptions());
    addAndMakeVisible (*browser);
    browser->goToURL (WebBrowserComponent::getResourceProviderRoot());

    setSize (1400, 900);
}

MainComponent::~MainComponent()
{
    stopTimer();
    // Clean exit: no recovery needed next time.
    getLockFile().deleteFile();
    getAutosaveFile().deleteFile();

    bridge = nullptr;
    browser = nullptr;
    song->getEdit().getTransport().stop (false, true);
    song = nullptr;
}

void MainComponent::resized()
{
    browser->setBounds (getLocalBounds());
}

File MainComponent::getAutosaveFile() const   { return getAppDataDir().getChildFile ("autosave").getChildFile ("autosave.tracktionedit"); }
File MainComponent::getLockFile() const       { return getAppDataDir().getChildFile ("autosave").getChildFile ("session.lock"); }

void MainComponent::timerCallback()
{
    if (song == nullptr || song->isRendering() || recoveryAvailable)
        return;

    // Only write when something changed since the last autosave.
    const auto hash = song->getEdit().state.toXmlString().hashCode64();
    if (hash == lastAutosaveHash)
        return;

    getAutosaveFile().getParentDirectory().createDirectory();
    if (song->saveCopy (getAutosaveFile()).isEmpty())
        lastAutosaveHash = hash;
}

//==============================================================================
WebBrowserComponent::Options MainComponent::createBrowserOptions()
{
    using Options = WebBrowserComponent::Options;
    using Args = const Array<var>&;
    using Completion = WebBrowserComponent::NativeFunctionCompletion;

    return Options{}
        .withBackend (Options::Backend::webview2)
        .withWinWebView2Options (Options::WinWebView2{}
                                     .withUserDataFolder (getAppDataDir().getChildFile ("WebView2"))
                                     .withBackgroundColour (Colour (0xff111318))
                                     .withStatusBarDisabled())
        // Keep the page (and a running Claude or Claude Code job) alive while minimized; by default
        // JUCE swaps in about:blank when the window is hidden.
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withNativeIntegrationEnabled()
        .withResourceProvider ([this] (const auto& url) { return getResource (url); })

        .withNativeFunction ("getState", [this] (Args args, Completion done)
        {
            done (song->getState (args.isEmpty() || (bool) args[0]));
        })
        .withNativeFunction ("notifyDone", [this] (Args, Completion done)
        {
            if (auto* peer = getPeer())
                flashTaskbarButton (peer->getNativeHandle());
            done (true);
        })
        .withNativeFunction ("getTransport", [this] (Args, Completion done) { done (song->getTransport()); })
        .withNativeFunction ("runTool", [this] (Args args, Completion done)
        {
            if (args.size() < 2)
                return done (result ("runTool expects (name, input)"));

            const auto name = args[0].toString();
            const auto started = Time::getMillisecondCounterHiRes();
            auto r = song->runTool (name, args[1]);
            const auto ms = Time::getMillisecondCounterHiRes() - started;
            if (name != "get_song" && name != "get_device_params")
                Diagnostics::log ("tool " + name + " " + ((bool) r["ok"] ? String ("ok") : "FAILED: " + r["error"].toString().substring (0, 200))
                                  + (ms > 500 ? " (" + String (ms / 1000.0, 1) + " s)" : String()));
            done (r);
        })
        // Claude Code bridge: the UI is loaded and answers requests; then the answer to one.
        .withNativeFunction ("bridgeReady", [this] (Args, Completion done) { bridge->start(); done (true); })
        .withNativeFunction ("bridgeReply", [this] (Args args, Completion done)
        {
            bridge->reply ((int) args[0], args[1].toString());
            done (true);
        })
        .withNativeFunction ("claudeRequest", [] (Args args, Completion done)
        {
            const auto started = Time::getMillisecondCounterHiRes();
            const auto requestBytes = args[0].toString().getNumBytesAsUTF8();
            Diagnostics::log ("Claude request (" + String (requestBytes / 1024) + " KB)");
            ClaudeClient::postMessages (args[0].toString(), [done, started] (String response)
            {
                auto json = JSON::parse (response);
                Diagnostics::log ("Claude reply after " + String ((Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) + " s: "
                                  + (json["error"].isObject() ? "error " + json["error"]["message"].toString().substring (0, 200)
                                                              : "stop_reason " + json["stop_reason"].toString())
                                  + ", " + String (response.getNumBytesAsUTF8() / 1024) + " KB");
                done (response);
            });
        })

        // Transport
        .withNativeFunction ("play", [this] (Args, Completion done)
        {
            song->getEdit().getTransport().play (false);
            done (true);
        })
        .withNativeFunction ("stop", [this] (Args, Completion done)
        {
            auto& t = song->getEdit().getTransport();
            if (t.isPlaying())  t.stop (false, false);
            else                t.setPosition (0s);
            done (true);
        })
        .withNativeFunction ("setPosition", [this] (Args args, Completion done)
        {
            auto& edit = song->getEdit();
            edit.getTransport().setPosition (edit.tempoSequence.toTime (te::BeatPosition::fromBeats ((double) args[0])));
            done (true);
        })
        .withNativeFunction ("undo", [this] (Args, Completion done) { done (song->getEdit().getUndoManager().undo()); })
        .withNativeFunction ("redo", [this] (Args, Completion done) { done (song->getEdit().getUndoManager().redo()); })

        // Devices
        .withNativeFunction ("openDeviceWindow", [this] (Args args, Completion done)
        {
            auto device = song->findDevice ((int) args[0], args[1]);

            if (device == nullptr || dynamic_cast<te::ExternalPlugin*> (device) == nullptr)
                return done (result ("Only plug-ins have their own window; built-in devices are edited in the panel."));

            device->showWindowExplicitly();
            done (result ({}));
        })
        .withNativeFunction ("scanPlugins", [this] (Args, Completion done) { showPluginScanner(); done (true); })
        .withNativeFunction ("scanPluginsNow", [this] (Args, Completion done)
        {
            // Scan the VST3 folders without the dialog. Already-known plug-ins are skipped, so this is
            // quick after the first run. A crashing plug-in is recorded and skipped next time.
            auto& pm = engine->getPluginManager();
            auto& props = engine->getPropertyStorage().getPropertiesFile();
            FileSearchPath paths (props.getValue ("lastPluginScanPath_VST3"));
            int added = 0;

            for (auto* format : pm.pluginFormatManager.getFormats())
            {
                if (! format->getName().containsIgnoreCase ("VST3"))
                    continue;

                const int before = pm.knownPluginList.getNumTypes();
                PluginDirectoryScanner scanner (pm.knownPluginList, *format, paths, true,
                                                engine->getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"), true);
                String name;
                while (scanner.scanNextFile (true, name)) {}
                added += pm.knownPluginList.getNumTypes() - before;
            }

            done (added);
        })
        .withNativeFunction ("audioSettings", [this] (Args, Completion done) { showAudioSettings(); done (true); })
        .withNativeFunction ("chooseSample", [this] (Args args, Completion done)
        {
            const int track = args[0];
            const int root = args.size() > 1 ? (int) args[1] : 60;
            chooseFile ("Load sample", "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg", false, [this, track, root, done] (File f)
            {
                if (f == File())
                    return done (result ("cancelled"));

                auto input = new DynamicObject();
                input->setProperty ("track", track);
                input->setProperty ("path", f.getFullPathName());
                input->setProperty ("root_note", root);
                done (song->runTool ("load_sample", var (input)));
            });
        })

        // Files
        .withNativeFunction ("newSong", [this] (Args, Completion done) { song->newSong(); done (result ({})); })
        .withNativeFunction ("openSong", [this] (Args, Completion done)
        {
            chooseFile ("Open song", "*.tracktionedit", false, [this, done] (File f)
            {
                done (f == File() ? result ("cancelled") : result (song->load (f)));
            });
        })
        .withNativeFunction ("saveSong", [this] (Args args, Completion done)
        {
            const bool saveAs = args.size() > 0 && (bool) args[0];

            if (! saveAs && song->getFile() != File())
                return done (result (song->save (song->getFile())));

            chooseFile ("Save song", "*.tracktionedit", true, [this, done] (File f)
            {
                done (f == File() ? result ("cancelled")
                                  : result (song->save (f.withFileExtension ("tracktionedit"))));
            });
        })
        .withNativeFunction ("exportWavTo", [this] (Args args, Completion done)
        {
            // Used by Claude's render_and_analyze tool and by tests.
            File target = args.size() > 0 && args[0].toString().isNotEmpty()
                              ? File (args[0].toString())
                              : getAppDataDir().getChildFile ("renders").getNonexistentChildFile ("mix", ".wav");
            target.getParentDirectory().createDirectory();
            song->exportAudio (target, [done, target] (String error) { done (result (error, target.getFullPathName())); });
        })
        .withNativeFunction ("saveSongTo", [this] (Args args, Completion done)
        {
            done (result (song->save (File (args[0].toString()))));
        })
        .withNativeFunction ("loadSongFrom", [this] (Args args, Completion done)
        {
            done (result (song->load (File (args[0].toString()))));
        })

        // Stem separation in its own window; the UI stays usable.
        .withNativeFunction ("separateStems", [this] (Args args, Completion done)
        {
            const String path = args.size() > 0 ? args[0].toString() : String();

            if (path.isNotEmpty())
            {
                openStemsWindow (File (path));
                return done (result ({}));
            }

            chooseFile ("Choose a song to split into stems", "*.wav;*.mp3;*.flac;*.ogg;*.aif;*.aiff;*.m4a", false, [this, done] (File f)
            {
                if (f == File())
                    return done (result ("cancelled"));
                openStemsWindow (f);
                done (result ({}));
            });
        })

        // Analysis sidecar
        .withNativeFunction ("analyzeAudio", [this] (Args args, Completion done)
        {
            const String path = args.size() > 0 ? args[0].toString() : String();
            const var options = args.size() > 1 ? args[1] : var();

            auto analyze = [done, options] (File f)
            {
                if (f == File())
                    return done (result ("cancelled"));

                auto outDir = getAppDataDir().getChildFile ("analysis")
                                  .getNonexistentChildFile (File::createLegalFileName (f.getFileNameWithoutExtension()), {});
                outDir.createDirectory();
                auto spectrogram = outDir.getChildFile ("spectrogram.png");

                StringArray cmd { "analyze_track", f.getFullPathName(), "--spectrogram", spectrogram.getFullPathName(),
                                  "--out-dir", outDir.getFullPathName() };
                if ((bool) options.getProperty ("stems", false)) cmd.add ("--stems");
                if ((bool) options.getProperty ("midi", false))  cmd.add ("--midi");

                Sidecar::run (cmd, [done, spectrogram, f] (var json)
                {
                    if (json.hasProperty ("error") && ! json.hasProperty ("tempo_bpm"))
                        return done (result (json["error"].toString()));

                    auto obj = new DynamicObject();
                    obj->setProperty ("analysis", json);
                    obj->setProperty ("file", f.getFullPathName());

                    MemoryBlock png;
                    if (spectrogram.loadFileAsData (png))
                        obj->setProperty ("spectrogramPng", Base64::toBase64 (png.getData(), png.getSize()));

                    done (result ({}, var (obj)));
                });
            };

            if (path.isNotEmpty())
                return analyze (File (path));

            chooseFile ("Choose a track to analyze", "*.wav;*.mp3;*.flac;*.ogg;*.aif;*.aiff", false, analyze);
        })
        .withNativeFunction ("scanSampleFolder", [this] (Args, Completion done)
        {
            activeChooser = std::make_shared<FileChooser> ("Choose your samples folder", getDocumentsDir());
            activeChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                                        [done] (const FileChooser& fc)
            {
                auto folder = fc.getResult();

                if (folder == File())
                    return done (result ("cancelled"));

                auto index = getAppDataDir().getChildFile ("sample_index.json");
                Sidecar::run ({ "tag_samples", folder.getFullPathName(), "--out", index.getFullPathName() },
                              [done] (var json)
                {
                    done (json.hasProperty ("error") ? result (json["error"].toString()) : result ({}, json));
                });
            });
        })
        // Producer memory: production rules and user preferences Claude reads every request.
        .withNativeFunction ("readMemory", [] (Args, Completion done)
        {
            auto f = getAppDataDir().getChildFile ("claude_memory.md");
            done (f.existsAsFile() ? var (f.loadFileAsString()) : var());
        })
        .withNativeFunction ("writeMemory", [] (Args args, Completion done)
        {
            auto f = getAppDataDir().getChildFile ("claude_memory.md");
            f.getParentDirectory().createDirectory();
            done (f.replaceWithText (args[0].toString()));
        })
        // Sampled instruments (library/index.json): regions with paths made absolute.
        .withNativeFunction ("getInstrumentIndex", [] (Args, Completion done)
        {
            auto dir = getLibraryDir();
            auto index = JSON::parse (dir.getChildFile ("index.json"));

            if (auto* list = index["instruments"].getArray())
                for (auto& inst : *list)
                {
                    if (auto* o = inst.getDynamicObject(); o != nullptr && o->hasProperty ("sfz"))
                        o->setProperty ("sfz", dir.getChildFile (o->getProperty ("sfz").toString()).getFullPathName());

                    if (auto* regions = inst["regions"].getArray())
                        for (auto& r : *regions)
                            if (auto* o = r.getDynamicObject())
                                o->setProperty ("path", dir.getChildFile (o->getProperty ("path").toString()).getFullPathName());
                }

            done (index.isObject() ? index : var());
        })
        // Crash reporting: what went wrong last time, a zip to email, and helpers to send it.
        .withNativeFunction ("getCrashReport", [] (Args, Completion done) { done (Diagnostics::getPreviousSessionProblem()); })
        .withNativeFunction ("makeCrashReport", [] (Args, Completion done)
        {
            auto zip = Diagnostics::writeCrashReport();
            if (zip == File())
                return done (result ("Couldn't write the crash report"));
            zip.revealToUser();
            done (zip.getFullPathName());
        })
        .withNativeFunction ("openIssue", [] (Args args, Completion done)
        {
            // Opens a prefilled "new issue" page for the crash report. Links can't attach files,
            // so the body asks the user to drag the zip in.
            URL url ("https://github.com/guyverphd/Synthient/issues/new?title="
                     + URL::addEscapeChars (args[0].toString(), true)
                     + "&body=" + URL::addEscapeChars (args[1].toString(), true));
            done (url.launchInDefaultBrowser());
        })
        // Plays a short chord on a track so a sound can be auditioned without the transport.
        .withNativeFunction ("previewNotes", [this] (Args args, Completion done)
        {
            auto tracks = te::getAudioTracks (song->getEdit());
            const int index = args.size() > 0 ? (int) args[0] : -1;
            if (! juce::isPositiveAndBelow (index, tracks.size()))
                return done (result ("No such track"));

            Component::SafePointer<MainComponent> safe (this);
            auto track = tracks[index];
            juce::Array<int> pitches;
            if (auto* list = args[1].getArray())
                for (auto& p : *list)
                    pitches.add (juce::jlimit (0, 127, (int) p));
            const int seconds = juce::jlimit (1, 8, args.size() > 2 ? (int) args[2] : 2);

            song->getEdit().getTransport().ensureContextAllocated();
            static const auto source = te::createUniqueMPESourceID();
            for (auto p : pitches)
                track->injectLiveMidiMessage (MidiMessage::noteOn (1, p, (uint8) 96), source);

            Timer::callAfterDelay (seconds * 1000, [safe, track = te::AudioTrack::Ptr (track), pitches]
            {
                if (safe == nullptr)
                    return;
                for (auto p : pitches)
                    track->injectLiveMidiMessage (MidiMessage::noteOff (1, p), source);
            });
            done (result ({}));
        })
        .withNativeFunction ("logEvent", [] (Args args, Completion done)
        {
            Diagnostics::log ("ui: " + args[0].toString().substring (0, 2000));
            done (true);
        })
        // The user's own instruments: Decent Sampler (.dspreset/.dslibrary) and SFZ files dropped into
        // Documents\Synthient\Instruments are converted and indexed by the analysis tools.
        .withNativeFunction ("openInstrumentsFolder", [] (Args, Completion done)
        {
            auto dir = getDocumentsDir().getChildFile ("Instruments");
            dir.createDirectory();
            dir.startAsProcess();
            done (dir.getFullPathName());
        })
        .withNativeFunction ("rescanUserInstruments", [] (Args, Completion done)
        {
            auto dir = getDocumentsDir().getChildFile ("Instruments");
            dir.createDirectory();
            Sidecar::run ({ "index_user_instruments", dir.getFullPathName() }, [done, dir] (var json)
            {
                if (json.hasProperty ("error"))
                    return done (json);
                auto index = JSON::parse (dir.getChildFile ("user_index.json"));
                if (auto* obj = json.getDynamicObject())
                    obj->setProperty ("list", index["instruments"]);
                done (json);
            });
        })
        .withNativeFunction ("getLibraryPath", [] (Args, Completion done) { done (getLibraryDir().getFullPathName()); })
        // Surge XT patches (library/surge_patches.json) with absolute paths.
        .withNativeFunction ("getSynthPatchIndex", [] (Args, Completion done)
        {
            auto dir = getLibraryDir();
            auto index = JSON::parse (dir.getChildFile ("surge_patches.json"));

            if (auto* list = index["patches"].getArray())
                for (auto& p : *list)
                    if (auto* o = p.getDynamicObject())
                        o->setProperty ("path", dir.getChildFile (o->getProperty ("path").toString()).getFullPathName());

            done (index.isObject() ? index : var());
        })
        .withNativeFunction ("getSampleIndex", [] (Args, Completion done)
        {
            // The built-in Starter Kit (paths relative to the samples folder) plus the user's library.
            Array<var> samples;

            auto kitDir = getAppFolder ("samples");
            auto kit = JSON::parse (kitDir.getChildFile ("index.json"));
            if (auto* arr = kit["samples"].getArray())
                for (auto s : *arr)
                    if (auto* o = s.getDynamicObject())
                    {
                        o->setProperty ("path", kitDir.getChildFile (o->getProperty ("path").toString()).getFullPathName());
                        samples.add (s);
                    }

            auto user = JSON::parse (getAppDataDir().getChildFile ("sample_index.json"));
            if (auto* arr = user.isArray() ? user.getArray() : user["samples"].getArray())
                samples.addArray (*arr);

            auto obj = new DynamicObject();
            obj->setProperty ("samples", samples);
            done (var (obj));
        })
        // The user's MIDI clips (Documents\Synthient\MIDI Clips, any subfolders): re-indexed on each
        // call (plain file parsing, about a second for a few hundred clips), paths made absolute.
        .withNativeFunction ("getMidiClipIndex", [] (Args, Completion done)
        {
            auto dir = getDocumentsDir().getChildFile ("MIDI Clips");
            dir.createDirectory();
            Sidecar::run ({ "index_midi_clips", dir.getFullPathName() }, [done, dir] (var json)
            {
                if (json.hasProperty ("error"))
                    return done (json);

                auto index = JSON::parse (dir.getChildFile ("clip_index.json"));
                if (auto* arr = index["clips"].getArray())
                    for (auto& c : *arr)
                        if (auto* o = c.getDynamicObject())
                            o->setProperty ("path", dir.getChildFile (o->getProperty ("path").toString()).getFullPathName());

                auto obj = new DynamicObject();
                obj->setProperty ("folder", dir.getFullPathName());
                obj->setProperty ("clips", index["clips"].isArray() ? index["clips"] : var (Array<var>()));
                done (var (obj));
            });
        })

        .withNativeFunction ("exportAudio", [this] (Args args, Completion done)
        {
            const String ext = args.size() > 0 && args[0].toString().isNotEmpty() ? args[0].toString() : "wav";
            chooseFile ("Export " + ext.toUpperCase(), "*." + ext, true, [this, done, ext] (File f)
            {
                if (f == File())
                    return done (result ("cancelled"));

                auto target = f.withFileExtension (ext);
                song->exportAudio (target, [done, target] (String error) { done (result (error, target.getFullPathName())); });
            });
        })
        .withNativeFunction ("exportStems", [this] (Args args, Completion done)
        {
            const String ext = args.size() > 0 && args[0].toString().isNotEmpty() ? args[0].toString() : "wav";
            const String path = args.size() > 1 ? args[1].toString() : String();

            auto run = [this, done, ext] (File folder)
            {
                if (folder == File())
                    return done (result ("cancelled"));

                song->exportStems (folder, ext, [done, folder] (String error, StringArray files)
                {
                    done (result (error, makeFileList (folder, files)));
                });
            };

            if (path.isNotEmpty())
                return run (File (path));

            activeChooser = std::make_shared<FileChooser> ("Choose a folder for the stems", getDocumentsDir());
            activeChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                                        [run] (const FileChooser& fc) { run (fc.getResult()); });
        })
        .withNativeFunction ("exportMidi", [this] (Args args, Completion done)
        {
            const int track = args.size() > 0 ? (int) args[0] : -1;
            const String path = args.size() > 1 ? args[1].toString() : String();

            if (path.isNotEmpty())
                return done (result (song->exportMidi (File (path), track), path));

            chooseFile ("Export MIDI", "*.mid", true, [this, done, track] (File f)
            {
                if (f == File())
                    return done (result ("cancelled"));

                auto target = f.withFileExtension ("mid");
                done (result (song->exportMidi (target, track), target.getFullPathName()));
            });
        })
        .withNativeFunction ("defaultExportPath", [] (Args args, Completion done)
        {
            // Documents\Synthient\Exports\<name>.<ext>, without overwriting earlier exports.
            auto dir = getDocumentsDir().getChildFile ("Exports");
            dir.createDirectory();
            auto name = File::createLegalFileName (args[0].toString().isNotEmpty() ? args[0].toString() : "Synthient");
            done (dir.getNonexistentChildFile (name, args.size() > 1 && args[1].toString().isNotEmpty() ? "." + args[1].toString() : String(), false).getFullPathName());
        })

        // Files dropped onto the web UI arrive as base64; store them so clips can reference a real path.
        .withNativeFunction ("importMedia", [] (Args args, Completion done)
        {
            auto dir = getAppDataDir().getChildFile ("media");
            dir.createDirectory();

            auto name = File::createLegalFileName (args[0].toString());
            auto target = dir.getNonexistentChildFile (File (name).getFileNameWithoutExtension(), File (name).getFileExtension(), false);

            FileOutputStream out (target);
            if (! out.openedOk() || ! Base64::convertFromBase64 (out, args[1].toString()))
                return done (result ("Could not store " + name));

            out.flush();
            done (result ({}, target.getFullPathName()));
        })

        // Crash recovery
        .withNativeFunction ("getRecovery", [this] (Args, Completion done)
        {
            auto obj = new DynamicObject();
            obj->setProperty ("available", recoveryAvailable);
            if (recoveryAvailable)
                obj->setProperty ("time", getAutosaveFile().getLastModificationTime().toString (true, true, false));
            done (var (obj));
        })
        .withNativeFunction ("recover", [this] (Args args, Completion done)
        {
            const bool restore = args.size() > 0 && (bool) args[0];
            String error;

            if (restore)
            {
                error = song->load (getAutosaveFile());
                song->forgetFile();
            }

            recoveryAvailable = false;
            lastAutosaveHash = 0;
            done (result (error));
        });
}

std::optional<WebBrowserComponent::Resource> MainComponent::getResource (const String& url)
{
    // Served from the source tree so the UI can be edited without rebuilding.
    const File uiDir (getAppFolder ("ui"));
    auto path = url.upToFirstOccurrenceOf ("?", false, false).trimCharactersAtStart ("/");

    if (path.isEmpty())
        path = "index.html";

    // The instrument library's big indexes are fetched as files: returning them from a native
    // function is very slow (JUCE escapes the result with repeated string replaces).
    if (path == "library-data/index.json" || path == "library-data/surge_patches.json" || path == "library-data/obxf_patches.json")
    {
        MemoryBlock block;
        if (! getLibraryDir().getChildFile (path.fromFirstOccurrenceOf ("/", false, false)).loadFileAsData (block))
            return std::nullopt;

        std::vector<std::byte> data (block.getSize());
        std::memcpy (data.data(), block.getData(), block.getSize());
        return WebBrowserComponent::Resource { std::move (data), "application/json" };
    }

    auto file = uiDir.getChildFile (path);

    if (! file.existsAsFile() || ! file.isAChildOf (uiDir))
        return std::nullopt;

    MemoryBlock block;
    file.loadFileAsData (block);

    std::vector<std::byte> data (block.getSize());
    std::memcpy (data.data(), block.getData(), block.getSize());

    return WebBrowserComponent::Resource { std::move (data), mimeTypeFor (file) };
}

//==============================================================================
// On a fresh install the plug-in list is empty until the background scan runs, which is after the
// first song is made. Scan just the bundled library plug-ins (a handful) up front when needed.
void MainComponent::scanBundledPlugins()
{
    auto& pm = engine->getPluginManager();
    for (auto& d : pm.knownPluginList.getTypes())
        if (d.name.containsIgnoreCase ("Surge XT"))
            return;

    auto folder = getLibraryDir().getChildFile ("plugins");
    if (! folder.isDirectory())
        return;

    for (auto* format : pm.pluginFormatManager.getFormats())
    {
        if (! format->getName().containsIgnoreCase ("VST3"))
            continue;

        PluginDirectoryScanner scanner (pm.knownPluginList, *format, FileSearchPath (folder.getFullPathName()), true,
                                        engine->getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"), true);
        String name;
        while (scanner.scanNextFile (true, name)) {}
    }
}

void MainComponent::emitToUi (const var& event)
{
    if (browser != nullptr)
        browser->emitEventIfBrowserIsVisible ("claudedaw", event);
}

void MainComponent::openStemsWindow (const File& source)
{
    Diagnostics::log ("Separate stems: " + source.getFileName());
    if (! source.existsAsFile())
        return;

    auto outDir = getDocumentsDir().getChildFile ("Stems")
                      .getNonexistentChildFile (File::createLegalFileName (source.getFileNameWithoutExtension()), {});
    outDir.createDirectory();

    StemsWindow::Callbacks cb;
    cb.addAsTracks = [this] (const var& stems) { return addStemsAsTracks (stems); };
    cb.finished = [this, source] (const var& stems)
    {
        auto obj = new DynamicObject();
        obj->setProperty ("type", "stemsReady");
        obj->setProperty ("source", source.getFullPathName());
        obj->setProperty ("stems", stems);
        emitToUi (var (obj));
    };
    cb.closed = [this] (StemsWindow& w)
    {
        Component::SafePointer<StemsWindow> safe (&w);
        MessageManager::callAsync ([this, safe] { if (safe != nullptr) stemsWindows.removeObject (safe.getComponent()); });
    };

    stemsWindows.add (new StemsWindow (source, outDir, std::move (cb)));
}

String MainComponent::addStemsAsTracks (const var& stems)
{
    auto* obj = stems.getDynamicObject();
    if (obj == nullptr)
        return "No stems to add";

    int added = 0;
    for (auto name : { "drums", "bass", "vocals", "other" })
    {
        File f (obj->getProperty (name)["path"].toString());
        if (! f.existsAsFile())
            continue;

        // Make the song long enough for the stem.
        te::AudioFile audio (*engine, f);
        auto& edit = song->getEdit();
        const double beats = edit.tempoSequence.toBeats (te::TimePosition::fromSeconds (audio.getLength())).inBeats();
        const int bars = (int) std::ceil (beats / 4.0);
        if (bars > (int) song->getState (false)["bars"])
        {
            auto length = new DynamicObject();
            length->setProperty ("bars", juce::jmin (bars, 256));
            song->runTool ("set_song_length", var (length));
        }

        auto label = String (name).substring (0, 1).toUpperCase() + String (name).substring (1) + " stem";
        auto track = song->runTool ("add_track", JSON::parse ("{\"instrument\": \"None\", \"name\": " + JSON::toString (label) + "}"));
        if (! (bool) track["ok"])
            return track["error"].toString();

        auto input = new DynamicObject();
        input->setProperty ("track", track["track"]);
        input->setProperty ("path", f.getFullPathName());
        input->setProperty ("start_beat", 0.0);
        auto clip = song->runTool ("add_audio_clip", var (input));
        if (! (bool) clip["ok"])
            return clip["error"].toString();
        ++added;
    }

    auto event = new DynamicObject();
    event->setProperty ("type", "songChanged");
    event->setProperty ("message", "Added " + String (added) + " stem tracks");
    emitToUi (var (event));
    return added > 0 ? String() : "No stem files found";
}

void MainComponent::chooseFile (const String& title, const String& patterns, bool forSaving,
                                std::function<void (File)> onChosen)
{
    auto start = song->getFile() != File() ? song->getFile().getParentDirectory() : getDocumentsDir();
    activeChooser = std::make_shared<FileChooser> (title, start, patterns);

    auto flags = forSaving ? FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting
                           : FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles;

    activeChooser->launchAsync (flags, [onChosen] (const FileChooser& fc) { onChosen (fc.getResult()); });
}

void MainComponent::prepareDefaultScanPaths()
{
    // The "instruments" folder next to the project is always included in VST3 scans.
    auto& props = engine->getPropertyStorage().getPropertiesFile();
    const String key = "lastPluginScanPath_VST3";

    // Always include the app's instruments folder and the downloaded library's plug-ins.
    FileSearchPath paths (props.getValue (key, "C:\\Program Files\\Common Files\\VST3"));
    File instruments (getAppFolder ("instruments"));
    instruments.createDirectory();
    paths.addIfNotAlreadyThere (instruments);

    auto libraryPlugins = getLibraryDir().getChildFile ("plugins");
    if (libraryPlugins.isDirectory())
        paths.addIfNotAlreadyThere (libraryPlugins);

    props.setValue (key, paths.toString());
}

void MainComponent::showPluginScanner()
{
    DialogWindow::LaunchOptions o;
    o.dialogTitle = "Plug-ins: click Options > Scan for new or updated VST3 plug-ins";
    o.dialogBackgroundColour = Colour (0xff191c23);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;

    auto& pm = engine->getPluginManager();
    auto list = new PluginListComponent (pm.pluginFormatManager, pm.knownPluginList,
                                         engine->getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"),
                                         std::addressof (engine->getPropertyStorage().getPropertiesFile()),
                                         true);
    list->setSize (900, 600);
    o.content.setOwned (list);
    o.launchAsync();
}

void MainComponent::showAudioSettings()
{
    DialogWindow::LaunchOptions o;
    o.dialogTitle = "Audio settings";
    o.dialogBackgroundColour = Colour (0xff191c23);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;

    auto selector = new AudioDeviceSelectorComponent (engine->getDeviceManager().deviceManager,
                                                      0, 512, 1, 512, true, false, true, true);
    selector->setSize (500, 500);
    o.content.setOwned (selector);
    o.launchAsync();
}
