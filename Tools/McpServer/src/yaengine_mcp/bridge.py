"""Async client for the YAEngine editor bridge: TCP on 127.0.0.1, one JSON object per line."""

import asyncio
import contextlib
import itertools
import json
import logging
from typing import Any, Callable, Optional

log = logging.getLogger(__name__)

PROTOCOL_VERSION = 1
CLIENT_NAME = "yaengine-mcp"
HOST = "127.0.0.1"
MAX_MESSAGE_BYTES = 4 * 1024 * 1024
DEFAULT_TIMEOUT = 30.0
CONNECT_TIMEOUT = 5.0

PROTOCOL_ERRORS = {
    "unauthorized": "the bridge rejected this client (wrong or outdated token)",
    "unknown_method": "the editor does not implement this method (it may be an older build)",
    "invalid_params": "invalid parameters",
    "not_found": "not found",
    "busy": "the editor is busy with another job; retry later",
    "failed": "the operation failed",
    "internal": "internal editor error; check the editor log",
}

# Raised by the client itself; the engine never sends these codes.
DISCONNECTED = "disconnected"
TIMEOUT = "timeout"
PROTOCOL_MISMATCH = "protocol_mismatch"
MESSAGE_TOO_LARGE = "message_too_large"

EventHandler = Callable[[str, dict], None]


class BridgeError(Exception):
    def __init__(self, code: str, message: str = "", method: Optional[str] = None, *, sent: bool = True):
        super().__init__(code, message, method)
        self.code = code
        self.message = message
        self.method = method
        # False when the request never left this client, so the editor cannot have started it.
        self.sent = sent

    def __str__(self) -> str:
        text = f"{self.method}: {self.code}" if self.method else self.code
        hint = PROTOCOL_ERRORS.get(self.code)
        if hint:
            text += f" ({hint})"
        if self.message:
            text += f": {self.message}"
        return text


