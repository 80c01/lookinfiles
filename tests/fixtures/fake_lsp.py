#!/usr/bin/env python3
"""Minimal language server for tests/test_lsp_client.c: answers initialize,
definition (one location at the position asked + 1 line), hover (the text of
the last didOpen / didChange of the document), asks the client one request,
and exits on "exit". Usage: fake_lsp.py [--crash-on-definition]"""
import json
import sys

docs = {}
crash = "--crash-on-definition" in sys.argv


def read():
    length = None
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        line = line.strip()
        if not line:
            break
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":")[1])
    return json.loads(sys.stdin.buffer.read(length))


def write(msg):
    body = json.dumps(msg).encode()
    sys.stdout.buffer.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
    sys.stdout.buffer.flush()


while True:
    msg = read()
    if msg is None:
        break
    method = msg.get("method")
    mid = msg.get("id")
    if method == "initialize":
        write({"jsonrpc": "2.0", "id": mid, "result": {
            "capabilities": {"definitionProvider": True, "hoverProvider": True,
                             "signatureHelpProvider": {"triggerCharacters": ["("]}},
            "serverInfo": {"name": "fake-lsp"}}})
    elif method == "initialized":
        write({"jsonrpc": "2.0", "id": "srv1", "method": "window/workDoneProgress/create",
               "params": {"token": "t"}})
    elif method == "textDocument/didOpen":
        docs[msg["params"]["textDocument"]["uri"]] = msg["params"]["textDocument"]["text"]
    elif method == "textDocument/didChange":
        docs[msg["params"]["textDocument"]["uri"]] = msg["params"]["contentChanges"][-1]["text"]
    elif method == "textDocument/definition":
        if crash:
            sys.exit(3)
        p = msg["params"]
        pos = p["position"]
        write({"jsonrpc": "2.0", "id": mid, "result": [{
            "uri": p["textDocument"]["uri"],
            "range": {"start": {"line": pos["line"] + 1, "character": pos["character"]},
                      "end": {"line": pos["line"] + 1, "character": pos["character"]}}}]})
    elif method == "textDocument/hover":
        uri = msg["params"]["textDocument"]["uri"]
        write({"jsonrpc": "2.0", "id": mid,
               "result": {"contents": {"kind": "plaintext", "value": docs.get(uri, "")}}})
    elif method == "shutdown":
        write({"jsonrpc": "2.0", "id": mid, "result": None})
    elif method == "exit":
        break
    elif mid is not None and "method" not in msg:
        pass  # answer to our server request
    elif mid is not None:
        write({"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": "unknown"}})
