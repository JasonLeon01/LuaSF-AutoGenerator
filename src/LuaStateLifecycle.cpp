#include "LuaStateLifecycle.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lua_sf::detail_internal {

enum class LuaStatePhase {
  running,
  stopping,
  stopped,
};

struct LuaStateSession;

}

namespace lua_sf {

struct LuaRegistryReferenceState {
  LuaRegistryReferenceState(
      std::weak_ptr<detail_internal::LuaStateSession> session,
      lua_State *state) noexcept
      : session(std::move(session)), state(state) {}

  ~LuaRegistryReferenceState();

  std::weak_ptr<detail_internal::LuaStateSession> session;
  lua_State *state{};
  int reference{LUA_NOREF};
};

}

namespace lua_sf::detail_internal {

class DeferredCallbackErrorQueue final {
public:
  static constexpr std::size_t capacity = 64;
  static constexpr std::size_t messageCapacity = 512;

  DeferredCallbackErrorQueue() noexcept {
    for (std::size_t index = 0; index < slots_.size(); ++index)
      slots_[index].sequence.store(index, std::memory_order_relaxed);
  }

  bool enqueue(std::string_view label, std::string_view message) noexcept {
    std::size_t position = enqueuePosition_.load(std::memory_order_relaxed);
    Slot *slot = nullptr;
    for (;;) {
      slot = &slots_[position & (capacity - 1)];
      const std::size_t sequence =
          slot->sequence.load(std::memory_order_acquire);
      const std::intptr_t difference = static_cast<std::intptr_t>(sequence) -
                                       static_cast<std::intptr_t>(position);
      if (difference == 0) {
        if (enqueuePosition_.compare_exchange_weak(
                position, position + 1, std::memory_order_relaxed))
          break;
      } else if (difference < 0) {
        overflowPending_.store(1, std::memory_order_release);
        return false;
      } else {
        position = enqueuePosition_.load(std::memory_order_relaxed);
      }
    }

    std::size_t offset = 0;
    copyPart(slot->message, offset, label);
    if (!label.empty() && !message.empty())
      copyPart(slot->message, offset, ": ");
    copyPart(slot->message, offset, message);
    slot->message[offset] = '\0';
    slot->sequence.store(position + 1, std::memory_order_release);
    return true;
  }

  bool dequeue(char *buffer, std::size_t bufferCapacity) noexcept {
    std::size_t position = dequeuePosition_.load(std::memory_order_relaxed);
    Slot *slot = nullptr;
    for (;;) {
      slot = &slots_[position & (capacity - 1)];
      const std::size_t sequence =
          slot->sequence.load(std::memory_order_acquire);
      const std::intptr_t difference =
          static_cast<std::intptr_t>(sequence) -
          static_cast<std::intptr_t>(position + 1);
      if (difference == 0) {
        if (dequeuePosition_.compare_exchange_weak(
                position, position + 1, std::memory_order_relaxed))
          break;
      } else if (difference < 0) {
        if (overflowPending_.exchange(0, std::memory_order_acq_rel) == 0)
          return false;
        copyToBuffer(buffer, bufferCapacity,
                     "Lua callback error queue overflow");
        return true;
      } else {
        position = dequeuePosition_.load(std::memory_order_relaxed);
      }
    }

    copyToBuffer(buffer, bufferCapacity, slot->message.data());
    slot->sequence.store(position + capacity, std::memory_order_release);
    return true;
  }

private:
  struct Slot {
    std::atomic<std::size_t> sequence{};
    std::array<char, messageCapacity> message{};
  };

  static void copyPart(std::array<char, messageCapacity> &target,
                       std::size_t &offset, std::string_view value) noexcept {
    const std::size_t available = messageCapacity - 1 - offset;
    const std::size_t count = std::min(available, value.size());
    if (count != 0)
      std::memcpy(target.data() + offset, value.data(), count);
    offset += count;
  }

  static void copyToBuffer(char *buffer, std::size_t bufferCapacity,
                           std::string_view value) noexcept {
    const std::size_t count = std::min(bufferCapacity - 1, value.size());
    if (count != 0)
      std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
  }

