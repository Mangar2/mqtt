#!/usr/bin/env python3
"""YAHA MCP server: stdio JSON-RPC 2.0 transport implementing the Model Context Protocol.

Dependency free (Python standard library only). Messages are newline delimited
JSON objects on stdin/stdout; diagnostics go to stderr.

Run:  python3 tools/yaha_mcp/yaha_mcp_server.py
"""

from __future__ import annotations

import json
import os
import sys
import traceback
from typing import Any

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from yaha_client import YahaError  # noqa: E402
from yaha_tools import HANDLERS, tool_definitions  # noqa: E402

SERVER_NAME = "yaha"
SERVER_VERSION = "1.0.0"
SUPPORTED_PROTOCOL_VERSIONS = ("2025-06-18", "2025-03-26", "2024-11-05")
INSTRUCTIONS = (
    "Access to the YAHA home automation (message store history, current values, publish, configuration). "
    "Start with yaha_overview, discover topic names with yaha_find_topics, then use yaha_history or "
    "yaha_history_summary. Reason chains explain why a value changed (rule names map to /automation/rules "
    "paths). Times are local (Europe/Berlin). yaha_publish changes the real house: only after explicit user "
    "approval. Full documentation: tools/yaha_mcp/README.md in the mqtt repo."
)

JSONRPC_PARSE_ERROR = -32700
JSONRPC_INVALID_REQUEST = -32600
JSONRPC_METHOD_NOT_FOUND = -32601
JSONRPC_INVALID_PARAMS = -32602


def log(message: str) -> None:
    """Writes a diagnostic line to stderr (stdout is reserved for protocol messages)."""
    sys.stderr.write(f"[yaha-mcp] {message}\n")
    sys.stderr.flush()


def send(message: dict[str, Any]) -> None:
    """Writes one JSON-RPC message to stdout."""
    sys.stdout.write(json.dumps(message, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def error_response(request_id: Any, code: int, message: str) -> dict[str, Any]:
    """Builds a JSON-RPC error response."""
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def call_tool(params: dict[str, Any]) -> dict[str, Any]:
    """Executes one tool call and wraps the result as MCP CallToolResult."""
    name = params.get("name")
    arguments = params.get("arguments") or {}
    handler = HANDLERS.get(str(name))
    if handler is None:
        return {"content": [{"type": "text", "text": f"unknown tool {name}"}], "isError": True}
    try:
        result = handler(arguments)
    except (YahaError, ValueError) as error:
        return {"content": [{"type": "text", "text": f"error: {error}"}], "isError": True}
    except Exception as error:  # noqa: BLE001 - report unexpected failures to the client instead of dying
        log(traceback.format_exc())
        return {"content": [{"type": "text", "text": f"internal error: {type(error).__name__}: {error}"}], "isError": True}
    return {"content": [{"type": "text", "text": json.dumps(result, ensure_ascii=False, indent=1)}], "isError": False}


def handle(message: dict[str, Any]) -> dict[str, Any] | None:
    """Dispatches one JSON-RPC message. Returns the response or None for notifications."""
    method = message.get("method")
    request_id = message.get("id")
    params = message.get("params") or {}
    is_notification = "id" not in message
    if method == "initialize":
        requested = params.get("protocolVersion")
        version = requested if requested in SUPPORTED_PROTOCOL_VERSIONS else SUPPORTED_PROTOCOL_VERSIONS[0]
        result: dict[str, Any] = {
            "protocolVersion": version,
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
            "instructions": INSTRUCTIONS,
        }
    elif method == "ping":
        result = {}
    elif method == "tools/list":
        result = {"tools": tool_definitions()}
    elif method == "tools/call":
        if not isinstance(params, dict) or "name" not in params:
            return None if is_notification else error_response(request_id, JSONRPC_INVALID_PARAMS, "missing tool name")
        result = call_tool(params)
    elif isinstance(method, str) and method.startswith("notifications/"):
        return None
    else:
        return None if is_notification else error_response(request_id, JSONRPC_METHOD_NOT_FOUND, f"unknown method {method}")
    return None if is_notification else {"jsonrpc": "2.0", "id": request_id, "result": result}


def main() -> None:
    """Reads newline delimited JSON-RPC messages from stdin until EOF."""
    log(f"started {SERVER_NAME} {SERVER_VERSION}")
    for line in sys.stdin:
        text = line.strip()
        if not text:
            continue
        try:
            message = json.loads(text)
        except json.JSONDecodeError:
            send(error_response(None, JSONRPC_PARSE_ERROR, "parse error"))
            continue
        if not isinstance(message, dict):
            send(error_response(None, JSONRPC_INVALID_REQUEST, "batch requests are not supported"))
            continue
        response = handle(message)
        if response is not None:
            send(response)


if __name__ == "__main__":
    main()
