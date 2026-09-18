#pragma once

#include "Editor/Bridge/BridgeJson.h"

namespace YAEngine
{
  class BridgeServer;
  class BridgeConnection;
  class MainThreadDispatcher;

  // Where and to whom one request is answered, captured when it reaches the main thread.
  struct BridgeReplyTarget
  {
    std::weak_ptr<BridgeServer> server;
    std::weak_ptr<BridgeConnection> connection;
    MainThreadDispatcher* dispatcher = nullptr;
    std::thread::id mainThread;
    Json id;
    std::string method;
    // Shown in the activity list when the connection is already gone
    std::string client;
    std::chrono::steady_clock::time_point received;
  };

  // Completes one request exactly once, immediately or in a later frame. Copies share a single
  // state, so a copy kept by a deferred job is the same reply as the original. A completion from
  // another thread is posted to the main thread. Completing for a client that disconnected is a
  // no-op, and a reply released without being completed answers "internal".
  class BridgeReply
  {
  public:
    // Receives the outcome of a local reply; an empty code means success.
    using Sink = std::function<void(std::string_view code, std::string_view message, Json result)>;

    BridgeReply() = default;
    explicit BridgeReply(BridgeReplyTarget target);

    // Hands the outcome to sink on the completing thread instead of answering a client: batch.run
    // runs each step's handler through one of these.
    static BridgeReply Local(Sink sink);

    // The result has to be a JSON object.
    void Ok(Json result = Json::object()) const;
    void Fail(std::string_view code, std::string_view message) const;
    bool IsCompleted() const;

  private:
    struct State;

    bool BeginCompletion() const;
    // To the sink of a local reply, else to the client
    void Complete(std::string_view code, std::string_view message, Json result) const;

    std::shared_ptr<State> m_State;
  };

  // Runs on the main thread.
  using BridgeHandler = std::function<void(const Json& params, const BridgeReply& reply)>;

  class BridgeMethodRegistry
  {
  public:
    // Registering a name again replaces its handler. "hello" belongs to the handshake and is
    // refused.
    void Register(const std::string& name, BridgeHandler handler);
    const BridgeHandler* Find(const std::string& name) const;

  private:
    std::unordered_map<std::string, BridgeHandler> m_Handlers;
  };
}