  static_assert((capacity & (capacity - 1)) == 0);
  static_assert(std::atomic<std::size_t>::is_always_lock_free);
  std::array<Slot, capacity> slots_{};
  alignas(64) std::atomic<std::size_t> enqueuePosition_{};
  alignas(64) std::atomic<std::size_t> dequeuePosition_{};
  std::atomic<std::size_t> overflowPending_{};
};

struct LuaStateSession {
  explicit LuaStateSession(lua_State *value) noexcept : state(value) {}

  lua_State *state{};
  std::recursive_mutex fallbackExecutionMutex;
  std::recursive_mutex metadataMutex;
  std::atomic<LuaStatePhase> phase{LuaStatePhase::running};
  LuaSFStateEnterHook enterHook{};
  LuaSFStateTryEnterHook tryEnterHook{};
  LuaSFStateLeaveHook leaveHook{};
  void *hookContext{};
  std::unordered_set<int> registryReferences;
  std::unordered_map<const void *, LuaRegistryReference> retainedObjects;
  bool quiescing{};
  std::unordered_map<const void *, LuaStateQuiesceCallback> quiesceCallbacks;
  DeferredCallbackErrorQueue deferredCallbackErrors;
};

struct EnteredSession {
  lua_State *state{};
  std::shared_ptr<LuaStateSession> session;
  LuaSFStateLeaveHook leaveHook{};
  void *hookContext{};
  bool usesFallback{};
};

std::mutex sessionsMutex;
std::unordered_map<lua_State *, std::shared_ptr<LuaStateSession>> sessions;
class EnteredSessionStack final {
public:
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] constexpr std::size_t capacity() const noexcept {
    return entries_.size();
  }

  EnteredSession &back() noexcept { return entries_[size_ - 1]; }

  void push_back(EnteredSession value) {
    if (size_ == entries_.size())
      throw std::bad_alloc();
    entries_[size_++] = std::move(value);
  }

  void pop_back() noexcept {
    if (size_ == 0)
      return;
    entries_[--size_] = {};
  }

private:
  std::array<EnteredSession, 32> entries_{};
  std::size_t size_{};
};

thread_local EnteredSessionStack enteredSessions;

std::shared_ptr<LuaStateSession> findSession(lua_State *state) {
  std::scoped_lock lock(sessionsMutex);
  const auto item = sessions.find(state);
  return item == sessions.end() ? std::shared_ptr<LuaStateSession>{}
                                : item->second;
}

std::shared_ptr<LuaStateSession> tryFindSession(lua_State *state) {
  std::unique_lock lock(sessionsMutex, std::try_to_lock);
  if (!lock.owns_lock())
    return {};
  const auto item = sessions.find(state);
  return item == sessions.end() ? std::shared_ptr<LuaStateSession>{}
                                : item->second;
}

std::vector<std::shared_ptr<LuaStateSession>> snapshotSessions() {
  std::scoped_lock lock(sessionsMutex);
  std::vector<std::shared_ptr<LuaStateSession>> result;
  result.reserve(sessions.size());
  for (const auto &[state, session] : sessions) {
    static_cast<void>(state);
    result.push_back(session);
  }
  return result;
}

}