class BridgeClient:
    """One bridge connection; concurrent requests are matched to replies by id.

    request() reconnects (repeating hello) when the previous connection dropped. A request that
    was already sent when the connection dropped fails instead of being resent, because the
    editor may have started it.
    """

    def __init__(
        self,
        port: int,
        token: str,
        *,
        host: str = HOST,
        client_name: str = CLIENT_NAME,
        default_timeout: float = DEFAULT_TIMEOUT,
        connect_timeout: float = CONNECT_TIMEOUT,
        on_event: Optional[EventHandler] = None,
    ):
        self.host = host
        self.port = port
        self.token = token
        self.client_name = client_name
        self.default_timeout = default_timeout
        self.connect_timeout = connect_timeout
        self.hello: Optional[dict] = None
        self._on_event = on_event
        self._ids = itertools.count(1)
        self._pending: dict[Any, tuple[str, asyncio.Future]] = {}
        self._writer: Optional[asyncio.StreamWriter] = None
        self._reader_task: Optional[asyncio.Task] = None
        self._connect_lock = asyncio.Lock()
        self._send_lock = asyncio.Lock()

    @property
    def connected(self) -> bool:
        return self._writer is not None

    async def __aenter__(self) -> "BridgeClient":
        await self.connect()
        return self

    async def __aexit__(self, *exc_info) -> None:
        await self.close()

    async def connect(self) -> dict:
        async with self._connect_lock:
            if self._writer is None:
                await self._open()
            return self.hello

    async def request(self, method: str, params: Optional[dict] = None, timeout: Optional[float] = None) -> Any:
        if self._writer is None:
            await self.connect()
        return await self._call(method, params, self.default_timeout if timeout is None else timeout)

    async def close(self) -> None:
        writer, task = self._writer, self._reader_task
        if writer is not None:
            self._teardown(writer, "connection closed by the client")
        if task is not None and task is not asyncio.current_task():
            task.cancel()
            with contextlib.suppress(asyncio.CancelledError, Exception):
                await task
        if writer is not None:
            with contextlib.suppress(Exception):
                await writer.wait_closed()

    async def _open(self) -> None:
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(self.host, self.port, limit=MAX_MESSAGE_BYTES), self.connect_timeout
            )
        except (OSError, asyncio.TimeoutError) as exc:
            detail = str(exc) or type(exc).__name__
            raise BridgeError(
                DISCONNECTED, f"cannot connect to {self.host}:{self.port}: {detail}", "hello", sent=False
            ) from exc

        self._writer = writer
        self._reader_task = asyncio.create_task(self._read_loop(reader, writer))
        try:
            hello = await self._call(
                "hello",
                {"token": self.token, "clientName": self.client_name, "protocolVersion": PROTOCOL_VERSION},
                self.default_timeout,
            )
            version = hello.get("protocolVersion") if isinstance(hello, dict) else None
            if version != PROTOCOL_VERSION:
                raise BridgeError(
                    PROTOCOL_MISMATCH,
                    f"editor speaks bridge protocol {version}, this client speaks {PROTOCOL_VERSION}",
                    "hello",
                )
        except BaseException:
            await self.close()
            raise
        self.hello = hello

    async def _call(self, method: str, params: Optional[dict], timeout: float) -> Any:
        writer = self._writer
        if writer is None:
            raise BridgeError(DISCONNECTED, "not connected", method, sent=False)

        request_id = next(self._ids)
        message: dict[str, Any] = {"id": request_id, "method": method}
        if params is not None:
            message["params"] = params
        data = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if len(data) > MAX_MESSAGE_BYTES:
            raise BridgeError(
                MESSAGE_TOO_LARGE,
                f"request is {len(data)} bytes; the bridge accepts at most {MAX_MESSAGE_BYTES}",
                method,
                sent=False,
            )

        future = asyncio.get_running_loop().create_future()
        self._pending[request_id] = (method, future)
        try:
            try:
                async with self._send_lock:
                    if self._writer is not writer:
                        raise BridgeError(
                            DISCONNECTED, "the connection closed before the request was sent", method, sent=False
                        )
                    writer.write(data + b"\n")
                    await writer.drain()
            except (OSError, RuntimeError) as exc:
                self._teardown(writer, f"send failed: {exc}")
                raise BridgeError(DISCONNECTED, f"send failed: {exc}", method, sent=False) from exc
            return await asyncio.wait_for(future, timeout)
        except asyncio.TimeoutError:
            raise BridgeError(TIMEOUT, f"no reply within {timeout:g} s", method) from None
        finally:
            self._pending.pop(request_id, None)
            if future.done() and not future.cancelled():
                # Retrieves the teardown error of a request that failed before it was sent.
                future.exception()

    async def _read_loop(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        reason = "connection closed by the editor"
        try:
            while True:
                try:
                    line = await reader.readline()
                except ValueError:
                    reason = f"editor sent a message larger than {MAX_MESSAGE_BYTES} bytes"
                    break
                except OSError as exc:
                    reason = f"connection lost: {exc}"
                    break
                if not line.endswith(b"\n"):
                    break
                self._dispatch(line)
        finally:
            self._teardown(writer, reason)

    def _dispatch(self, line: bytes) -> None:
        try:
            message = json.loads(line)
        except ValueError:
            log.warning("bridge %s:%d: ignoring a line that is not JSON (%d bytes)", self.host, self.port, len(line))
            return
        if not isinstance(message, dict):
            log.warning("bridge %s:%d: ignoring a message that is not an object", self.host, self.port)
            return

        if "id" in message:
            request_id = message["id"]
            entry = self._pending.get(request_id) if isinstance(request_id, (int, str)) else None
            if entry is None:
                log.debug("bridge %s:%d: reply for unknown or expired request %r", self.host, self.port, request_id)
                return
            method, future = entry
            if future.done():
                return
            error = message.get("error")
            if error is not None:
                error = error if isinstance(error, dict) else {}
                future.set_exception(
                    BridgeError(str(error.get("code") or "internal"), str(error.get("message") or ""), method)
                )
            else:
                result = message.get("result")
                future.set_result({} if result is None else result)
            return

        event = message.get("event")
        if isinstance(event, str) and self._on_event is not None:
            try:
                self._on_event(event, message.get("params") or {})
            except Exception:
                log.exception("bridge event handler failed for %s", event)

    def _teardown(self, writer: asyncio.StreamWriter, reason: str) -> None:
        if self._writer is writer:
            self._writer = None
            self._reader_task = None
            pending = list(self._pending.values())
            self._pending.clear()
            for method, future in pending:
                if not future.done():
                    future.set_exception(BridgeError(DISCONNECTED, reason, method))
            log.debug("bridge %s:%d: %s", self.host, self.port, reason)
        writer.close()
