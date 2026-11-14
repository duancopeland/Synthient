#include "Bridge.h"
#include "Diagnostics.h"
#include "Paths.h"
#include <random>
#include <process.h>

using namespace juce;

namespace
{
    String randomToken()
    {
        std::random_device rd;   // MSVC: backed by the OS cryptographic generator
        String s;
        for (int i = 0; i < 8; ++i)
            s << String::toHexString ((int) rd()).paddedLeft ('0', 8);
        return s;
    }

    void sendResponse (StreamingSocket& s, int status, const String& body)
    {
        const auto text = status == 200 ? "OK" : status == 401 ? "Unauthorized" : status == 404 ? "Not Found" : "Bad Request";
        MemoryOutputStream out;
        out << "HTTP/1.1 " << status << " " << text << "\r\n"
            << "Content-Type: application/json; charset=utf-8\r\n"
            << "Content-Length: " << (int) body.getNumBytesAsUTF8() << "\r\n"
            << "Connection: close\r\n\r\n";
        out << body;   // MemoryOutputStream writes Strings as UTF-8

        auto* data = static_cast<const char*> (out.getData());
        size_t sent = 0;
        while (sent < out.getDataSize())
        {
            const int n = s.write (data + sent, (int) (out.getDataSize() - sent));
            if (n <= 0)
                break;
            sent += (size_t) n;
        }
    }

    String errorJson (const String& message)
    {
        auto obj = new DynamicObject();
        obj->setProperty ("error", message);
        return JSON::toString (var (obj), true);
    }
}

ToolBridge::ToolBridge (Dispatch d) : Thread ("Claude Code bridge"), dispatch (std::move (d)) {}

ToolBridge::~ToolBridge()
{
    *alive = false;
    listener.close();   // unblocks waitForNextConnection
    {
        std::lock_guard<std::mutex> g (lock);
        for (auto& [id, p] : pending)
        {
            p->json = errorJson ("Synthient is closing.");
            p->done.signal();
        }
    }
    stopThread (5000);

    // Only remove the file if it is still ours (another Synthient may have started since).
    auto f = getInfoFile();
    if (started && JSON::parse (f.loadFileAsString())["token"].toString() == token)
        f.deleteFile();
}

File ToolBridge::getInfoFile() const   { return getUserDataDir().getChildFile ("bridge.json"); }

void ToolBridge::start()
{
    if (started)
    {
        // The page was reloaded: requests it was working on will never be answered.
        std::lock_guard<std::mutex> g (lock);
        for (auto& [id, p] : pending)
        {
            p->json = errorJson ("Synthient's screen reloaded during this call. Check the song with get_song, then try again.");
            p->done.signal();
        }
        return;
    }

    if (! listener.createListener (0, "127.0.0.1"))
    {
        Diagnostics::log ("Claude Code bridge: could not open a local port");
        return;
    }

    started = true;
    token = randomToken();

    auto info = new DynamicObject();
    info->setProperty ("port", listener.getBoundPort());
    info->setProperty ("token", token);
    info->setProperty ("pid", _getpid());
    getInfoFile().getParentDirectory().createDirectory();
    getInfoFile().replaceWithText (JSON::toString (var (info)));
    Diagnostics::log ("Claude Code bridge listening on 127.0.0.1:" + String (listener.getBoundPort()));

    startThread();
}

void ToolBridge::reply (int id, const String& json)
{
    std::lock_guard<std::mutex> g (lock);
    auto it = pending.find (id);
    if (it == pending.end())
        return;
    it->second->json = json;
    it->second->done.signal();
}

void ToolBridge::run()
{
    // One request at a time: tool calls change the song, so they run in order anyway.
    while (! threadShouldExit())
    {
        std::unique_ptr<StreamingSocket> client (listener.waitForNextConnection());
        if (client == nullptr || threadShouldExit())
            break;
        handle (*client);
        client->close();
    }
}

void ToolBridge::handle (StreamingSocket& s)
{
    // Read the header block.
    MemoryBlock buffer;
    int headerEnd = -1;
    char chunk[4096];

    while (headerEnd < 0)
    {
        if (s.waitUntilReady (true, 10000) != 1)
            return;
        const int n = s.read (chunk, sizeof (chunk), false);
        if (n <= 0)
            return;
        buffer.append (chunk, (size_t) n);
        if (buffer.getSize() > 64 * 1024)
            return sendResponse (s, 400, errorJson ("Header too large"));

        auto text = String::fromUTF8 (static_cast<const char*> (buffer.getData()), (int) buffer.getSize());
        headerEnd = text.indexOf ("\r\n\r\n");
    }

    const auto all = String::fromUTF8 (static_cast<const char*> (buffer.getData()), (int) buffer.getSize());
    const auto head = all.substring (0, headerEnd);
    StringArray lines;
    lines.addLines (head);
    const auto requestLine = StringArray::fromTokens (lines[0], " ", "");
    const auto method = requestLine[0], path = requestLine[1];

    String auth;
    int contentLength = 0;
    for (int i = 1; i < lines.size(); ++i)
    {
        const auto name = lines[i].upToFirstOccurrenceOf (":", false, false).trim().toLowerCase();
        const auto value = lines[i].fromFirstOccurrenceOf (":", false, false).trim();
        if (name == "authorization")        auth = value;
        else if (name == "content-length")  contentLength = value.getIntValue();
    }

    if (auth != "Bearer " + token)
        return sendResponse (s, 401, errorJson ("Missing or wrong token"));

    if (contentLength < 0 || contentLength > 16 * 1024 * 1024)
        return sendResponse (s, 400, errorJson ("Bad Content-Length"));

    // The body: whatever came after the header, plus the rest.
    const int headerBytes = head.getNumBytesAsUTF8() + 4;
    MemoryBlock body (static_cast<const char*> (buffer.getData()) + headerBytes, buffer.getSize() - (size_t) headerBytes);
    while ((int) body.getSize() < contentLength)
    {
        if (s.waitUntilReady (true, 10000) != 1)
            return;
        const int n = s.read (chunk, jmin ((int) sizeof (chunk), contentLength - (int) body.getSize()), false);
        if (n <= 0)
            return;
        body.append (chunk, (size_t) n);
    }

    if (method == "GET" && path == "/tools")
        return sendResponse (s, 200, forward (JSON::parse ("{\"name\": \"__tools\"}"), 30000));

    if (method == "POST" && path == "/call")
    {
        auto request = JSON::parse (body.toString());
        if (request["name"].toString().isEmpty())
            return sendResponse (s, 400, errorJson ("Expected {name, input}"));
        // Rendering and analysis can take minutes.
        return sendResponse (s, 200, forward (request, 20 * 60 * 1000));
    }

    sendResponse (s, 404, errorJson ("Unknown endpoint"));
}

String ToolBridge::forward (const var& request, int timeoutMs)
{
    auto p = std::make_shared<Pending>();
    int id;
    {
        std::lock_guard<std::mutex> g (lock);
        id = nextId++;
        pending[id] = p;
    }

    MessageManager::callAsync ([this, flag = alive, id, request]
    {
        if (*flag)
            dispatch (id, request);
    });

    const bool answered = p->done.wait (timeoutMs);

    std::lock_guard<std::mutex> g (lock);
    pending.erase (id);
    return answered ? p->json : errorJson ("Synthient didn't answer in time (is a dialog open?).");
}
