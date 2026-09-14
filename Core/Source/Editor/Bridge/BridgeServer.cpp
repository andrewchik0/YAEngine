#include "Editor/Bridge/BridgeServer.h"

#include "Editor/Bridge/BridgeDiscovery.h"
#include "Utils/Log.h"
#include "Utils/MainThreadDispatcher.h"

namespace YAEngine
{
  namespace
  {
    constexpr size_t RECEIVE_CHUNK_BYTES = 64 * 1024;
    // Every connection holds a reader thread, so a local process opening connections in a loop is
    // refused past this many instead of exhausting threads.
    constexpr size_t MAX_CONNECTIONS = 16;
    // A connection that has not sent a valid hello by then is closed. Only this wait has a timeout;
    // an authenticated reader blocks without one.
    constexpr std::chrono::seconds HELLO_DEADLINE { 10 };

    // Looks at every character, so the time taken says nothing about how much of a guess matched
    bool TokensMatch(std::string_view candidate, std::string_view expected)
    {
      if (expected.empty() || candidate.size() != expected.size())
        return false;

      uint8_t difference = 0;
      for (size_t i = 0; i < expected.size(); i++)
        difference |= static_cast<uint8_t>(candidate[i] ^ expected[i]);
      return difference == 0;
    }

    std::string BuildReplyLine(const Json& id, std::string_view code, std::string_view message, const Json& result)
    {
      Json reply = Json::object();
      reply["id"] = id;
      if (code.empty())
      {
        reply["result"] = result;
      }
      else
      {
        Json error = Json::object();
        error["code"] = std::string(code);
        error["message"] = std::string(message);
        reply["error"] = std::move(error);
      }
      return SerializeBridgeMessage(reply);
    }

    double MillisecondsSince(std::chrono::steady_clock::time_point start)
    {
      return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    std::string DefaultClientName(uint64_t connectionId)
    {
      return "client #" + std::to_string(connectionId);
    }

    BridgeActivityRecord MakeActivityRecord(std::string client, const std::string& method, std::string result,
      double durationMs)
    {
      return BridgeActivityRecord {
        .time = FormatBridgeLocalTime(std::chrono::system_clock::now()),
        .client = std::move(client),
        .method = method.empty() ? std::string("(none)") : method,
        .result = std::move(result),
        .durationMs = durationMs
      };
    }
  }

  BridgeConnection::BridgeConnection(uint64_t id, BridgeSocket::Handle socket, BridgeSocket::ReadState* readState)
    : m_Id(id),
      m_ConnectedAt(std::chrono::system_clock::now()),
      m_Socket(socket),
      m_ReadState(readState),
      m_DisplayName(DefaultClientName(id))
  {
  }

  BridgeConnection::~BridgeConnection()
  {
    // Only finds a live socket when the reader thread never started
    CloseFromReader();
  }

  bool BridgeConnection::Send(std::string_view data)
  {
    std::lock_guard lock(m_SocketMutex);
    if (!b_Open.load() || m_Socket == BridgeSocket::INVALID_HANDLE)
      return false;

    if (BridgeSocket::SendAll(m_Socket, data.data(), data.size()))
      return true;

    YA_LOG_WARN("Bridge", "Sending to agent connection #%llu failed; closing it",
      static_cast<unsigned long long>(m_Id));
    b_Open = false;
    if (m_ReadState != nullptr)
      BridgeSocket::WakeReader(m_ReadState);
    return false;
  }

  void BridgeConnection::RequestClose()
  {
    std::lock_guard lock(m_SocketMutex);
    b_Open = false;
    if (m_ReadState != nullptr)
      BridgeSocket::WakeReader(m_ReadState);
  }

  int BridgeConnection::Receive(char* buffer, int size, uint32_t timeoutMs)
  {
    return BridgeSocket::Receive(m_Socket, m_ReadState, buffer, size, timeoutMs);
  }

  void BridgeConnection::CloseFromReader()
  {
    BridgeSocket::ReadState* readState = nullptr;
    {
      std::lock_guard lock(m_SocketMutex);
      b_Open = false;
      if (m_Socket != BridgeSocket::INVALID_HANDLE)
      {
        BridgeSocket::Close(m_Socket);
        m_Socket = BridgeSocket::INVALID_HANDLE;
      }
      readState = std::exchange(m_ReadState, nullptr);
    }

    BridgeSocket::DestroyReadState(readState);
  }

