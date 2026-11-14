#include "Diagnostics.h"
#include "Paths.h"

bool writeMiniDumpW (const wchar_t* path, void* exceptionPointers, bool fromOtherThread);   // WinNotify.cpp

static bool writeMiniDump (const juce::String& path, void* exceptionPointers, bool fromOtherThread)
{
    return writeMiniDumpW (path.toWideCharPointer(), exceptionPointers, fromOtherThread);
}

namespace Diagnostics
{
namespace
{
    juce::CriticalSection logLock;
    std::unique_ptr<juce::FileOutputStream> logStream;
    std::atomic<juce::int64> lastHeartbeat { 0 };

    juce::String stamp (bool forFileName)
    {
        return juce::Time::getCurrentTime().formatted (forFileName ? "%Y%m%d-%H%M%S" : "%Y-%m-%d %H:%M:%S");
    }

    // Keeps the newest few reports so the folder doesn't grow forever.
    void pruneReports()
    {
        for (auto pattern : { "crash-*", "freeze-*" })
        {
            auto files = getLogFolder().findChildFiles (juce::File::findFiles, false, pattern);
            std::sort (files.begin(), files.end(), [] (auto& a, auto& b) { return a.getLastModificationTime() > b.getLastModificationTime(); });
            for (int i = 6; i < files.size(); ++i)
                files.getReference (i).deleteFile();
        }
    }

    void crashHandler (void* exceptionPointers)
    {
        const auto name = "crash-" + stamp (true);
        const auto dump = getLogFolder().getChildFile (name + ".dmp");
        const bool dumped = writeMiniDump (dump.getFullPathName(), exceptionPointers, false);

        const auto trace = juce::SystemStats::getStackBacktrace();
        getLogFolder().getChildFile (name + ".txt").replaceWithText ("Synthient crashed at " + stamp (false) + "\n\n" + trace);
        log ("CRASH (report " + name + (dumped ? ", with dump" : "") + ")\n" + trace);
    }

    // Notices when the message thread stops responding (a hang looks like a crash to the user).
    class Watchdog  : public juce::Thread,
                      private juce::Timer
    {
    public:
        Watchdog() : juce::Thread ("Synthient watchdog")
        {
            lastHeartbeat = juce::Time::currentTimeMillis();
            startTimer (250);
            startThread (juce::Thread::Priority::low);
        }

        ~Watchdog() override
        {
            stopTimer();
            stopThread (2000);
        }

    private:
        void timerCallback() override { lastHeartbeat = juce::Time::currentTimeMillis(); }

        void run() override
        {
            bool frozen = false, dumped = false;
            juce::int64 frozenSince = 0;

            while (! threadShouldExit())
            {
                wait (1000);
                const auto now = juce::Time::currentTimeMillis();
                const auto silentFor = now - lastHeartbeat.load();

                if (! frozen && silentFor > 10000)
                {
                    frozen = true;
                    frozenSince = lastHeartbeat.load();
                    log ("FREEZE: the window hasn't responded for " + juce::String (silentFor / 1000) + " s");

                    if (! dumped) // one dump per session is enough to see where it's stuck
                    {
                        const auto dump = getLogFolder().getChildFile ("freeze-" + stamp (true) + ".dmp");
                        dumped = writeMiniDump (dump.getFullPathName(), nullptr, true);
                        log (dumped ? "  wrote " + dump.getFileName() : juce::String ("  could not write a dump"));
                    }
                }
                else if (frozen && silentFor < 2000)
                {
                    frozen = false;
                    log ("Responsive again after " + juce::String ((now - frozenSince) / 1000) + " s");
                }
            }
        }
    };

    std::unique_ptr<Watchdog> watchdog;

    // What the previous session left behind (read before this session starts logging).
    juce::String previousLog;
    juce::Time previousStart;
    bool previousEndedBadly = false;

    void inspectPreviousSession (const juce::File& logFile)
    {
        const auto text = logFile.loadFileAsString();
        const auto start = text.lastIndexOf ("---- Synthient started");
        if (start < 0)
            return;

        const auto lineStart = text.substring (0, start).lastIndexOfChar ('\n') + 1;
        previousLog = text.substring (lineStart);
        previousEndedBadly = ! previousLog.contains ("---- Synthient closed normally");

        // "2026-10-31 10:39:31  ---- ..." -> time
        const auto stampText = previousLog.substring (0, 19);
        previousStart = juce::Time (stampText.substring (0, 4).getIntValue(), stampText.substring (5, 7).getIntValue() - 1,
                                    stampText.substring (8, 10).getIntValue(), stampText.substring (11, 13).getIntValue(),
                                    stampText.substring (14, 16).getIntValue(), stampText.substring (17, 19).getIntValue());
    }

