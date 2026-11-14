"""MCP server that lets Claude Code drive Synthient with the same tools as the in-app chat.

Claude Code starts this script and talks MCP (JSON-RPC, one message per line) over stdin/stdout.
Each tool call is passed to the running Synthient app through its local bridge: a small HTTP
server on 127.0.0.1 whose port and token Synthient writes to bridge.json in its data folder
(%APPDATA%\\Synthient, or SYNTHIENT_DATA_DIR when set). Standard library only.

Setup and examples: docs/CLAUDE_CODE.md
"""

import json
import os
import sys
import urllib.error
import urllib.request

SERVER_INFO = {"name": "synthient", "title": "Synthient", "version": "0.1.0"}
# Newest first. We answer with the client's version when we know it, else the newest.
PROTOCOL_VERSIONS = ["2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"]

NOT_RUNNING = (
    "Synthient isn't running (or was started before this connection). Start Synthient, "
    "then reconnect the synthient server in Claude Code (/mcp)."
)

BASE_INSTRUCTIONS = (
    "Synthient is the user's music workstation (a DAW). These tools change the song open in "
    "Synthient; the user sees and hears every change immediately. Call get_instructions once "
    "before composing: it holds the composing guide and the user's producer memory (mix rules, "
    "genre notes, preferences), which you must follow. Call get_song to see the current state and "
    "get_selection to see what the user selected. Explain what you did in plain musical words: "
    "the user is a musician, not a programmer."
)


def data_dir():
    override = os.environ.get("SYNTHIENT_DATA_DIR")
    if override:
        return override
    return os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "Synthient")


class BridgeError(Exception):
    pass


def bridge_request(path, payload=None, timeout=1500):
    try:
        with open(os.path.join(data_dir(), "bridge.json"), encoding="utf-8") as f:
            info = json.load(f)
    except (OSError, ValueError):
        raise BridgeError(NOT_RUNNING)

    url = f"http://127.0.0.1:{int(info['port'])}{path}"
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="GET" if data is None else "POST")
    req.add_header("Authorization", f"Bearer {info['token']}")
    if data is not None:
        req.add_header("Content-Type", "application/json")
    # Never route localhost traffic through a system proxy.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(req, timeout=timeout) as resp:
            body = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        raise BridgeError(f"Synthient refused the request ({e.code}). Restart Synthient and reconnect.")
    except (urllib.error.URLError, ConnectionError, TimeoutError, OSError):
        raise BridgeError(NOT_RUNNING)
    if isinstance(body, dict) and body.get("error") and "result" not in body:
        raise BridgeError(str(body["error"]))
    return body


def to_mcp_tool(t):
    return {
        "name": t["name"],
        "description": t.get("description", ""),
        "inputSchema": t.get("input_schema") or {"type": "object", "properties": {}},
    }


def to_mcp_content(block):
    if block.get("type") == "image":
        src = block.get("source", {})
        return {"type": "image", "data": src.get("data", ""), "mimeType": src.get("media_type", "image/png")}
    if block.get("type") == "text":
        return {"type": "text", "text": block.get("text", "")}
    return {"type": "text", "text": json.dumps(block)}


def list_tools():
    return [to_mcp_tool(t) for t in bridge_request("/tools").get("tools", [])]


def call_tool(name, arguments):
    try:
        reply = bridge_request("/call", {"name": name, "input": arguments or {}})
    except BridgeError as e:
        return {"content": [{"type": "text", "text": str(e)}], "isError": True}
    result = reply.get("result")
    if reply.get("content"):
        content = [to_mcp_content(b) for b in reply["content"]]
    else:
        content = [{"type": "text", "text": json.dumps(result, ensure_ascii=False)}]
    is_error = isinstance(result, dict) and result.get("ok") is False
    return {"content": content, "isError": is_error}


def instructions():
    # Claude Code adds these to its system prompt. Include the full guide when Synthient is up.
    try:
        r = call_tool("get_instructions", {})
        if not r["isError"]:
            guide = json.loads(r["content"][0]["text"]).get("instructions", "")
            return BASE_INSTRUCTIONS + "\n\n" + guide
    except Exception:
        pass
    return BASE_INSTRUCTIONS + "\n\n(" + NOT_RUNNING + ")"


def handle(msg):
    method = msg.get("method")
    params = msg.get("params") or {}

    if method == "initialize":
        asked = params.get("protocolVersion")
        return {
            "protocolVersion": asked if asked in PROTOCOL_VERSIONS else PROTOCOL_VERSIONS[0],
            "capabilities": {"tools": {"listChanged": False}, "prompts": {"listChanged": False}},
            "serverInfo": SERVER_INFO,
            "instructions": instructions(),
        }
    if method == "ping":
        return {}
    if method == "tools/list":
        try:
            return {"tools": list_tools()}
        except BridgeError as e:
            # Still offer get_instructions so Claude can report the problem clearly.
            return {"tools": [{"name": "get_instructions", "description": str(e),
                               "inputSchema": {"type": "object", "properties": {}}}]}
    if method == "tools/call":
        return call_tool(params.get("name", ""), params.get("arguments"))
    if method == "prompts/list":
        return {"prompts": [{"name": "compose", "title": "Compose in Synthient",
                             "description": "Load Synthient's composing guide and producer memory, then work on the song.",
                             "arguments": [{"name": "request", "description": "What to make or change", "required": False}]}]}
    if method == "prompts/get":
        request = (params.get("arguments") or {}).get("request", "")
        text = instructions() + ("\n\nThe user's request: " + request if request else "")
        return {"description": "Synthient composing guide", "messages": [{"role": "user", "content": {"type": "text", "text": text}}]}
    raise LookupError(method)


def main():
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")

    def send(obj):
        sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
        sys.stdout.flush()

    for line in sys.stdin:
        line = line.strip().lstrip("﻿")  # PowerShell pipes add a BOM
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            send({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "Parse error"}})
            continue
        if "id" not in msg or "method" not in msg:
            continue  # notifications (e.g. notifications/initialized) and stray responses get no reply
        try:
            send({"jsonrpc": "2.0", "id": msg["id"], "result": handle(msg)})
        except LookupError:
            send({"jsonrpc": "2.0", "id": msg["id"], "error": {"code": -32601, "message": f"Method not found: {msg['method']}"}})
        except Exception as e:
            print(f"synthient_mcp: {e!r}", file=sys.stderr)
            send({"jsonrpc": "2.0", "id": msg["id"], "error": {"code": -32603, "message": str(e)}})


if __name__ == "__main__":
    main()