  void BridgeConnection::SetClientName(const std::string& name)
  {
    m_DisplayName = name.empty() ? DefaultClientName(m_Id) : name;
  }

  BridgeServer::BridgeServer(BridgeServerConfig config)
    : m_Config(std::move(config))
  {
  }

  BridgeServer::~BridgeServer()
  {
    Stop();
  }

  bool BridgeServer::Start(std::string& outError)
  {
    m_MainThread = std::this_thread::get_id();

    if (!BridgeSocket::Startup(outError))
      return false;
    b_WinsockStarted = true;

    m_Listener = BridgeSocket::Listen(m_Port, outError);
    if (m_Listener == BridgeSocket::INVALID_HANDLE)
    {
      Stop();
      return false;
    }

    try
    {
      m_AcceptThread = std::thread(&BridgeServer::AcceptLoop, this, m_Listener);
    }
    catch (const std::system_error& e)
    {
      outError = std::string("cannot start the accept thread: ") + e.what();
      Stop();
      return false;
    }

    return true;
  }

  void BridgeServer::Stop()
  {
    b_Stopping = true;

    // Closing the listener is what wakes the accept thread
    if (m_Listener != BridgeSocket::INVALID_HANDLE)
    {
      BridgeSocket::Close(m_Listener);
      m_Listener = BridgeSocket::INVALID_HANDLE;
    }
    if (m_AcceptThread.joinable())
      m_AcceptThread.join();

    std::unordered_map<uint64_t, ClientSlot> slots;
    {
      std::lock_guard lock(m_SlotsMutex);
      slots.swap(m_Slots);
    }
    for (auto& [id, slot] : slots)
      slot.connection->RequestClose();
    for (auto& [id, slot] : slots)
    {
      if (slot.reader.joinable())
        slot.reader.join();
    }
    m_Clients.clear();

    if (b_WinsockStarted)
    {
      BridgeSocket::Cleanup();
      b_WinsockStarted = false;
    }
  }

  void BridgeServer::DisconnectAll()
  {
    std::lock_guard lock(m_SlotsMutex);
    for (auto& [id, slot] : m_Slots)
      slot.connection->RequestClose();
  }

  void BridgeServer::AcceptLoop(BridgeSocket::Handle listener)
  {
    bool limitReported = false;

    while (!b_Stopping.load())
    {
      int error = 0;
      BridgeSocket::Handle socket = BridgeSocket::Accept(listener, error);
      if (socket == BridgeSocket::INVALID_HANDLE)
      {
        if (b_Stopping.load())
          break;
        if (BridgeSocket::IsTransientAcceptError(error))
          continue;

        YA_LOG_ERROR("Bridge", "accept failed (WSA error %d); no further agent connections are accepted", error);
        m_AcceptError = error;
        break;
      }

      // Only this thread adds slots, so the count cannot grow past the limit before the insert below
      bool atLimit = false;
      {
        std::lock_guard lock(m_SlotsMutex);
        atLimit = m_Slots.size() >= MAX_CONNECTIONS;
      }
      if (atLimit)
      {
        // Once per run of refusals, or a process looping on connect floods the log
        if (!limitReported)
          YA_LOG_WARN("Bridge", "Refusing agent connections while %zu are open", MAX_CONNECTIONS);
        limitReported = true;
        BridgeSocket::Close(socket);
        continue;
      }
      limitReported = false;

      BridgeSocket::ReadState* readState = BridgeSocket::CreateReadState();
      if (readState == nullptr)
      {
        YA_LOG_ERROR("Bridge", "Cannot create the receive state for a new agent connection");
        BridgeSocket::Close(socket);
        continue;
      }

      auto connection = std::make_shared<BridgeConnection>(m_NextConnectionId++, socket, readState);

      std::lock_guard lock(m_SlotsMutex);
      if (b_Stopping.load())
        break;

      ClientSlot& slot = m_Slots[connection->GetId()];
      slot.connection = connection;
      try
      {
        slot.reader = std::thread(&BridgeServer::ReadLoop, this, connection);
      }
      catch (const std::system_error& e)
      {
        YA_LOG_ERROR("Bridge", "Cannot start a reader thread for an agent connection: %s", e.what());
        m_Slots.erase(connection->GetId());
      }
    }
  }