    juce::Array<juce::File> previousReports()
    {
        juce::Array<juce::File> result;
        for (auto& f : getLogFolder().findChildFiles (juce::File::findFiles, false, "crash-*;freeze-*"))
            if (f.getLastModificationTime() >= previousStart - juce::RelativeTime::seconds (5))
                result.add (f);
        return result;
    }
}

juce::File getLogFolder()
{
    auto dir = getUserDataDir().getChildFile ("logs");
    dir.createDirectory();
    return dir;
}

void start()
{
    auto file = getLogFolder().getChildFile ("Synthient.log");
    inspectPreviousSession (file);

    if (file.getSize() > 2 * 1024 * 1024) // keep one older log around
    {
        auto old = getLogFolder().getChildFile ("Synthient.old.log");
        old.deleteFile();
        file.moveFileTo (old);
    }

    {
        const juce::ScopedLock sl (logLock);
        logStream = std::make_unique<juce::FileOutputStream> (file);

        // Another copy of the app (or a stuck one) holds the log: write a separate log rather than none.
        if (! logStream->openedOk())
            logStream = std::make_unique<juce::FileOutputStream> (getLogFolder().getChildFile ("Synthient-" + stamp (true) + ".log"));

        if (! logStream->openedOk())
            logStream = nullptr;
    }

    pruneReports();
    juce::SystemStats::setApplicationCrashHandler (crashHandler);
    log ("---- Synthient started (" + juce::SystemStats::getOperatingSystemName() + ", "
         + juce::String (juce::SystemStats::getMemorySizeInMegabytes()) + " MB RAM, built " __DATE__ " " __TIME__ ")");
    watchdog = std::make_unique<Watchdog>();
}

juce::var getPreviousSessionProblem()
{
    auto obj = new juce::DynamicObject();
    obj->setProperty ("crashed", previousEndedBadly);

    if (previousEndedBadly)
    {
        juce::String kind = "closed";
        for (auto& f : previousReports())
            kind = f.getFileName().startsWith ("crash") ? juce::String ("crash") : (kind == "crash" ? kind : juce::String ("freeze"));
        if (previousLog.contains ("  CRASH (report")) kind = "crash";
        else if (kind == "closed" && previousLog.contains ("FREEZE")) kind = "freeze";

        obj->setProperty ("kind", kind);
        obj->setProperty ("time", previousStart.formatted ("%Y-%m-%d %H:%M"));
        obj->setProperty ("log", previousLog.getLastCharacters (20000));
    }

    return juce::var (obj);
}

juce::File writeCrashReport()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Synthient").getChildFile ("Crash reports");
    dir.createDirectory();
    auto zipFile = dir.getChildFile ("Synthient-crash-report-" + previousStart.formatted ("%Y%m%d-%H%M%S") + ".zip");

    juce::ZipFile::Builder zip;
    auto logCopy = juce::File::createTempFile (".log");
    logCopy.replaceWithText (previousLog);
    zip.addFile (logCopy, 9, "Synthient-session.log");

    juce::int64 total = 0;
    for (auto& f : previousReports())
    {
        // Keep the zip small enough to email (dumps compress well, but cap them anyway).
        if (f.hasFileExtension ("dmp") && total + f.getSize() > 60 * 1024 * 1024)
            continue;
        total += f.getSize();
        zip.addFile (f, 9, f.getFileName());
    }

    zipFile.deleteFile();
    juce::FileOutputStream out (zipFile);
    const bool ok = out.openedOk() && zip.writeToStream (out, nullptr);
    out.flush();
    logCopy.deleteFile();
    return ok ? zipFile : juce::File();
}

void stop()
{
    watchdog = nullptr;
    log ("---- Synthient closed normally");
    const juce::ScopedLock sl (logLock);
    logStream = nullptr;
}

void log (const juce::String& line)
{
    const juce::ScopedLock sl (logLock);
    if (logStream == nullptr)
        return;

    *logStream << stamp (false) << "  " << line << "\r\n";
    logStream->flush();
}
}