namespace lua_sf {

LuaRegistryReferenceState::~LuaRegistryReferenceState() {
  if (reference < 0)
    return;
  LuaStateExecutionScope execution(state);
  if (!execution.active())
    return;
  const std::shared_ptr<detail_internal::LuaStateSession> activeSession =
      session.lock();
  if (activeSession == nullptr)
    return;
  std::scoped_lock lock(activeSession->metadataMutex);
  if (activeSession->state != state ||
      activeSession->phase.load(std::memory_order_acquire) !=
          detail_internal::LuaStatePhase::running)
    return;
  if (activeSession->registryReferences.erase(reference) != 0)
    luaL_unref(state, LUA_REGISTRYINDEX, reference);
}

LuaStateExecutionScope::LuaStateExecutionScope(lua_State *state) noexcept
    : state_(state), active_(LuaSF_enter_state(state) != 0) {}

LuaStateExecutionScope::~LuaStateExecutionScope() {
  if (active_)
    LuaSF_leave_state(state_);
}

bool LuaStateExecutionScope::active() const noexcept { return active_; }

LuaStateTryExecutionScope::LuaStateTryExecutionScope(
    lua_State *state) noexcept
    : state_(state), active_(LuaSF_try_enter_state(state) != 0) {}

LuaStateTryExecutionScope::~LuaStateTryExecutionScope() {
  if (active_)
    LuaSF_leave_state(state_);
}

bool LuaStateTryExecutionScope::active() const noexcept { return active_; }

LuaRegistryReference::LuaRegistryReference(lua_State *state, int stackIndex) {
  LuaStateExecutionScope execution(state);
  if (!execution.active())
    throw std::logic_error("Lua state is stopping");
  const std::shared_ptr<detail_internal::LuaStateSession> session =
      detail_internal::findSession(state);
  if (session == nullptr)
    throw std::logic_error("Lua registry reference has no active state");
  std::scoped_lock lock(session->metadataMutex);
  if (session->state != state ||
      session->phase.load(std::memory_order_acquire) !=
          detail_internal::LuaStatePhase::running)
    throw std::logic_error("Lua state is stopping");
  std::shared_ptr<LuaRegistryReferenceState> reference =
      std::make_shared<LuaRegistryReferenceState>(session, state);
  lua_pushvalue(state, stackIndex);
  reference->reference = luaL_ref(state, LUA_REGISTRYINDEX);
  if (reference->reference >= 0)
    session->registryReferences.insert(reference->reference);
  reference_ = std::move(reference);
}

lua_State *LuaRegistryReference::state() const noexcept {
  return reference_ == nullptr ? nullptr : reference_->state;
}

bool LuaRegistryReference::push() const {
  if (reference_ == nullptr)
    return false;
  LuaStateExecutionScope execution(reference_->state);
  if (!execution.active())
    return false;
  const std::shared_ptr<detail_internal::LuaStateSession> session =
      reference_->session.lock();
  if (session == nullptr)
    return false;
  std::scoped_lock lock(session->metadataMutex);
  if (session->state != reference_->state ||
      session->phase.load(std::memory_order_acquire) !=
          detail_internal::LuaStatePhase::running)
    return false;
  if (reference_->reference == LUA_REFNIL)
    lua_pushnil(reference_->state);
  else
    lua_rawgeti(reference_->state, LUA_REGISTRYINDEX,
                reference_->reference);
  return true;
}

bool LuaRegistryReference::pushUnderExecutionScope() const noexcept {
  if (reference_ == nullptr)
    return false;
  if (detail_internal::enteredSessions.empty())
    return false;
  const std::shared_ptr<detail_internal::LuaStateSession> session =
      reference_->session.lock();
  if (session == nullptr || reference_->reference < 0)
    return false;
  const detail_internal::EnteredSession &entered =
      detail_internal::enteredSessions.back();
  if (entered.state != reference_->state || entered.session != session ||
      session->state != reference_->state ||
      session->phase.load(std::memory_order_acquire) !=
          detail_internal::LuaStatePhase::running)
    return false;
  if (reference_->reference == LUA_REFNIL)
    lua_pushnil(reference_->state);
  else
    lua_rawgeti(reference_->state, LUA_REGISTRYINDEX,
                reference_->reference);
  return true;
}

void LuaRegistryReference::deferCallbackError(
    std::string_view label, std::string_view message) const noexcept {
  if (reference_ == nullptr)
    return;
  const std::shared_ptr<detail_internal::LuaStateSession> session =
      reference_->session.lock();
  if (session != nullptr)
    session->deferredCallbackErrors.enqueue(label, message);
}

bool LuaRegistryReference::equals(
    const LuaRegistryReference &other) const {
  if (reference_ == nullptr || other.reference_ == nullptr ||
      reference_->state != other.reference_->state)
    return false;
  LuaStateExecutionScope execution(reference_->state);
  if (!execution.active())
    return false;
  const std::shared_ptr<detail_internal::LuaStateSession> session =
      reference_->session.lock();
  if (session == nullptr || session != other.reference_->session.lock())
    return false;
  std::scoped_lock lock(session->metadataMutex);
  if (session->state != reference_->state ||
      session->phase.load(std::memory_order_acquire) !=
          detail_internal::LuaStatePhase::running)
    return false;
  if (reference_->reference == LUA_REFNIL)
    lua_pushnil(reference_->state);
  else
    lua_rawgeti(reference_->state, LUA_REGISTRYINDEX,
                reference_->reference);
  if (other.reference_->reference == LUA_REFNIL)
    lua_pushnil(reference_->state);
  else
    lua_rawgeti(reference_->state, LUA_REGISTRYINDEX,
                other.reference_->reference);
  const bool result = lua_rawequal(reference_->state, -2, -1) != 0;
  lua_pop(reference_->state, 2);
  return result;
}

LuaRegistryReference::operator bool() const noexcept {
  return reference_ != nullptr;
}

namespace detail {

void retainLuaRegistryReference(
    const void *owner, const LuaRegistryReference &reference) {
  if (owner == nullptr || !reference)
    return;
  const std::shared_ptr<detail_internal::LuaStateSession> session =
      detail_internal::findSession(reference.state());
  if (session == nullptr)
    return;
  LuaStateExecutionScope execution(reference.state());
  if (!execution.active())
    return;
  std::scoped_lock lock(session->metadataMutex);
  if (session->state != reference.state() ||
      session->phase.load(std::memory_order_acquire) !=
          detail_internal::LuaStatePhase::running)
    return;
  session->retainedObjects.insert_or_assign(owner, reference);
}

void releaseLuaRegistryReference(const void *owner) {
  if (owner == nullptr)
    return;
  for (const std::shared_ptr<detail_internal::LuaStateSession> &session :
       detail_internal::snapshotSessions()) {
    lua_State *state = nullptr;
    {
      std::scoped_lock lock(session->metadataMutex);
      state = session->state;
    }
    LuaStateExecutionScope execution(state);
    if (!execution.active())
      continue;
    std::scoped_lock lock(session->metadataMutex);
    session->retainedObjects.erase(owner);
  }
}

void registerStateQuiesceCallback(lua_State *state, const void *owner,
                                  LuaStateQuiesceCallback callback) {
  if (state == nullptr || owner == nullptr || callback == nullptr)
    throw std::invalid_argument("State quiesce callback is incomplete");
  const std::shared_ptr<detail_internal::LuaStateSession> session =
      detail_internal::findSession(state);
  if (session == nullptr)
    throw std::logic_error("State quiesce callback has no active state");
  LuaStateExecutionScope execution(state);
  if (!execution.active())
    throw std::logic_error("Lua state is stopping");
  std::scoped_lock lock(session->metadataMutex);
  if (session->state != state || session->quiescing ||
      session->phase.load(std::memory_order_acquire) !=
          detail_internal::LuaStatePhase::running)
    throw std::logic_error("Lua state is stopping");
  session->quiesceCallbacks.insert_or_assign(owner, callback);
}

void unregisterStateQuiesceCallback(lua_State *state,
                                    const void *owner) noexcept {
  if (state == nullptr || owner == nullptr)
    return;
  const std::shared_ptr<detail_internal::LuaStateSession> session =
      detail_internal::findSession(state);
  if (session == nullptr)
    return;
  std::scoped_lock lock(session->metadataMutex);
  if (session->state == state)
    session->quiesceCallbacks.erase(owner);
}

}

}

