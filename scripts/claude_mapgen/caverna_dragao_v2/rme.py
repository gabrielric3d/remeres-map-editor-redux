"""Minimal JSON-RPC client for the RME MCP server (port 7777)."""
import json, re, base64, os, urllib.request, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = os.path.join(HERE, "..", "..", "..", "config.toml")  # repo root, next to rme.exe

def _token():
    txt = open(CFG, encoding="utf-8").read()
    m = re.search(r"claude_mcp_token\s*=\s*'([^']+)'", txt) or re.search(r'claude_mcp_token\s*=\s*"([^"]+)"', txt)
    return m.group(1)

def _port():
    txt = open(CFG, encoding="utf-8").read()
    m = re.search(r"claude_mcp_port\s*=\s*(\d+)", txt)
    return int(m.group(1)) if m else 7777

URL = f"http://127.0.0.1:{_port()}/mcp"
TOKEN = _token()
_session = None
_id = 0

def _post(payload, timeout=600):
    global _session
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(URL, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("Accept", "application/json, text/event-stream")
    req.add_header("Authorization", "Bearer " + TOKEN)
    if _session:
        req.add_header("Mcp-Session-Id", _session)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        sid = r.headers.get("Mcp-Session-Id")
        if sid:
            _session = sid
        body = r.read().decode("utf-8")
        ctype = r.headers.get("Content-Type", "")
    if not body.strip():
        return None
    if "text/event-stream" in ctype:
        out = None
        for line in body.splitlines():
            if line.startswith("data:"):
                try:
                    out = json.loads(line[5:].strip())
                except Exception:
                    pass
        return out
    return json.loads(body)

def _init():
    global _id
    if _session is not None:
        return
    _id += 1
    _post({"jsonrpc": "2.0", "id": _id, "method": "initialize",
           "params": {"protocolVersion": "2025-03-26", "capabilities": {},
                      "clientInfo": {"name": "cave-py", "version": "1"}}})
    try:
        _post({"jsonrpc": "2.0", "method": "notifications/initialized"})
    except Exception:
        pass

def call(tool, args=None, img_name=None, timeout=600):
    """Call a tool. Returns (text, [image paths])."""
    global _id
    _init()
    _id += 1
    resp = _post({"jsonrpc": "2.0", "id": _id, "method": "tools/call",
                  "params": {"name": tool, "arguments": args or {}}}, timeout=timeout)
    if resp is None:
        return "", []
    if "error" in resp:
        raise RuntimeError(json.dumps(resp["error"]))
    res = resp.get("result", {})
    texts, imgs = [], []
    for i, c in enumerate(res.get("content", [])):
        if c.get("type") == "text":
            texts.append(c["text"])
        elif c.get("type") == "image":
            name = img_name or f"shot_{int(time.time()*1000)}"
            p = os.path.join(HERE, "shots", f"{name}_{i}.png" if i else f"{name}.png")
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "wb").write(base64.b64decode(c["data"]))
            imgs.append(p)
    if res.get("isError"):
        raise RuntimeError("tool error: " + "\n".join(texts))
    return "\n".join(texts), imgs

if __name__ == "__main__":
    tool = sys.argv[1]
    args = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    t, im = call(tool, args, img_name=(sys.argv[3] if len(sys.argv) > 3 else None))
    print(t)
    for p in im:
        print("IMAGE:", p)
