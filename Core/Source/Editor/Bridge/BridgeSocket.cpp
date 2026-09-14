// Built without the precompiled header: winsock2.h has to be included before windows.h, which
// the precompiled header (and so BridgeSocket.h) pulls in.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include "Editor/Bridge/BridgeSocket.h"
#include "Editor/Bridge/BridgeTypes.h"
#include "Utils/Log.h"

namespace YAEngine
{
  namespace BridgeSocket
  {
    namespace
    {
      // Replies are sent from the main thread, so this bounds the hitch a client that stopped
      // reading can cause before its connection is dropped.
      constexpr DWORD SEND_TIMEOUT_MS = 5000;

      std::string DescribeError(const char* operation, int error)
      {
        return std::string(operation) + " failed (WSA error " + std::to_string(error) + ")";
      }

      // Port 0 lets the OS pick. INVALID_SOCKET with outError set on failure.
      SOCKET ListenOn(uint16_t port, std::string& outError)
      {
        SOCKET listener = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0,
          WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
        if (listener == INVALID_SOCKET)
        {
          outError = DescribeError("WSASocket", WSAGetLastError());
          return INVALID_SOCKET;
        }

        // Keeps another process from binding the same port and taking connections meant for us,
        // which a fixed port makes easy to aim at
        int exclusive = 1;
        if (setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
          reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR)
        {
          outError = DescribeError("setsockopt(SO_EXCLUSIVEADDRUSE)", WSAGetLastError());
          closesocket(listener);
          return INVALID_SOCKET;
        }

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
          || listen(listener, SOMAXCONN) == SOCKET_ERROR)
        {
          outError = DescribeError("bind/listen", WSAGetLastError());
          closesocket(listener);
          return INVALID_SOCKET;
        }

        return listener;
      }
    }

    struct ReadState
    {
      WSAOVERLAPPED overlapped {};
      HANDLE wake = nullptr;
      // An aborted receive owns the overlapped block until its completion is signalled
      bool pending = false;
    };

    bool Startup(std::string& outError)
    {
      WSADATA data {};
      int result = WSAStartup(MAKEWORD(2, 2), &data);
      if (result != 0)
      {
        outError = DescribeError("WSAStartup", result);
        return false;
      }
      return true;
    }

    void Cleanup()
    {
      WSACleanup();
    }

    Handle Listen(uint16_t& outPort, std::string& outError)
    {
      std::string preferredError;
      SOCKET listener = ListenOn(BRIDGE_PREFERRED_PORT, preferredError);
      const bool preferred = listener != INVALID_SOCKET;
      if (!preferred)
      {
        std::string fallbackError;
        listener = ListenOn(0, fallbackError);
        if (listener == INVALID_SOCKET)
        {
          outError = "port " + std::to_string(BRIDGE_PREFERRED_PORT) + ": " + preferredError
            + "; OS-chosen port: " + fallbackError;
          return INVALID_HANDLE;
        }
      }

      sockaddr_in address {};
      int length = sizeof(address);
      if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR)
      {
        outError = DescribeError("getsockname", WSAGetLastError());
        closesocket(listener);
        return INVALID_HANDLE;
      }