extern "C" LUASF_API int LuaSF_initialize_state(lua_State *state) {
  if (state == nullptr)
    return 1;
  std::scoped_lock lock(lua_sf::detail_internal::sessionsMutex);
  const auto item = lua_sf::detail_internal::sessions.find(state);
  if (item != lua_sf::detail_internal::sessions.end()) {
    return item->second->phase.load(std::memory_order_acquire) ==
                   lua_sf::detail_internal::LuaStatePhase::running
               ? 0
               : 1;
  }
  lua_sf::detail_internal::sessions.emplace(
      state,
      std::make_shared<lua_sf::detail_internal::LuaStateSession>(state));
  return 0;
}

extern "C" LUASF_API int LuaSF_set_state_execution_hooks(
    lua_State *state, LuaSFStateEnterHook enterHook,
    LuaSFStateTryEnterHook tryEnterHook, LuaSFStateLeaveHook leaveHook,
    void *context) {
  if (state == nullptr ||
      (enterHook == nullptr) != (tryEnterHook == nullptr) ||
      (enterHook == nullptr) != (leaveHook == nullptr))
    return 1;
  const std::shared_ptr<lua_sf::detail_internal::LuaStateSession> session =
      lua_sf::detail_internal::findSession(state);
  if (session == nullptr)
    return 1;
  lua_sf::LuaStateExecutionScope execution(state);
  if (!execution.active())
    return 1;
  std::scoped_lock lock(session->metadataMutex);
  if (session->state != state ||
      session->phase.load(std::memory_order_acquire) !=
          lua_sf::detail_internal::LuaStatePhase::running)
    return 1;
  session->enterHook = enterHook;
  session->tryEnterHook = tryEnterHook;
  session->leaveHook = leaveHook;
  session->hookContext = context;
  return 0;
}

