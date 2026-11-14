#pragma once

#include <JuceHeader.h>
#include <map>
#include <mutex>

// Lets Claude Code (through analysis/synthient_mcp.py) use the same tools as the in-app chat.
// A tiny HTTP server on 127.0.0.1 with a random port and a per-session token, both written to
// bridge.json in the user data folder. Each request is handed to the web UI, which runs the tool
// exactly like the chat does and sends the result back with reply().
//
//   GET  /tools          -> {"tools": [...], "instructions": "..."}
//   POST /call {name, input} -> {"result": ..., "content": [...]?}
//   Header: Authorization: Bearer <token>
class ToolBridge : private juce::Thread
{
public:
    // Called on the message thread: pass the request on to the UI (id is used for reply()).
    using Dispatch = std::function<void (int id, const juce::var& request)>;

    explicit ToolBridge (Dispatch);
    ~ToolBridge() override;

    // Starts listening and writes bridge.json. Called once the UI is ready to answer.
    void start();

    // The UI's answer (a JSON string) for request id. Any thread.
    void reply (int id, const juce::String& json);

private:
    void run() override;
    void handle (juce::StreamingSocket&);
    juce::String forward (const juce::var& request, int timeoutMs);
    juce::File getInfoFile() const;

    struct Pending
    {
        juce::WaitableEvent done;
        juce::String json;
    };

    Dispatch dispatch;
    juce::StreamingSocket listener;
    juce::String token;
    std::mutex lock;
    std::map<int, std::shared_ptr<Pending>> pending;
    int nextId = 1;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    bool started = false;

    JUCE_DECLARE_NON_COPYABLE (ToolBridge)
};