  void BridgeServer::ReadLoop(std::shared_ptr<BridgeConnection> connection)
  {
    std::vector<char> chunk(RECEIVE_CHUNK_BYTES);
    std::string pending;
    bool authorized = false;
    bool keepOpen = true;
    const auto helloDeadline = std::chrono::steady_clock::now() + HELLO_DEADLINE;

    while (keepOpen)
    {
      uint32_t timeoutMs = BridgeSocket::WAIT_FOREVER;
      if (!authorized)
      {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(helloDeadline - std::chrono::steady_clock::now());
        timeoutMs = static_cast<uint32_t>(std::max<int64_t>(left.count(), 0));
      }

      int received = connection->Receive(chunk.data(), static_cast<int>(chunk.size()), timeoutMs);
      if (received == BridgeSocket::RECEIVE_TIMED_OUT)
      {
        YA_LOG_WARN("Bridge", "Agent connection #%llu sent no valid hello within %lld s; closing it",
          static_cast<unsigned long long>(connection->GetId()), static_cast<long long>(HELLO_DEADLINE.count()));
        break;
      }
      if (received <= 0)
        break;

      size_t scanFrom = pending.size();
      pending.append(chunk.data(), static_cast<size_t>(received));

      size_t lineStart = 0;
      size_t newline = 0;
      while (keepOpen && (newline = pending.find('\n', scanFrom)) != std::string::npos)
      {
        std::string_view line(pending.data() + lineStart, newline - lineStart);
        if (line.size() > BRIDGE_MAX_MESSAGE_BYTES)
        {
          YA_LOG_WARN("Bridge", "Agent connection #%llu sent a message over 4 MB; closing it",
            static_cast<unsigned long long>(connection->GetId()));
          keepOpen = false;
          break;
        }

        if (!line.empty() && line.back() == '\r')
          line.remove_suffix(1);
        if (!line.empty())
          keepOpen = ProcessLine(connection, line, authorized);

        lineStart = newline + 1;
        scanFrom = lineStart;
      }
      pending.erase(0, lineStart);

      if (keepOpen && pending.size() > BRIDGE_MAX_MESSAGE_BYTES)
      {
        YA_LOG_WARN("Bridge", "Agent connection #%llu sent a message over 4 MB; closing it",
          static_cast<unsigned long long>(connection->GetId()));
        keepOpen = false;
      }
    }

    connection->CloseFromReader();

    std::weak_ptr<BridgeServer> weakSelf = weak_from_this();
    uint64_t connectionId = connection->GetId();
    m_Config.dispatcher->Dispatch([weakSelf, connectionId]() {
      if (auto self = weakSelf.lock())
        self->ReapConnection(connectionId);
    });
    // A minimized editor sits in glfwWaitEvents and would reap only after some window event
    glfwPostEmptyEvent();
  }

