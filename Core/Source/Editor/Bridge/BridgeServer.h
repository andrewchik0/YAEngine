#pragma once

#include "Editor/Bridge/BridgeMethods.h"
#include "Editor/Bridge/BridgeSocket.h"
#include "Editor/Bridge/BridgeTypes.h"

namespace YAEngine
{
  class MainThreadDispatcher;

  // One accepted client. Its reader thread is the only thread that closes the socket; everyone
  // else asks it to through RequestClose.
  class BridgeConnection
  {
  public:
    BridgeConnection(uint64_t id, BridgeSocket::Handle socket, BridgeSocket::ReadState* readState);
    ~BridgeConnection();

    BridgeConnection(const BridgeConnection&) = delete;
    BridgeConnection& operator=(const BridgeConnection&) = delete;

    uint64_t GetId() const { return m_Id; }
    std::chrono::system_clock::time_point GetConnectedAt() const { return m_ConnectedAt; }
    bool IsOpen() const { return b_Open.load(); }

    // Thread-safe. A failed send closes the connection.
    bool Send(std::string_view data);
    // Thread-safe. Wakes the reader, which then closes the socket.
    void RequestClose();

    // Reader thread only.
    int Receive(char* buffer, int size, uint32_t timeoutMs);
    void CloseFromReader();

    // Main thread only.
    void SetClientName(const std::string& name);
    const std::string& GetDisplayName() const { return m_DisplayName; }
    bool HasSaidHello() const { return b_HelloDone; }
    void MarkHelloDone() { b_HelloDone = true; }

  private:
    uint64_t m_Id = 0;
    std::chrono::system_clock::time_point m_ConnectedAt;

    std::mutex m_SocketMutex;
    BridgeSocket::Handle m_Socket = BridgeSocket::INVALID_HANDLE;
    BridgeSocket::ReadState* m_ReadState = nullptr;
    std::atomic<bool> b_Open { true };

    std::string m_DisplayName;
    bool b_HelloDone = false;
  };

  struct BridgeServerConfig
  {
    MainThreadDispatcher* dispatcher = nullptr;
    const BridgeMethodRegistry* methods = nullptr;
    // Owned by EditorBridge so the list survives stopping and restarting the listener
    std::deque<BridgeActivityRecord>* activity = nullptr;
    const std::string* scenePath = nullptr;
    std::string token;
  };

  // One listener run, from enabling agent connections to disabling them. Idle it costs nothing:
  // the accept thread and one reader per client stay blocked in the kernel until the network
  // wakes them. Parsed requests go to the main thread dispatcher, so every handler runs on the
  // main thread. Start, Stop and every public call except Deliver's thread hop are main thread.
  class BridgeServer : public std::enable_shared_from_this<BridgeServer>
  {
  public:
    explicit BridgeServer(BridgeServerConfig config);
    ~BridgeServer();

    BridgeServer(const BridgeServer&) = delete;
    BridgeServer& operator=(const BridgeServer&) = delete;

    bool Start(std::string& outError);
    // Closes the listener and every connection, then joins all threads.
    void Stop();

    uint16_t GetPort() const { return m_Port; }
    // Non-zero once the accept thread gave up on an unexpected error.
    int GetAcceptError() const { return m_AcceptError.load(); }

    const std::vector<BridgeClientInfo>& GetClients() const { return m_Clients; }
    void DisconnectAll();

    // Reached through BridgeReply, always on the main thread. An empty code means success.
    void Deliver(const BridgeReplyTarget& target, std::string_view code, std::string_view message, const Json& result);

  private:
    struct ClientSlot
    {
      std::shared_ptr<BridgeConnection> connection;
      std::thread reader;
    };

    void AcceptLoop(BridgeSocket::Handle listener);
    void ReadLoop(std::shared_ptr<BridgeConnection> connection);
    // Reader thread. Returns false when the connection has to close.
    bool ProcessLine(const std::shared_ptr<BridgeConnection>& connection, std::string_view line, bool& authorized);
    void RejectUnauthorized(const std::shared_ptr<BridgeConnection>& connection, const Json& id,
      const std::string& method, const char* reason, std::chrono::steady_clock::time_point received);
    // Reader thread. Rejections reach the main thread in batches, one dispatch per batch.
    void QueueRejection(BridgeActivityRecord record);

    // Main thread.
    void HandleRequest(const std::shared_ptr<BridgeConnection>& connection, Json id, std::string method, Json params,
      std::chrono::steady_clock::time_point received, const std::string& problem);
    void HandleHello(BridgeConnection& connection, const Json& params, const BridgeReply& reply);
    void ReapConnection(uint64_t connectionId);
    void DrainRejections();
    void AppendActivity(BridgeActivityRecord record);

    BridgeServerConfig m_Config;
    std::thread::id m_MainThread;

    BridgeSocket::Handle m_Listener = BridgeSocket::INVALID_HANDLE;
    uint16_t m_Port = 0;
    bool b_WinsockStarted = false;
    std::atomic<bool> b_Stopping { false };
    std::atomic<int> m_AcceptError { 0 };
    std::thread m_AcceptThread;
    // Accept thread only
    uint64_t m_NextConnectionId = 1;

    std::mutex m_SlotsMutex;
    std::unordered_map<uint64_t, ClientSlot> m_Slots;

    std::mutex m_RejectionsMutex;
    std::deque<BridgeActivityRecord> m_PendingRejections;
    // A drain is queued on the dispatcher and has not run yet
    bool b_RejectionsPosted = false;

    std::vector<BridgeClientInfo> m_Clients;
  };
}