      outPort = ntohs(address.sin_port);
      if (preferred)
      {
        YA_LOG_INFO("Bridge", "Agent bridge bound its preferred port %u", static_cast<unsigned>(outPort));
      }
      else
      {
        YA_LOG_INFO("Bridge", "Agent bridge preferred port %u unavailable (%s), fell back to port %u chosen by the OS",
          static_cast<unsigned>(BRIDGE_PREFERRED_PORT), preferredError.c_str(), static_cast<unsigned>(outPort));
      }
      return static_cast<Handle>(listener);
    }

    Handle Accept(Handle listener, int& outError)
    {
      SOCKET client = accept(static_cast<SOCKET>(listener), nullptr, nullptr);
      if (client == INVALID_SOCKET)
      {
        outError = WSAGetLastError();
        return INVALID_HANDLE;
      }

      // Not inheritable from the moment it exists: an accepted socket takes WSA_FLAG_NO_HANDLE_INHERIT
      // over from the listener. ShaderCompiler starts children with handle inheritance on, and an
      // inherited client socket would keep the connection alive after we close it.
      int noDelay = 1;
      setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
      DWORD timeout = SEND_TIMEOUT_MS;
      setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

      return static_cast<Handle>(client);
    }

    bool IsTransientAcceptError(int error)
    {
      return error == WSAECONNRESET;
    }

    ReadState* CreateReadState()
    {
      auto state = std::make_unique<ReadState>();
      state->overlapped.hEvent = WSACreateEvent();
      state->wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (state->overlapped.hEvent == nullptr || state->wake == nullptr)
      {
        if (state->overlapped.hEvent != nullptr)
          WSACloseEvent(state->overlapped.hEvent);
        if (state->wake != nullptr)
          CloseHandle(state->wake);
        return nullptr;
      }

      return state.release();
    }

    void WakeReader(ReadState* state)
    {
      SetEvent(state->wake);
    }

    void DestroyReadState(ReadState* state)
    {
      if (state == nullptr)
        return;

      if (state->pending)
        WaitForSingleObject(state->overlapped.hEvent, INFINITE);

      WSACloseEvent(state->overlapped.hEvent);
      CloseHandle(state->wake);
      delete state;
    }

    int Receive(Handle socket, ReadState* state, char* buffer, int size, uint32_t timeoutMs)
    {
      if (WaitForSingleObject(state->wake, 0) == WAIT_OBJECT_0)
        return 0;

      SOCKET s = static_cast<SOCKET>(socket);
      HANDLE completion = state->overlapped.hEvent;
      state->overlapped = WSAOVERLAPPED {};
      state->overlapped.hEvent = completion;
      WSAResetEvent(completion);

      WSABUF chunk { static_cast<ULONG>(size), buffer };
      DWORD flags = 0;
      if (WSARecv(s, &chunk, 1, nullptr, &flags, &state->overlapped, nullptr) == SOCKET_ERROR
        && WSAGetLastError() != WSA_IO_PENDING)
        return -1;

      state->pending = true;
      HANDLE events[2] = { completion, state->wake };
      DWORD wait = WaitForMultipleObjects(2, events, FALSE, timeoutMs == WAIT_FOREVER ? INFINITE : DWORD(timeoutMs));
      if (wait != WAIT_OBJECT_0)
      {
        // If the cancel itself fails the receive stays pending; the caller's Close aborts it and
        // DestroyReadState waits for that.
        if (CancelIoEx(reinterpret_cast<HANDLE>(s), &state->overlapped) || GetLastError() == ERROR_NOT_FOUND)
        {
          WaitForSingleObject(completion, INFINITE);
          state->pending = false;
        }
        return wait == WAIT_TIMEOUT ? RECEIVE_TIMED_OUT : 0;
      }

      state->pending = false;
      DWORD transferred = 0;
      if (!WSAGetOverlappedResult(s, &state->overlapped, &transferred, FALSE, &flags))
        return -1;

      return static_cast<int>(transferred);
    }

    bool SendAll(Handle socket, const char* data, size_t size)
    {
      SOCKET s = static_cast<SOCKET>(socket);
      while (size > 0)
      {
        int chunk = static_cast<int>(std::min<size_t>(size, size_t(1) << 30));
        int sent = send(s, data, chunk, 0);
        if (sent == SOCKET_ERROR)
          return false;

        data += sent;
        size -= static_cast<size_t>(sent);
      }
      return true;
    }

    void Close(Handle socket)
    {
      SOCKET s = static_cast<SOCKET>(socket);
      // Send side only: shutting down receive resets a connection the peer still writes to, and
      // the reset can discard a reply the peer has not read yet
      shutdown(s, SD_SEND);
      closesocket(s);
    }
  }
}