  bool BridgeServer::ProcessLine(const std::shared_ptr<BridgeConnection>& connection, std::string_view line,
    bool& authorized)
  {
    auto received = std::chrono::steady_clock::now();
    Json message = Json::parse(line.begin(), line.end(), nullptr, false);

    Json id;
    std::string method;
    Json params = Json::object();
    std::string problem;

    if (message.is_discarded() || !message.is_object())
    {
      problem = "The message is not a JSON object";
    }
    else
    {
      auto idIt = message.find("id");
      if (idIt != message.end() && (idIt->is_number_integer() || idIt->is_string()))
        id = *idIt;
      else
        problem = "'id' must be an integer or a string";

      auto methodIt = message.find("method");
      if (methodIt != message.end() && methodIt->is_string())
        method = methodIt->get<std::string>();
      else if (problem.empty())
        problem = "'method' must be a string";

      auto paramsIt = message.find("params");
      if (paramsIt != message.end() && !paramsIt->is_null())
      {
        if (paramsIt->is_object())
          params = std::move(*paramsIt);
        else if (problem.empty())
          problem = "'params' must be an object";
      }
    }

    // The token is checked here rather than on the main thread, so no request of an
    // unauthenticated client ever reaches a handler; its rejection is only recorded, in batches.
    if (problem.empty() && method == "hello")
    {
      auto tokenIt = params.find("token");
      std::string_view token;
      if (tokenIt != params.end() && tokenIt->is_string())
        token = tokenIt->get_ref<const std::string&>();

      if (!TokensMatch(token, m_Config.token))
      {
        RejectUnauthorized(connection, id, method, "Invalid token", received);
        return false;
      }
      authorized = true;
    }
    else if (!authorized)
    {
      RejectUnauthorized(connection, id, method, "The first request must be hello with a valid token", received);
      return false;
    }

    std::weak_ptr<BridgeServer> weakSelf = weak_from_this();
    std::weak_ptr<BridgeConnection> weakConnection = connection;
    m_Config.dispatcher->Dispatch([weakSelf, weakConnection, id = std::move(id), method = std::move(method),
      params = std::move(params), received, problem = std::move(problem)]() mutable {
      auto self = weakSelf.lock();
      auto liveConnection = weakConnection.lock();
      // A client that left before its request reached the main thread gets nothing started
      if (!self || !liveConnection || !liveConnection->IsOpen())
        return;

      self->HandleRequest(liveConnection, std::move(id), std::move(method), std::move(params), received, problem);
    });
    // A minimized editor sits in glfwWaitEvents and would not drain the dispatcher until some
    // unrelated window event arrived
    glfwPostEmptyEvent();

    return true;
  }

  void BridgeServer::RejectUnauthorized(const std::shared_ptr<BridgeConnection>& connection, const Json& id,
    const std::string& method, const char* reason, std::chrono::steady_clock::time_point received)
  {
    connection->Send(BuildReplyLine(id, BridgeErrorCode::UNAUTHORIZED, reason, Json()));
    YA_LOG_WARN("Bridge", "Rejected agent connection #%llu: %s",
      static_cast<unsigned long long>(connection->GetId()), reason);

    QueueRejection(MakeActivityRecord(DefaultClientName(connection->GetId()), method, BridgeErrorCode::UNAUTHORIZED,
      MillisecondsSince(received)));
  }

  void BridgeServer::QueueRejection(BridgeActivityRecord record)
  {
    bool post = false;
    {
      std::lock_guard lock(m_RejectionsMutex);
      m_PendingRejections.push_back(std::move(record));
      while (m_PendingRejections.size() > BRIDGE_MAX_ACTIVITY_RECORDS)
        m_PendingRejections.pop_front();

      post = !b_RejectionsPosted;
      b_RejectionsPosted = true;
    }

    if (!post)
      return;

    std::weak_ptr<BridgeServer> weakSelf = weak_from_this();
    m_Config.dispatcher->Dispatch([weakSelf]() {
      if (auto self = weakSelf.lock())
        self->DrainRejections();
    });
  }

  void BridgeServer::DrainRejections()
  {
    std::deque<BridgeActivityRecord> rejections;
    {
      std::lock_guard lock(m_RejectionsMutex);
      rejections.swap(m_PendingRejections);
      b_RejectionsPosted = false;
    }

    for (BridgeActivityRecord& record : rejections)
      AppendActivity(std::move(record));
  }

  void BridgeServer::HandleRequest(const std::shared_ptr<BridgeConnection>& connection, Json id, std::string method,
    Json params, std::chrono::steady_clock::time_point received, const std::string& problem)
  {
    BridgeReplyTarget target;
    target.server = weak_from_this();
    target.connection = connection;
    target.dispatcher = m_Config.dispatcher;
    target.mainThread = m_MainThread;
    target.id = std::move(id);
    target.method = method;
    target.client = connection->GetDisplayName();
    target.received = received;
    BridgeReply reply(std::move(target));

    if (!problem.empty())
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, problem);
      return;
    }

    if (method == "hello")
    {
      HandleHello(*connection, params, reply);
      return;
    }

    const BridgeHandler* handler = m_Config.methods->Find(method);
    if (handler == nullptr)
    {
      reply.Fail(BridgeErrorCode::UNKNOWN_METHOD, "Unknown method '" + method + "'");
      return;
    }