extern "C" LUASF_API int LuaSF_enter_state(lua_State *state) {
  const std::shared_ptr<lua_sf::detail_internal::LuaStateSession> session =
      lua_sf::detail_internal::findSession(state);
  if (session == nullptr)
    return 0;
  LuaSFStateEnterHook enterHook = nullptr;
  LuaSFStateLeaveHook leaveHook = nullptr;
  void *hookContext = nullptr;
  {
    std::scoped_lock lock(session->metadataMutex);
    if (session->state != state ||
        session->phase.load(std::memory_order_acquire) !=
            lua_sf::detail_internal::LuaStatePhase::running)
      return 0;
    enterHook = session->enterHook;
    leaveHook = session->leaveHook;
    hookContext = session->hookContext;
  }
  try {
    lua_sf::detail_internal::enteredSessions.push_back(
        {state, session, leaveHook, hookContext, enterHook == nullptr});
  } catch (const std::bad_alloc &) {
    return 0;
  }
  if (enterHook != nullptr) {
    if (enterHook(state, hookContext) == 0) {
      lua_sf::detail_internal::enteredSessions.pop_back();
      return 0;
    }
  } else {
    session->fallbackExecutionMutex.lock();
  }
  {
    std::scoped_lock lock(session->metadataMutex);
    if (session->state == state &&
        session->phase.load(std::memory_order_acquire) ==
            lua_sf::detail_internal::LuaStatePhase::running)
      return 1;
  }
  lua_sf::detail_internal::EnteredSession entered =
      std::move(lua_sf::detail_internal::enteredSessions.back());
  lua_sf::detail_internal::enteredSessions.pop_back();
  if (entered.usesFallback)
    session->fallbackExecutionMutex.unlock();
  else
    entered.leaveHook(state, entered.hookContext);
  return 0;
}

extern "C" LUASF_API int LuaSF_try_enter_state(lua_State *state) noexcept {
  try {
    const std::shared_ptr<lua_sf::detail_internal::LuaStateSession> session =
        lua_sf::detail_internal::tryFindSession(state);
    if (session == nullptr)
      return 0;
    LuaSFStateTryEnterHook tryEnterHook = nullptr;
    LuaSFStateLeaveHook leaveHook = nullptr;
    void *hookContext = nullptr;
    {
      std::unique_lock lock(session->metadataMutex, std::try_to_lock);
      if (!lock.owns_lock())
        return 0;
      if (session->state != state ||
          session->phase.load(std::memory_order_acquire) !=
              lua_sf::detail_internal::LuaStatePhase::running)
        return 0;
      tryEnterHook = session->tryEnterHook;
      leaveHook = session->leaveHook;
      hookContext = session->hookContext;
    }
    if (lua_sf::detail_internal::enteredSessions.capacity() ==
        lua_sf::detail_internal::enteredSessions.size())
      return 0;
    lua_sf::detail_internal::enteredSessions.push_back(
        {state, session, leaveHook, hookContext, tryEnterHook == nullptr});
    if (tryEnterHook != nullptr) {
      if (tryEnterHook(state, hookContext) == 0) {
        lua_sf::detail_internal::enteredSessions.pop_back();
        return 0;
      }
    } else {
      bool locked = false;
      try {
        locked = session->fallbackExecutionMutex.try_lock();
      } catch (...) {
        lua_sf::detail_internal::enteredSessions.pop_back();
        return 0;
      }
      if (!locked) {
        lua_sf::detail_internal::enteredSessions.pop_back();
        return 0;
      }
    }
    if (session->phase.load(std::memory_order_acquire) ==
        lua_sf::detail_internal::LuaStatePhase::running)
      return 1;
    lua_sf::detail_internal::EnteredSession entered =
        std::move(lua_sf::detail_internal::enteredSessions.back());
    lua_sf::detail_internal::enteredSessions.pop_back();
    if (entered.usesFallback)
      session->fallbackExecutionMutex.unlock();
    else
      entered.leaveHook(state, entered.hookContext);
    return 0;
  } catch (...) {
    return 0;
  }
}

