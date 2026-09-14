#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Thin Winsock layer. Everything that needs winsock2.h stays in BridgeSocket.cpp.
  namespace BridgeSocket
  {
    using Handle = uintptr_t;
    inline constexpr Handle INVALID_HANDLE = ~Handle(0);

    // Reference counted by Winsock: every successful Startup needs one Cleanup.
    bool Startup(std::string& outError);
    void Cleanup();

    // Bound to 127.0.0.1 on BRIDGE_PREFERRED_PORT, or on a port the OS picks when that bind fails;
    // exclusive, not inherited by child processes.
    Handle Listen(uint16_t& outPort, std::string& outError);
    // Blocks until a client connects. Closing the listener from another thread makes it return
    // INVALID_HANDLE.
    Handle Accept(Handle listener, int& outError);
    bool IsTransientAcceptError(int error);

    // Receive state of one connection. Windows does not wake a blocked recv on shutdown, and
    // closing a socket under a blocked reader lets the next accept reuse its handle value. So the
    // reader waits on an overlapped receive plus a wake event, and is the only thread that ever
    // closes its socket.
    struct ReadState;
    ReadState* CreateReadState();
    // Thread-safe. The blocked receive, or the next one, returns 0.
    void WakeReader(ReadState* state);
    // Call after Close: waits out a receive the close aborted, then frees the state.
    void DestroyReadState(ReadState* state);
    inline constexpr uint32_t WAIT_FOREVER = ~uint32_t(0);
    inline constexpr int RECEIVE_TIMED_OUT = -2;
    // Blocks until data arrives or timeoutMs passes. Returns the byte count, 0 once the peer
    // closed or the reader was woken, RECEIVE_TIMED_OUT, or another negative value on error.
    int Receive(Handle socket, ReadState* state, char* buffer, int size, uint32_t timeoutMs);

    // Blocking. Fails once the send timeout expires on a client that stopped reading.
    bool SendAll(Handle socket, const char* data, size_t size);
    void Close(Handle socket);
  }
}
