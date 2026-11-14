#include "ClaudeClient.h"

namespace
{
    juce::String errorJson (const juce::String& message)
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty ("type", "error");
        auto err = std::make_unique<juce::DynamicObject>();
        err->setProperty ("type", "client_error");
        err->setProperty ("message", message);
        obj->setProperty ("error", juce::var (err.release()));
        return juce::JSON::toString (juce::var (obj.release()), true);
    }
}

void ClaudeClient::postMessages (const juce::String& requestBodyJson,
                                 std::function<void (juce::String)> onDone)
{
    juce::Thread::launch ([requestBodyJson, onDone = std::move (onDone)]
    {
        juce::String result;
        const auto apiKey = juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {});

        if (apiKey.isEmpty())
        {
            result = errorJson ("ANTHROPIC_API_KEY is not set. Set it in your Windows environment variables and restart Synthient.");
        }
        else
        {
            const juce::String headers = "Content-Type: application/json\r\n"
                                         "anthropic-version: 2023-06-01\r\n"
                                         "anthropic-beta: server-side-fallback-2026-07-01\r\n"
                                         "x-api-key: " + apiKey + "\r\n";
            int statusCode = 0;

            auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                               .withExtraHeaders (headers)
                               .withConnectionTimeoutMs (600000)
                               .withStatusCode (&statusCode)
                               .withHttpRequestCmd ("POST");

            auto url = juce::URL ("https://api.anthropic.com/v1/messages").withPOSTData (requestBodyJson);

            if (auto stream = url.createInputStream (options))
                result = stream->readEntireStreamAsString();
            else
                result = errorJson ("Could not connect to api.anthropic.com");

            if (result.isEmpty())
                result = errorJson ("Empty response (HTTP " + juce::String (statusCode) + ")");
        }

        juce::MessageManager::callAsync ([onDone, result] { onDone (result); });
    });
}