extern "C" LUASF_API void LuaSF_leave_state(lua_State *state) noexcept {
  if (lua_sf::detail_internal::enteredSessions.empty() ||
      lua_sf::detail_internal::enteredSessions.back().state != state)
    return;
  lua_sf::detail_internal::EnteredSession entered =
      std::move(lua_sf::detail_internal::enteredSessions.back());
  lua_sf::detail_internal::enteredSessions.pop_back();
  if (entered.usesFallback)
    entered.session->fallbackExecutionMutex.unlock();
  else
    entered.leaveHook(state, entered.hookContext);
}

extern "C" LUASF_API int
LuaSF_take_deferred_callback_error(lua_State *state, char *buffer,
                                   std::size_t capacity) {
  if (state == nullptr || buffer == nullptr || capacity == 0)
    return 0;
  const std::shared_ptr<lua_sf::detail_internal::LuaStateSession> session =
      lua_sf::detail_internal::findSession(state);
  if (session == nullptr)
    return 0;
  return session->deferredCallbackErrors.dequeue(buffer, capacity) ? 1 : 0;
}

extern "C" LUASF_API void LuaSF_quiesce_state(lua_State *state) noexcept {
  const std::shared_ptr<lua_sf::detail_internal::LuaStateSession> session =
      lua_sf::detail_internal::findSession(state);
  if (session == nullptr)
    return;
  std::unordered_map<const void *, lua_sf::LuaStateQuiesceCallback> callbacks;
  {
    std::scoped_lock lock(session->metadataMutex);
    if (session->state != state || session->quiescing)
      return;
    session->quiescing = true;
    callbacks.swap(session->quiesceCallbacks);
  }
  for (const auto &[owner, callback] : callbacks) {
    static_cast<void>(owner);
    callback();
  }
}

extern "C" LUASF_API void LuaSF_shutdown_state(lua_State *state) {
  const std::shared_ptr<lua_sf::detail_internal::LuaStateSession> session =
      lua_sf::detail_internal::findSession(state);
  if (session == nullptr)
    return;
  LuaSF_quiesce_state(state);
  lua_sf::LuaStateExecutionScope execution(state);
  if (!execution.active())
    return;
  lua_sf::detail_internal::LuaStatePhase expected =
      lua_sf::detail_internal::LuaStatePhase::running;
  if (!session->phase.compare_exchange_strong(
          expected, lua_sf::detail_internal::LuaStatePhase::stopping,
          std::memory_order_acq_rel, std::memory_order_acquire))
    return;
  {
    std::scoped_lock lock(session->metadataMutex);
    session->retainedObjects.clear();
    for (const int reference : session->registryReferences)
      luaL_unref(state, LUA_REGISTRYINDEX, reference);
    session->registryReferences.clear();
    session->state = nullptr;
    session->phase.store(lua_sf::detail_internal::LuaStatePhase::stopped,
                         std::memory_order_release);
  }
  std::scoped_lock lock(lua_sf::detail_internal::sessionsMutex);
  const auto item = lua_sf::detail_internal::sessions.find(state);
  if (item != lua_sf::detail_internal::sessions.end() &&
      item->second == session)
    lua_sf::detail_internal::sessions.erase(item);
}
