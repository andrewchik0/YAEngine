#include "Editor/Bridge/BridgeMethods.h"

#include "Editor/Bridge/BridgeServer.h"
#include "Editor/Bridge/BridgeTypes.h"
#include "Utils/Log.h"
#include "Utils/MainThreadDispatcher.h"

namespace YAEngine
{
  struct BridgeReply::State
  {
    explicit State(BridgeReplyTarget replyTarget)
      : target(std::move(replyTarget))
    {
    }

    ~State();

    BridgeReplyTarget target;
    std::atomic<bool> completed { false };
  };

  namespace
  {
    // An empty code means success. Delivery touches main-thread state (the activity list and the
    // client's send path ordering with other replies), so other threads post it there.
    void DeliverReply(BridgeReplyTarget target, std::string code, std::string message, Json result)
    {
      if (std::this_thread::get_id() == target.mainThread)
      {
        if (auto server = target.server.lock())
          server->Deliver(target, code, message, result);
        return;
      }

      MainThreadDispatcher* dispatcher = target.dispatcher;
      if (dispatcher == nullptr)
        return;

      dispatcher->Dispatch([target = std::move(target), code = std::move(code),
        message = std::move(message), result = std::move(result)]() {
        if (auto server = target.server.lock())
          server->Deliver(target, code, message, result);
      });
      // Wakes a minimized editor waiting in glfwWaitEvents
      glfwPostEmptyEvent();
    }
  }

  BridgeReply::State::~State()
  {
    if (completed.load())
      return;

    try
    {
      YA_LOG_ERROR("Bridge", "Method '%s' released its reply without completing it", target.method.c_str());
      DeliverReply(std::move(target), BridgeErrorCode::INTERNAL, "The handler finished without replying", Json());
    }
    catch (...)
    {
    }
  }

  BridgeReply::BridgeReply(BridgeReplyTarget target)
    : m_State(std::make_shared<State>(std::move(target)))
  {
  }

  bool BridgeReply::BeginCompletion() const
  {
    if (!m_State)
      return false;

    if (m_State->completed.exchange(true))
    {
      YA_LOG_WARN("Bridge", "Reply to '%s' completed twice; the second completion is ignored",
        m_State->target.method.c_str());
      return false;
    }

    return true;
  }

  void BridgeReply::Ok(Json result) const
  {
    if (!BeginCompletion())
      return;

    if (!result.is_object())
    {
      YA_LOG_ERROR("Bridge", "Method '%s' produced a result that is not a JSON object",
        m_State->target.method.c_str());
      DeliverReply(m_State->target, BridgeErrorCode::INTERNAL,
        "The handler produced a result that is not a JSON object", Json());
      return;
    }

    DeliverReply(m_State->target, std::string(), std::string(), std::move(result));
  }

  void BridgeReply::Fail(std::string_view code, std::string_view message) const
  {
    if (!BeginCompletion())
      return;

    DeliverReply(m_State->target, std::string(code), std::string(message), Json());
  }

  bool BridgeReply::IsCompleted() const
  {
    return m_State && m_State->completed.load();
  }

  void BridgeMethodRegistry::Register(const std::string& name, BridgeHandler handler)
  {
    if (name == "hello")
    {
      YA_LOG_ERROR("Bridge", "'hello' is part of the handshake and cannot be registered");
      return;
    }

    if (m_Handlers.contains(name))
      YA_LOG_WARN("Bridge", "Method '%s' registered twice; the later handler replaces the earlier one", name.c_str());

    m_Handlers[name] = std::move(handler);
  }

  const BridgeHandler* BridgeMethodRegistry::Find(const std::string& name) const
  {
    auto it = m_Handlers.find(name);
    return it != m_Handlers.end() ? &it->second : nullptr;
  }
}