    try
    {
      (*handler)(params, reply);
    }
    catch (const std::exception& e)
    {
      YA_LOG_ERROR("Bridge", "Method '%s' threw: %s", method.c_str(), e.what());
      if (!reply.IsCompleted())
        reply.Fail(BridgeErrorCode::INTERNAL, e.what());
    }
    catch (...)
    {
      YA_LOG_ERROR("Bridge", "Method '%s' threw a non-standard exception", method.c_str());
      if (!reply.IsCompleted())
        reply.Fail(BridgeErrorCode::INTERNAL, "The handler threw a non-standard exception");
    }
  }

  void BridgeServer::HandleHello(BridgeConnection& connection, const Json& params, const BridgeReply& reply)
  {
    std::string clientName;
    int64_t protocolVersion = BRIDGE_PROTOCOL_VERSION;
    std::string error;
    if (!ReadOptionalParam(params, "clientName", clientName, error)
      || !ReadOptionalParam(params, "protocolVersion", protocolVersion, error))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
      return;
    }

    connection.SetClientName(clientName);
    if (protocolVersion != BRIDGE_PROTOCOL_VERSION)
    {
      YA_LOG_WARN("Bridge", "Agent client '%s' speaks protocol %lld, the editor speaks %d",
        connection.GetDisplayName().c_str(), static_cast<long long>(protocolVersion), BRIDGE_PROTOCOL_VERSION);
    }

    if (!connection.HasSaidHello())
    {
      connection.MarkHelloDone();
      m_Clients.push_back(BridgeClientInfo {
        .connectionId = connection.GetId(),
        .name = connection.GetDisplayName(),
        .connectedAt = FormatBridgeLocalTime(connection.GetConnectedAt())
      });
      YA_LOG_INFO("Bridge", "Agent client '%s' connected", connection.GetDisplayName().c_str());
    }
    else
    {
      for (BridgeClientInfo& client : m_Clients)
      {
        if (client.connectionId == connection.GetId())
          client.name = connection.GetDisplayName();
      }
    }

    Json result = Json::object();
    result["protocolVersion"] = BRIDGE_PROTOCOL_VERSION;
    result["pid"] = GetBridgeProcessId();
    result["buildConfig"] = GetBridgeBuildConfig();
    result["scenePath"] = *m_Config.scenePath;
    result["engine"] = "YAEngine";
    reply.Ok(std::move(result));
  }

  void BridgeServer::ReapConnection(uint64_t connectionId)
  {
    ClientSlot slot;
    {
      std::lock_guard lock(m_SlotsMutex);
      auto it = m_Slots.find(connectionId);
      if (it == m_Slots.end())
        return;

      slot = std::move(it->second);
      m_Slots.erase(it);
    }

    if (slot.reader.joinable())
      slot.reader.join();

    if (slot.connection->HasSaidHello())
    {
      std::erase_if(m_Clients, [connectionId](const BridgeClientInfo& client) {
        return client.connectionId == connectionId;
      });
      YA_LOG_INFO("Bridge", "Agent client '%s' disconnected", slot.connection->GetDisplayName().c_str());
    }
  }

  void BridgeServer::Deliver(const BridgeReplyTarget& target, std::string_view code, std::string_view message,
    const Json& result)
  {
    std::string resultCode = code.empty() ? std::string("ok") : std::string(code);
    std::string line = BuildReplyLine(target.id, code, message, result);
    if (line.size() > BRIDGE_MAX_MESSAGE_BYTES)
    {
      YA_LOG_ERROR("Bridge", "Reply to '%s' is %zu bytes, over the 4 MB message limit",
        target.method.c_str(), line.size());
      resultCode = BridgeErrorCode::INTERNAL;
      line = BuildReplyLine(target.id, BridgeErrorCode::INTERNAL, "The reply exceeded the 4 MB message limit", Json());
    }

    std::string client = target.client;
    if (auto connection = target.connection.lock())
    {
      client = connection->GetDisplayName();
      connection->Send(line);
    }

    AppendActivity(MakeActivityRecord(std::move(client), target.method, std::move(resultCode),
      MillisecondsSince(target.received)));
  }

  void BridgeServer::AppendActivity(BridgeActivityRecord record)
  {
    std::deque<BridgeActivityRecord>& activity = *m_Config.activity;
    activity.push_back(std::move(record));

    while (activity.size() > BRIDGE_MAX_ACTIVITY_RECORDS)
      activity.pop_front();
  }
}
