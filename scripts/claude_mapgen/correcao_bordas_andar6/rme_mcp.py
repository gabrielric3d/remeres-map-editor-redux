"""Tiny client for the editor's MCP server: python rme_mcp.py <tool|--list> [json-args]

Prints the tool's text result; images are saved next to this script and their
path printed, so they can be opened with the Read tool.
"""
import base64
import json
import os
import re
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))


def _config_text():
    # The editor's config.toml (repo root, next to rme.exe) holds the MCP port and token.
    d = HERE
    for _ in range(6):
        p = os.path.join(d, "config.toml")
        if os.path.exists(p):
            return open(p, encoding="utf-8").read()
        d = os.path.dirname(d)
    return ""


_CFG = _config_text()
_PORT = re.search(r"claude_mcp_port\s*=\s*(\d+)", _CFG)
_TOKEN = re.search(r"""claude_mcp_token\s*=\s*['"]([^'"]+)['"]""", _CFG)
URL = "http://127.0.0.1:%d/mcp" % int(os.environ.get("RME_MCP_PORT") or (_PORT.group(1) if _PORT else 7777))
TOKEN = os.environ.get("RME_MCP_TOKEN") or (_TOKEN.group(1) if _TOKEN else "")


def rpc(method, params=None, rid=1):
    body = {"jsonrpc": "2.0", "id": rid, "method": method}
    if params is not None:
        body["params"] = params
    req = urllib.request.Request(URL, data=json.dumps(body).encode("utf-8"), method="POST",
                                 headers={"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
                                          "Authorization": "Bearer " + TOKEN})
    with urllib.request.urlopen(req, timeout=600) as resp:
        return json.loads(resp.read().decode("utf-8"))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "rme_mcp.py", "version": "1"}})
    if sys.argv[1] == "--list":
        for tool in rpc("tools/list", {}, 2)["result"]["tools"]:
            print(tool["name"])
        return
    args = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    reply = rpc("tools/call", {"name": sys.argv[1], "arguments": args}, 3)
    if "error" in reply:
        print("RPC ERROR:", reply["error"])
        return
    result = reply["result"]
    if result.get("isError"):
        print("[tool error]")
    for block in result.get("content", []):
        if block.get("type") == "text":
            print(block["text"])
        elif block.get("type") == "image":
            name = os.path.join(HERE, "shot_%d.png" % int(time.time() * 1000))
            with open(name, "wb") as f:
                f.write(base64.b64decode(block["data"]))
            print("[image saved]", name)


if __name__ == "__main__":
    main()
