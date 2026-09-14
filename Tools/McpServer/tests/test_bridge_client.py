import asyncio

import pytest

from yaengine_mcp.bridge import (
    DISCONNECTED,
    MAX_MESSAGE_BYTES,
    MESSAGE_TOO_LARGE,
    PROTOCOL_ERRORS,
    PROTOCOL_MISMATCH,
    TIMEOUT,
    BridgeClient,
    BridgeError,
)


async def test_hello_carries_token_client_name_and_version(fake_bridge, client):
    assert client.hello["engine"] == "YAEngine"
    assert client.hello["pid"] == fake_bridge.pid
    first = fake_bridge.requests[0]
    assert first["method"] == "hello"
    assert first["params"] == {"token": fake_bridge.token, "clientName": "yaengine-mcp", "protocolVersion": 1}


async def test_wrong_token_is_rejected(fake_bridge):
    client = BridgeClient(fake_bridge.port, "0" * 32)
    with pytest.raises(BridgeError) as info:
        await client.connect()
    assert info.value.code == "unauthorized"
    assert not client.connected


async def test_protocol_version_mismatch_is_rejected(fake_bridge):
    fake_bridge.protocol_version = 2
    client = BridgeClient(fake_bridge.port, fake_bridge.token)
    with pytest.raises(BridgeError) as info:
        await client.connect()
    assert info.value.code == PROTOCOL_MISMATCH
    assert not client.connected


async def test_connection_refused_is_reported(fake_bridge):
    port = fake_bridge.port
    await fake_bridge.stop()
    with pytest.raises(BridgeError) as info:
        await BridgeClient(port, fake_bridge.token, connect_timeout=2.0).connect()
    assert info.value.code == DISCONNECTED


async def test_status_and_log_tail(fake_bridge, client):
    status = await client.request("engine.status")
    assert status["pid"] == fake_bridge.pid
    assert status["renderExtent"] == [1280, 720]

    tail = await client.request("log.tail", {"count": 3, "minLevel": "warning"})
    assert [line["seq"] for line in tail["lines"]] == [26, 27, 30]
    assert tail["lastSeq"] == 30


async def test_out_of_order_replies_are_matched_by_id(fake_bridge, client):
    gate = fake_bridge.gate("slow")
    slow = asyncio.create_task(client.request("test.deferred", {"value": "slow", "gate": "slow"}))
    await gate.received.wait()

    assert await client.request("test.deferred", {"value": "fast"}) == {"value": "fast"}
    assert await client.request("bridge.ping") == {}
    assert not slow.done()

    gate.opened.set()
    assert await slow == {"value": "slow"}


async def test_timeout_leaves_connection_usable(fake_bridge, client):
    gate = fake_bridge.gate("late")
    with pytest.raises(BridgeError) as info:
        await client.request("test.deferred", {"value": 1, "gate": "late"}, timeout=0.05)
    assert info.value.code == TIMEOUT

    # The late reply reaches the client before the ping reply and must be ignored.
    gate.opened.set()
    await gate.replied.wait()
    assert await client.request("bridge.ping") == {}
    assert client.connected


async def test_failed_send_is_reported_as_not_sent(client, monkeypatch):
    async def broken_drain():
        raise ConnectionResetError("reset by the test")

    monkeypatch.setattr(client._writer, "drain", broken_drain)
    with pytest.raises(BridgeError) as info:
        await client.request("bridge.ping")

    assert info.value.code == DISCONNECTED and info.value.sent is False
    assert "send failed" in info.value.message
    assert not client.connected


@pytest.mark.parametrize("code", sorted(PROTOCOL_ERRORS))
async def test_protocol_errors_are_mapped(client, code):
    with pytest.raises(BridgeError) as info:
        await client.request("test.error", {"code": code, "message": "details"})
    error = info.value
    assert (error.code, error.message, error.method) == (code, "details", "test.error")
    assert "details" in str(error)
    assert PROTOCOL_ERRORS[code] in str(error)


async def test_unknown_method(client):
    with pytest.raises(BridgeError) as info:
        await client.request("no.such.method")
    assert info.value.code == "unknown_method"
    assert client.connected


async def test_events_do_not_disturb_replies(fake_bridge):
    events = []
    client = BridgeClient(fake_bridge.port, fake_bridge.token, on_event=lambda name, params: events.append((name, params)))
    try:
        assert await client.request("test.event") == {}
    finally:
        await client.close()
    assert events == [("test.unknownEvent", {"x": 1})]


async def test_dropped_connection_fails_pending_and_reconnects_on_demand(fake_bridge, client):
    pending = asyncio.create_task(client.request("test.deferred", {"value": 1, "gate": "never"}))
    await fake_bridge.gate("never").received.wait()
    await fake_bridge.drop_connections()

    with pytest.raises(BridgeError) as info:
        await pending
    assert info.value.code == DISCONNECTED and info.value.sent
    assert not client.connected

    assert await client.request("bridge.ping") == {}
    assert len(fake_bridge.hellos()) == 2


async def test_oversized_reply_closes_connection(client):
    with pytest.raises(BridgeError) as info:
        await client.request("test.oversized")
    assert info.value.code == DISCONNECTED
    assert "larger than" in info.value.message
    assert not client.connected


async def test_oversized_request_is_not_sent(fake_bridge, client):
    with pytest.raises(BridgeError) as info:
        await client.request("bridge.ping", {"blob": "x" * MAX_MESSAGE_BYTES})
    assert info.value.code == MESSAGE_TOO_LARGE
    assert client.connected
    assert [request["method"] for request in fake_bridge.requests] == ["hello"]
