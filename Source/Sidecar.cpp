#include "Sidecar.h"
#include "Paths.h"

#include <cstdlib>

// A developer setup has analysis\.venv; the installer ships a self-contained Python in analysis\python.
static juce::File findPython (const juce::File& dir)
{
    auto venv = dir.getChildFile (".venv").getChildFile ("Scripts").getChildFile ("python.exe");
    return venv.existsAsFile() ? venv : dir.getChildFile ("python").getChildFile ("python.exe");
}

juce::File Sidecar::getAnalysisDir()
{
    auto dir = getAppFolder ("analysis");

    // An install without its own Python tools can borrow a set-up copy named in analysis_path.txt.
    if (! findPython (dir).existsAsFile())
    {
        auto pointer = getUserDataDir().getChildFile ("analysis_path.txt");
        juce::File other (pointer.existsAsFile() ? pointer.loadFileAsString().trim() : juce::String());

        if (pointer.existsAsFile() && findPython (other).existsAsFile() && other.getChildFile ("cli.py").existsAsFile())
            return other;
    }

    return dir;
}

namespace
{
    juce::var errorObject (const juce::String& message)
    {
        auto obj = new juce::DynamicObject();
        obj->setProperty ("error", message);
        return juce::var (obj);
    }
}

void Sidecar::runStreaming (const juce::StringArray& toolAndArgs, std::shared_ptr<Job> job,
                            std::function<void (juce::var)> onLine, std::function<void (juce::var)> onDone)
{
    _putenv_s ("PYTHONIOENCODING", "utf-8");
    _putenv_s ("PYTHONUTF8", "1");
    _putenv_s ("PYTHONWARNINGS", "ignore");

    juce::Thread::launch ([toolAndArgs, job, onLine = std::move (onLine), onDone = std::move (onDone)]
    {
        auto dir = getAnalysisDir();
        auto python = findPython (dir);
        auto cli = dir.getChildFile ("cli.py");
        juce::var last;

        if (! python.existsAsFile() || ! cli.existsAsFile())
        {
            last = errorObject ("The analysis tools (Python + Demucs) aren't set up for this copy of Synthient.");
        }
        else
        {
            juce::StringArray args { python.getFullPathName(), cli.getFullPathName() };
            args.addArray (toolAndArgs);
            auto& process = job->process;

            if (job->cancelled || ! process.start (args, juce::ChildProcess::wantStdOut))
            {
                last = errorObject (job->cancelled ? "cancelled" : "Could not start the analysis tool");
            }
            else
            {
                juce::MemoryBlock lineBytes;

                // One byte at a time: JUCE's read waits until its buffer is full, which would hold
                // progress lines back. The tool prints little, so this is cheap.
                for (char c; ! job->cancelled && process.readProcessOutput (&c, 1) == 1;)
                {
                    if (c != '\n')
                    {
                        lineBytes.append (&c, 1);
                        continue;
                    }

                    auto line = juce::String::fromUTF8 (static_cast<const char*> (lineBytes.getData()), (int) lineBytes.getSize()).trim();
                    lineBytes.reset();

                    {
                        if (! line.startsWithChar ('{'))
                            continue;

                        auto json = juce::JSON::parse (line);
                        if (! json.isObject())
                            continue;

                        last = json;
                        juce::MessageManager::callAsync ([onLine, json] { onLine (json); });
                    }
                }

                if (job->cancelled)
                    last = errorObject ("cancelled");
                else if (! last.isObject())
                    last = errorObject ("The tool stopped without a result (exit code " + juce::String ((int) process.getExitCode()) + ")");
            }
        }

        juce::MessageManager::callAsync ([onDone, last] { onDone (last); });
    });
}

void Sidecar::run (const juce::StringArray& toolAndArgs, std::function<void (juce::var)> onDone)
{
    // Same environment run.cmd sets up; inherited by the child process.
    static const bool envReady = []
    {
        _putenv_s ("PYTHONIOENCODING", "utf-8");
        _putenv_s ("PYTHONUTF8", "1");
        _putenv_s ("PYTHONWARNINGS", "ignore");
        return true;
    }();
    juce::ignoreUnused (envReady);

    juce::Thread::launch ([toolAndArgs, onDone = std::move (onDone)]
    {
        juce::var result;

        auto makeError = [] (const juce::String& message)
        {
            auto obj = new juce::DynamicObject();
            obj->setProperty ("error", message);
            return juce::var (obj);
        };

        // Run the venv's python directly rather than through cmd.exe, whose quoting
        // breaks on paths with spaces, '&' or '%'. JUCE quotes each argument itself.
        auto dir = getAnalysisDir();
        auto python = findPython (dir);
        auto cli = dir.getChildFile ("cli.py");

        if (! cli.existsAsFile())
        {
            result = makeError ("Analysis tools are not installed (missing " + cli.getFullPathName() + ")");
        }
        else if (! python.existsAsFile())
        {
            result = makeError ("Analysis tools are not set up yet. Run " + dir.getChildFile ("setup.cmd").getFullPathName());
        }
        else
        {
            juce::StringArray args { python.getFullPathName(), cli.getFullPathName() };
            args.addArray (toolAndArgs);

            juce::ChildProcess process;

            if (! process.start (args, juce::ChildProcess::wantStdOut))
            {
                result = makeError ("Could not start the analysis tool");
            }
            else
            {
                auto output = process.readAllProcessOutput();
                process.waitForProcessToFinish (5000);

                // Tools print exactly one JSON object as their last line.
                juce::StringArray lines;
                lines.addLines (output);
                for (int i = lines.size(); --i >= 0;)
                {
                    if (lines[i].trimStart().startsWithChar ('{'))
                    {
                        result = juce::JSON::parse (lines[i]);
                        break;
                    }
                }

                if (! result.isObject())
                    result = makeError ("Analysis tool returned no JSON (exit code "
                                        + juce::String ((int) process.getExitCode()) + "): "
                                        + output.trim().getLastCharacters (500));
            }
        }

        juce::MessageManager::callAsync ([onDone, result] { onDone (result); });
    });
}
