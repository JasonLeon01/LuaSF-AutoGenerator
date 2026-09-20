#include <LuaGlue/Lifecycle.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lua_glue::detail_internal {

enum class LuaStatePhase {
    running,
    stopping,
    stopped,
};

struct LuaStateSession;

}  // namespace lua_glue::detail_internal

namespace lua_glue {

struct RegistryReferenceState {
    RegistryReferenceState(
        std::weak_ptr<detail_internal::LuaStateSession> session,
        lua_State* state, lua_State* origin) noexcept
        : session(std::move(session)), state(state), origin(origin) {}

    ~RegistryReferenceState();

    std::weak_ptr<detail_internal::LuaStateSession> session;
    lua_State* state{};
    lua_State* origin{};
    int reference{LUA_NOREF};
};

}  // namespace lua_glue

namespace lua_glue::detail_internal {

class DeferredCallbackErrorQueue final {
public:
    static constexpr std::size_t capacity = 64;
    static constexpr std::size_t messageCapacity = 512;

    DeferredCallbackErrorQueue() noexcept {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            slots_[index].sequence.store(index, std::memory_order_relaxed);
        }
    }

    bool enqueue(std::string_view label, std::string_view message) noexcept {
        std::size_t position = enqueuePosition_.load(std::memory_order_relaxed);
        Slot* slot = nullptr;
        for (;;) {
            slot = &slots_[position & (capacity - 1)];
            const std::size_t sequence =
                slot->sequence.load(std::memory_order_acquire);
            const std::intptr_t difference =
                static_cast<std::intptr_t>(sequence) -
                static_cast<std::intptr_t>(position);
            if (difference == 0) {
                if (enqueuePosition_.compare_exchange_weak(
                        position, position + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (difference < 0) {
                overflowPending_.store(1, std::memory_order_release);
                return false;
            } else {
                position = enqueuePosition_.load(std::memory_order_relaxed);
            }
        }

        std::size_t offset = 0;
        copyPart(slot->message, offset, label);
        if (!label.empty() && !message.empty()) {
            copyPart(slot->message, offset, ": ");
        }
        copyPart(slot->message, offset, message);
        slot->message[offset] = '\0';
        slot->sequence.store(position + 1, std::memory_order_release);
        return true;
    }

    bool dequeue(char* buffer, std::size_t bufferCapacity) noexcept {
        std::size_t position = dequeuePosition_.load(std::memory_order_relaxed);
        Slot* slot = nullptr;
        for (;;) {
            slot = &slots_[position & (capacity - 1)];
            const std::size_t sequence =
                slot->sequence.load(std::memory_order_acquire);
            const std::intptr_t difference =
                static_cast<std::intptr_t>(sequence) -
                static_cast<std::intptr_t>(position + 1);
            if (difference == 0) {
                if (dequeuePosition_.compare_exchange_weak(
                        position, position + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (difference < 0) {
                if (overflowPending_.exchange(0, std::memory_order_acq_rel) ==
                    0) {
                    return false;
                }
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

    static void copyPart(std::array<char, messageCapacity>& target,
                         std::size_t& offset, std::string_view value) noexcept {
        const std::size_t available = messageCapacity - 1 - offset;
        const std::size_t count = std::min(available, value.size());
        if (count != 0) {
            std::memcpy(target.data() + offset, value.data(), count);
        }
        offset += count;
    }

    static void copyToBuffer(char* buffer, std::size_t bufferCapacity,
                             std::string_view value) noexcept {
        const std::size_t count = std::min(bufferCapacity - 1, value.size());
        if (count != 0) {
            std::memcpy(buffer, value.data(), count);
        }
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
    explicit LuaStateSession(lua_State* value) noexcept : state(value) {}

    lua_State* state{};
    std::recursive_mutex fallbackExecutionMutex;
    std::recursive_mutex metadataMutex;
    std::atomic<LuaStatePhase> phase{LuaStatePhase::running};
    StateEnterHook enterHook{};
    StateTryEnterHook tryEnterHook{};
    StateLeaveHook leaveHook{};
    void* hookContext{};
    StateOwnsExecutionHook ownsExecutionHook{};
    std::atomic<std::uint64_t> hookGeneration{};
    std::unordered_set<int> registryReferences;
    std::unordered_map<const void*, RegistryReference> retainedObjects;
    bool quiescing{};
    std::unordered_map<const void*, StateQuiesceCallback> quiesceCallbacks;
    DeferredCallbackErrorQueue deferredCallbackErrors;
};

struct EnteredSession {
    lua_State* state{};
    lua_State* requestedState{};
    std::shared_ptr<LuaStateSession> session;
    StateLeaveHook leaveHook{};
    void* hookContext{};
    bool usesFallback{};
    StateOwnsExecutionHook ownsExecutionHook{};
    std::uint64_t hookGeneration{};
    bool gateAcquired{};
};

std::mutex sessionsMutex;
std::unordered_map<lua_State*, std::shared_ptr<LuaStateSession>> sessions;
std::unordered_map<lua_State*, lua_State*> stateAliases;
class EnteredSessionStack final {
public:
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept {
        return entries_.size();
    }

    EnteredSession& back() noexcept {
        return entries_[size_ - 1];
    }

    void push_back(EnteredSession value) {
        if (size_ == entries_.size()) {
            throw std::bad_alloc();
        }
        entries_[size_++] = std::move(value);
    }

    void pop_back() noexcept {
        if (size_ == 0) {
            return;
        }
        entries_[--size_] = {};
    }

private:
    std::array<EnteredSession, 32> entries_{};
    std::size_t size_{};
};

thread_local EnteredSessionStack enteredSessions;

const EnteredSession* enteredSessionForState(lua_State* state) noexcept {
    if (state == nullptr || enteredSessions.empty()) {
        return nullptr;
    }
    const EnteredSession& entered = enteredSessions.back();
    if ((state != entered.state && state != entered.requestedState) ||
        entered.session == nullptr ||
        entered.session->phase.load(std::memory_order_acquire) !=
            LuaStatePhase::running) {
        return nullptr;
    }
    // Entries exist while an enter hook is still acquiring its gate, and a
    // host may temporarily release that gate. Reuse only the VM identity;
    // callers must still execute the normal enter hook and phase checks.
    return &entered;
}

lua_State* mainThreadFromRegistry(lua_State* state) noexcept {
    if (state == nullptr || lua_checkstack(state, 1) == 0) {
        return nullptr;
    }

    const int originalTop = lua_gettop(state);
    lua_rawgeti(state, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
    lua_State* mainState = lua_tothread(state, -1);
    lua_settop(state, originalTop);
    return mainState;
}

std::shared_ptr<LuaStateSession> findSessionByMainState(lua_State* mainState) {
    if (mainState == nullptr) {
        return {};
    }
    if (const auto* entered = enteredSessionForState(mainState);
        entered != nullptr && entered->state == mainState) {
        return entered->session;
    }
    std::scoped_lock lock(sessionsMutex);
    const auto item = sessions.find(mainState);
    return item == sessions.end() ? std::shared_ptr<LuaStateSession>{}
                                  : item->second;
}

struct ResolvedSession {
    lua_State* mainState{};
    std::shared_ptr<LuaStateSession> session;
};

ResolvedSession findResolvedSessionLocked(lua_State* state) {
    const auto alias = stateAliases.find(state);
    if (alias == stateAliases.end()) {
        return {};
    }
    const auto session = sessions.find(alias->second);
    return session == sessions.end()
               ? ResolvedSession{}
               : ResolvedSession{alias->second, session->second};
}

ResolvedSession findResolvedSession(lua_State* state) noexcept {
    if (state == nullptr) {
        return {};
    }
    if (const auto* entered = enteredSessionForState(state)) {
        return {entered->state, entered->session};
    }
    try {
        std::scoped_lock lock(sessionsMutex);
        return findResolvedSessionLocked(state);
    } catch (...) {
        return {};
    }
}

int createProtectedRegistryReference(lua_State* state, int index) {
    if (lua_checkstack(state, 3) == 0) {
        throw std::runtime_error("Lua registry stack cannot grow");
    }
    index = lua_absindex(state, index);
    lua_pushcfunction(state, [](lua_State* inner) -> int {
        lua_settop(inner, 1);
        const int reference = luaL_ref(inner, LUA_REGISTRYINDEX);
        lua_pushinteger(inner, reference);
        return 1;
    });
    lua_pushvalue(state, index);
    if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        const std::string error =
            message ? message : "Unable to retain Lua registry value";
        lua_pop(state, 1);
        throw std::runtime_error(error);
    }
    const int reference = static_cast<int>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    return reference;
}

bool addStateAliasUnderExecution(
    lua_State* state, lua_State* mainState,
    const std::shared_ptr<LuaStateSession>& session) noexcept {
    if (state == nullptr || mainState == nullptr || session == nullptr) {
        return false;
    }

    int threadReference = LUA_NOREF;
    if (state != mainState) {
        lua_pushthread(state);
        try {
            threadReference = createProtectedRegistryReference(state, -1);
        } catch (...) {
            lua_pop(state, 1);
            return false;
        }
        lua_pop(state, 1);
        if (threadReference < 0) {
            return false;
        }
        try {
            std::scoped_lock lock(session->metadataMutex);
            if (session->state != mainState ||
                session->phase.load(std::memory_order_acquire) !=
                    LuaStatePhase::running ||
                !session->registryReferences.insert(threadReference).second) {
                luaL_unref(state, LUA_REGISTRYINDEX, threadReference);
                return false;
            }
        } catch (...) {
            luaL_unref(state, LUA_REGISTRYINDEX, threadReference);
            return false;
        }
    }

    bool aliasRegistered = false;
    try {
        std::scoped_lock lock(sessionsMutex);
        const auto activeSession = sessions.find(mainState);
        if (activeSession != sessions.end() &&
            activeSession->second == session) {
            stateAliases.insert_or_assign(state, mainState);
            aliasRegistered = true;
        }
    } catch (...) {}
    if (aliasRegistered) {
        return true;
    }

    if (threadReference >= 0) {
        try {
            std::scoped_lock lock(session->metadataMutex);
            session->registryReferences.erase(threadReference);
        } catch (...) {}
        luaL_unref(state, LUA_REGISTRYINDEX, threadReference);
    }
    return false;
}

ResolvedSession resolveSession(lua_State* state) noexcept {
    return findResolvedSession(state);
}

ResolvedSession learnRegistryReferenceSessionUnderExecution(
    lua_State* state) noexcept {
    ResolvedSession resolved = findResolvedSession(state);
    if (resolved.session != nullptr) {
        return resolved;
    }

    // Registry-reference construction receives the currently executing Lua
    // thread. The host therefore already owns the VM either through a LuaGlue
    // scope or through the same lock installed as its execution hooks.
    lua_State* mainState = mainThreadFromRegistry(state);
    if (mainState == nullptr) {
        return {};
    }
    std::shared_ptr<LuaStateSession> session;
    if (!enteredSessions.empty()) {
        const EnteredSession& entered = enteredSessions.back();
        if (mainState != entered.state) {
            return {};
        }
        session = entered.session;
    } else {
        session = findSessionByMainState(mainState);
    }
    if (session == nullptr ||
        session->phase.load(std::memory_order_acquire) !=
            LuaStatePhase::running ||
        !addStateAliasUnderExecution(state, mainState, session)) {
        return {};
    }
    return {mainState, std::move(session)};
}

ResolvedSession tryResolveSession(lua_State* state) noexcept {
    if (state == nullptr) {
        return {};
    }
    if (const auto* entered = enteredSessionForState(state)) {
        return {entered->state, entered->session};
    }

    std::unique_lock lock(sessionsMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return {};
    }
    return findResolvedSessionLocked(state);
}

std::vector<std::shared_ptr<LuaStateSession>> snapshotSessions() {
    std::scoped_lock lock(sessionsMutex);
    std::vector<std::shared_ptr<LuaStateSession>> result;
    result.reserve(sessions.size());
    for (const auto& [state, session] : sessions) {
        static_cast<void>(state);
        result.push_back(session);
    }
    return result;
}

}  // namespace lua_glue::detail_internal

namespace lua_glue {

RegistryReferenceState::~RegistryReferenceState() {
    if (reference < 0) {
        return;
    }
    const std::shared_ptr<detail_internal::LuaStateSession> activeSession =
        session.lock();
    if (activeSession == nullptr ||
        activeSession->phase.load(std::memory_order_acquire) !=
            detail_internal::LuaStatePhase::running) {
        return;
    }
    detail::AccessScope execution(state);
    if (!execution.active()) {
        return;
    }
    std::scoped_lock lock(activeSession->metadataMutex);
    if (activeSession->state != state ||
        activeSession->phase.load(std::memory_order_acquire) !=
            detail_internal::LuaStatePhase::running) {
        return;
    }
    if (activeSession->registryReferences.erase(reference) != 0) {
        luaL_unref(state, LUA_REGISTRYINDEX, reference);
    }
}

detail::AccessScope::AccessScope(lua_State* state) noexcept {
    const auto* entered = detail_internal::enteredSessionForState(state);
    if (entered != nullptr && entered->gateAcquired &&
        entered->ownsExecutionHook != nullptr &&
        entered->session->hookGeneration.load(std::memory_order_acquire) ==
            entered->hookGeneration &&
        entered->ownsExecutionHook(entered->state, entered->hookContext) != 0 &&
        entered->session->hookGeneration.load(std::memory_order_acquire) ==
            entered->hookGeneration &&
        entered->session->phase.load(std::memory_order_acquire) ==
            detail_internal::LuaStatePhase::running) {
        state_ = entered->state;
        active_ = true;
        borrowed_ = true;
        return;
    }
    active_ = EnterState(state) != 0;
    if (active_ && !detail_internal::enteredSessions.empty()) {
        state_ = detail_internal::enteredSessions.back().state;
    }
}

detail::AccessScope::~AccessScope() {
    if (active_ && !borrowed_) {
        LeaveState(state_);
    }
}

bool detail::AccessScope::active() const noexcept {
    return active_;
}

ExecutionScope::ExecutionScope(lua_State* state) noexcept
    : active_(EnterState(state) != 0) {
    if (active_ && !detail_internal::enteredSessions.empty()) {
        state_ = detail_internal::enteredSessions.back().state;
    }
}

ExecutionScope::~ExecutionScope() {
    if (active_) {
        LeaveState(state_);
    }
}

bool ExecutionScope::active() const noexcept {
    return active_;
}

TryExecutionScope::TryExecutionScope(lua_State* state) noexcept
    : active_(TryEnterState(state) != 0) {
    if (active_ && !detail_internal::enteredSessions.empty()) {
        state_ = detail_internal::enteredSessions.back().state;
    }
}

TryExecutionScope::~TryExecutionScope() {
    if (active_) {
        LeaveState(state_);
    }
}

bool TryExecutionScope::active() const noexcept {
    return active_;
}

RegistryReference::RegistryReference(lua_State* state, int stackIndex) {
    if (state == nullptr) {
        throw std::invalid_argument("Lua registry reference has no state");
    }
    const detail_internal::ResolvedSession resolved =
        detail_internal::learnRegistryReferenceSessionUnderExecution(state);
    lua_State* mainState = resolved.mainState;
    const std::shared_ptr<detail_internal::LuaStateSession>& session =
        resolved.session;
    if (mainState == nullptr || session == nullptr) {
        throw std::logic_error("Lua registry reference has no main state");
    }
    detail::AccessScope execution(mainState);
    if (!execution.active()) {
        throw std::logic_error("Lua state is stopping");
    }
    const int absoluteStackIndex = lua_absindex(state, stackIndex);
    std::scoped_lock lock(session->metadataMutex);
    if (session->state != mainState ||
        session->phase.load(std::memory_order_acquire) !=
            detail_internal::LuaStatePhase::running) {
        throw std::logic_error("Lua state is stopping");
    }
    std::shared_ptr<RegistryReferenceState> reference =
        std::make_shared<RegistryReferenceState>(session, mainState, state);
    reference->reference = detail_internal::createProtectedRegistryReference(
        state, absoluteStackIndex);
    if (reference->reference >= 0) {
        try {
            session->registryReferences.insert(reference->reference);
        } catch (...) {
            luaL_unref(state, LUA_REGISTRYINDEX, reference->reference);
            reference->reference = LUA_NOREF;
            throw;
        }
    }
    reference_ = std::move(reference);
}

lua_State* RegistryReference::state() const noexcept {
    if (reference_ == nullptr) {
        return nullptr;
    }
    const std::shared_ptr<detail_internal::LuaStateSession> session =
        reference_->session.lock();
    if (session == nullptr || session->phase.load(std::memory_order_acquire) !=
                                  detail_internal::LuaStatePhase::running) {
        return nullptr;
    }
    return reference_->state;
}

lua_State* RegistryReference::originState() const noexcept {
    return state() != nullptr ? reference_->origin : nullptr;
}

bool RegistryReference::push() const {
    if (reference_ == nullptr) {
        return false;
    }
    const std::shared_ptr<detail_internal::LuaStateSession> session =
        reference_->session.lock();
    if (session == nullptr || session->phase.load(std::memory_order_acquire) !=
                                  detail_internal::LuaStatePhase::running) {
        return false;
    }
    detail::AccessScope execution(reference_->state);
    if (!execution.active()) {
        return false;
    }
    std::scoped_lock lock(session->metadataMutex);
    if (session->state != reference_->state ||
        session->phase.load(std::memory_order_acquire) !=
            detail_internal::LuaStatePhase::running) {
        return false;
    }
    if (lua_checkstack(reference_->state, 2) == 0) {
        return false;
    }
    if (reference_->reference == LUA_REFNIL) {
        lua_pushnil(reference_->state);
    } else {
        lua_rawgeti(reference_->state, LUA_REGISTRYINDEX,
                    reference_->reference);
    }
    return true;
}

bool RegistryReference::push(lua_State* target) const {
    lua_State* source = state();
    if (source == nullptr || target == nullptr) {
        return false;
    }
    // The reference already identifies its main VM and retains its origin
    // coroutine through the registered session. Other targets still require
    // an alias lookup; the entered session is checked again before pushing.
    if (target != source && target != reference_->origin) {
        const auto resolved = detail_internal::findResolvedSession(target);
        if (resolved.mainState != source) {
            throw std::invalid_argument(
                "Cannot move a Lua value between independent states");
        }
    }
    detail::AccessScope execution(source);
    if (!execution.active() || !pushUnderExecutionScope()) {
        return false;
    }
    if (source != target) {
        if (lua_checkstack(target, 1) == 0) {
            lua_pop(source, 1);
            throw std::runtime_error("Lua target stack cannot grow");
        }
        lua_xmove(source, target, 1);
    }
    return true;
}

bool RegistryReference::pushUnderExecutionScope() const noexcept {
    if (reference_ == nullptr) {
        return false;
    }
    if (detail_internal::enteredSessions.empty()) {
        return false;
    }
    const std::shared_ptr<detail_internal::LuaStateSession> session =
        reference_->session.lock();
    if (session == nullptr || reference_->reference == LUA_NOREF) {
        return false;
    }
    const detail_internal::EnteredSession& entered =
        detail_internal::enteredSessions.back();
    if (entered.state != reference_->state || entered.session != session ||
        session->state != reference_->state ||
        session->phase.load(std::memory_order_acquire) !=
            detail_internal::LuaStatePhase::running) {
        return false;
    }
    if (lua_checkstack(reference_->state, 2) == 0) {
        return false;
    }
    if (reference_->reference == LUA_REFNIL) {
        lua_pushnil(reference_->state);
    } else {
        lua_rawgeti(reference_->state, LUA_REGISTRYINDEX,
                    reference_->reference);
    }
    return true;
}

void RegistryReference::deferCallbackError(
    std::string_view label, std::string_view message) const noexcept {
    if (reference_ == nullptr) {
        return;
    }
    const std::shared_ptr<detail_internal::LuaStateSession> session =
        reference_->session.lock();
    if (session != nullptr) {
        session->deferredCallbackErrors.enqueue(label, message);
    }
}

bool RegistryReference::equals(const RegistryReference& other) const {
    if (reference_ == nullptr || other.reference_ == nullptr ||
        reference_->state != other.reference_->state) {
        return false;
    }
    const std::shared_ptr<detail_internal::LuaStateSession> session =
        reference_->session.lock();
    if (session == nullptr || session != other.reference_->session.lock() ||
        session->phase.load(std::memory_order_acquire) !=
            detail_internal::LuaStatePhase::running) {
        return false;
    }
    detail::AccessScope execution(reference_->state);
    if (!execution.active()) {
        return false;
    }
    std::scoped_lock lock(session->metadataMutex);
    if (session->state != reference_->state ||
        session->phase.load(std::memory_order_acquire) !=
            detail_internal::LuaStatePhase::running) {
        return false;
    }
    if (lua_checkstack(reference_->state, 2) == 0) {
        return false;
    }
    if (reference_->reference == LUA_REFNIL) {
        lua_pushnil(reference_->state);
    } else {
        lua_rawgeti(reference_->state, LUA_REGISTRYINDEX,
                    reference_->reference);
    }
    if (other.reference_->reference == LUA_REFNIL) {
        lua_pushnil(reference_->state);
    } else {
        lua_rawgeti(reference_->state, LUA_REGISTRYINDEX,
                    other.reference_->reference);
    }
    const bool result = lua_rawequal(reference_->state, -2, -1) != 0;
    lua_pop(reference_->state, 2);
    return result;
}

RegistryReference::operator bool() const noexcept {
    return reference_ != nullptr;
}

namespace detail {

void registerLuaThreadForRegistryReference(lua_State* state) {
    if (state == nullptr) {
        throw std::invalid_argument("Lua registry reference has no state");
    }
    const detail_internal::ResolvedSession resolved =
        detail_internal::learnRegistryReferenceSessionUnderExecution(state);
    if (resolved.session == nullptr) {
        throw std::logic_error(
            "Lua registry reference requires an entered main state");
    }
}

void retainLuaRegistryReference(const void* owner,
                                const RegistryReference& reference) {
    if (owner == nullptr || !reference) {
        return;
    }
    lua_State* state = reference.state();
    if (state == nullptr) {
        return;
    }
    const std::shared_ptr<detail_internal::LuaStateSession> session =
        detail_internal::findSessionByMainState(state);
    if (session == nullptr) {
        return;
    }
    ExecutionScope execution(state);
    if (!execution.active()) {
        return;
    }
    std::scoped_lock lock(session->metadataMutex);
    if (session->state != state ||
        session->phase.load(std::memory_order_acquire) !=
            detail_internal::LuaStatePhase::running) {
        return;
    }
    session->retainedObjects.insert_or_assign(owner, reference);
}

void releaseLuaRegistryReference(const void* owner) {
    if (owner == nullptr) {
        return;
    }
    for (const std::shared_ptr<detail_internal::LuaStateSession>& session :
         detail_internal::snapshotSessions()) {
        lua_State* state = nullptr;
        {
            std::scoped_lock lock(session->metadataMutex);
            state = session->state;
        }
        ExecutionScope execution(state);
        if (!execution.active()) {
            continue;
        }
        std::scoped_lock lock(session->metadataMutex);
        session->retainedObjects.erase(owner);
    }
}

void registerStateQuiesceCallback(lua_State* state, const void* owner,
                                  StateQuiesceCallback callback) {
    if (state == nullptr || owner == nullptr || callback == nullptr) {
        throw std::invalid_argument("State quiesce callback is incomplete");
    }
    const detail_internal::ResolvedSession resolved =
        detail_internal::resolveSession(state);
    lua_State* mainState = resolved.mainState;
    const std::shared_ptr<detail_internal::LuaStateSession>& session =
        resolved.session;
    if (session == nullptr) {
        throw std::logic_error("State quiesce callback has no active state");
    }
    ExecutionScope execution(mainState);
    if (!execution.active()) {
        throw std::logic_error("Lua state is stopping");
    }
    std::scoped_lock lock(session->metadataMutex);
    if (session->state != mainState || session->quiescing ||
        session->phase.load(std::memory_order_acquire) !=
            detail_internal::LuaStatePhase::running) {
        throw std::logic_error("Lua state is stopping");
    }
    session->quiesceCallbacks.insert_or_assign(owner, callback);
}

void unregisterStateQuiesceCallback(lua_State* state,
                                    const void* owner) noexcept {
    if (state == nullptr || owner == nullptr) {
        return;
    }
    const detail_internal::ResolvedSession resolved =
        detail_internal::findResolvedSession(state);
    lua_State* mainState = resolved.mainState;
    const std::shared_ptr<detail_internal::LuaStateSession>& session =
        resolved.session;
    if (session == nullptr) {
        return;
    }
    std::scoped_lock lock(session->metadataMutex);
    if (session->state == mainState) {
        session->quiesceCallbacks.erase(owner);
    }
}

}  // namespace detail

int InitializeState(lua_State* state) {
    if (state == nullptr) {
        return 1;
    }
    const lua_glue::detail_internal::ResolvedSession existing =
        lua_glue::detail_internal::findResolvedSession(state);
    if (existing.session != nullptr) {
        return existing.session->phase.load(std::memory_order_acquire) ==
                       lua_glue::detail_internal::LuaStatePhase::running
                   ? 0
                   : 1;
    }

    // Initialization is called by the thread currently executing registration,
    // before a LuaGlue execution gate necessarily exists. It is the only public
    // entry point that learns a VM identity without a pre-existing session.
    lua_State* mainState =
        lua_glue::detail_internal::mainThreadFromRegistry(state);
    if (mainState == nullptr) {
        return 1;
    }
    std::shared_ptr<lua_glue::detail_internal::LuaStateSession> session;
    bool createdSession = false;
    try {
        std::scoped_lock lock(lua_glue::detail_internal::sessionsMutex);
        const auto item = lua_glue::detail_internal::sessions.find(mainState);
        if (item != lua_glue::detail_internal::sessions.end()) {
            if (item->second->phase.load(std::memory_order_acquire) !=
                lua_glue::detail_internal::LuaStatePhase::running) {
                return 1;
            }
            session = item->second;
        } else {
            session =
                std::make_shared<lua_glue::detail_internal::LuaStateSession>(
                    mainState);
            lua_glue::detail_internal::sessions.emplace(mainState, session);
            createdSession = true;
        }
        try {
            lua_glue::detail_internal::stateAliases.insert_or_assign(mainState,
                                                                     mainState);
        } catch (...) {
            lua_glue::detail_internal::stateAliases.erase(mainState);
            if (createdSession) {
                lua_glue::detail_internal::sessions.erase(mainState);
            }
            throw;
        }
    } catch (...) {
        return 1;
    }

    bool initialized = false;
    {
        lua_glue::ExecutionScope execution(mainState);
        if (execution.active()) {
            initialized =
                state == mainState ||
                lua_glue::detail_internal::addStateAliasUnderExecution(
                    state, mainState, session);
        }
    }
    if (!initialized && createdSession) {
        ShutdownState(mainState);
    }
    return initialized ? 0 : 1;
}

int SetStateExecutionHooks(lua_State* state, StateEnterHook enterHook,
                           StateTryEnterHook tryEnterHook,
                           StateLeaveHook leaveHook, void* context) {
    if (state == nullptr ||
        (enterHook == nullptr) != (tryEnterHook == nullptr) ||
        (enterHook == nullptr) != (leaveHook == nullptr)) {
        return 1;
    }
    const lua_glue::detail_internal::ResolvedSession resolved =
        lua_glue::detail_internal::resolveSession(state);
    lua_State* mainState = resolved.mainState;
    const std::shared_ptr<lua_glue::detail_internal::LuaStateSession>& session =
        resolved.session;
    if (session == nullptr) {
        return 1;
    }
    lua_glue::ExecutionScope execution(mainState);
    if (!execution.active()) {
        return 1;
    }
    std::scoped_lock lock(session->metadataMutex);
    if (session->state != mainState ||
        session->phase.load(std::memory_order_acquire) !=
            lua_glue::detail_internal::LuaStatePhase::running) {
        return 1;
    }
    session->hookGeneration.fetch_add(1, std::memory_order_acq_rel);
    session->ownsExecutionHook = nullptr;
    session->enterHook = enterHook;
    session->tryEnterHook = tryEnterHook;
    session->leaveHook = leaveHook;
    session->hookContext = context;
    return 0;
}

int SetStateOwnsExecutionHook(lua_State* state, StateOwnsExecutionHook owns) {
    const detail_internal::ResolvedSession resolved =
        detail_internal::resolveSession(state);
    const auto& session = resolved.session;
    if (session == nullptr) {
        return 1;
    }
    ExecutionScope execution(resolved.mainState);
    if (!execution.active()) {
        return 1;
    }
    std::scoped_lock lock(session->metadataMutex);
    if (session->state != resolved.mainState ||
        session->phase.load(std::memory_order_acquire) !=
            detail_internal::LuaStatePhase::running ||
        (owns != nullptr && session->enterHook == nullptr)) {
        return 1;
    }
    session->hookGeneration.fetch_add(1, std::memory_order_acq_rel);
    session->ownsExecutionHook = owns;
    return 0;
}

int EnterState(lua_State* state) {
    const lua_glue::detail_internal::ResolvedSession resolved =
        lua_glue::detail_internal::resolveSession(state);
    lua_State* mainState = resolved.mainState;
    const std::shared_ptr<lua_glue::detail_internal::LuaStateSession>& session =
        resolved.session;
    if (session == nullptr) {
        return 0;
    }
    StateEnterHook enterHook = nullptr;
    StateLeaveHook leaveHook = nullptr;
    void* hookContext = nullptr;
    StateOwnsExecutionHook ownsExecutionHook = nullptr;
    std::uint64_t hookGeneration = 0;
    {
        std::scoped_lock lock(session->metadataMutex);
        if (session->state != mainState ||
            session->phase.load(std::memory_order_acquire) !=
                lua_glue::detail_internal::LuaStatePhase::running) {
            return 0;
        }
        enterHook = session->enterHook;
        leaveHook = session->leaveHook;
        hookContext = session->hookContext;
        ownsExecutionHook = session->ownsExecutionHook;
        hookGeneration =
            session->hookGeneration.load(std::memory_order_acquire);
    }
    try {
        lua_glue::detail_internal::enteredSessions.push_back(
            {mainState, state, session, leaveHook, hookContext,
             enterHook == nullptr, ownsExecutionHook, hookGeneration, false});
    } catch (const std::bad_alloc&) {
        return 0;
    }
    if (enterHook != nullptr) {
        if (enterHook(mainState, hookContext) == 0) {
            lua_glue::detail_internal::enteredSessions.pop_back();
            return 0;
        }
    } else {
        session->fallbackExecutionMutex.lock();
    }
    {
        std::scoped_lock lock(session->metadataMutex);
        if (session->state == mainState &&
            session->phase.load(std::memory_order_acquire) ==
                lua_glue::detail_internal::LuaStatePhase::running) {
            lua_glue::detail_internal::enteredSessions.back().gateAcquired =
                true;
            return 1;
        }
    }
    lua_glue::detail_internal::EnteredSession entered =
        std::move(lua_glue::detail_internal::enteredSessions.back());
    lua_glue::detail_internal::enteredSessions.pop_back();
    if (entered.usesFallback) {
        session->fallbackExecutionMutex.unlock();
    } else {
        entered.leaveHook(mainState, entered.hookContext);
    }
    return 0;
}

int TryEnterState(lua_State* state) noexcept {
    try {
        const lua_glue::detail_internal::ResolvedSession resolved =
            lua_glue::detail_internal::tryResolveSession(state);
        lua_State* mainState = resolved.mainState;
        const std::shared_ptr<lua_glue::detail_internal::LuaStateSession>&
            session = resolved.session;
        if (session == nullptr) {
            return 0;
        }
        StateTryEnterHook tryEnterHook = nullptr;
        StateLeaveHook leaveHook = nullptr;
        void* hookContext = nullptr;
        StateOwnsExecutionHook ownsExecutionHook = nullptr;
        std::uint64_t hookGeneration = 0;
        {
            std::unique_lock lock(session->metadataMutex, std::try_to_lock);
            if (!lock.owns_lock()) {
                return 0;
            }
            if (session->state != mainState ||
                session->phase.load(std::memory_order_acquire) !=
                    lua_glue::detail_internal::LuaStatePhase::running) {
                return 0;
            }
            tryEnterHook = session->tryEnterHook;
            leaveHook = session->leaveHook;
            hookContext = session->hookContext;
            ownsExecutionHook = session->ownsExecutionHook;
            hookGeneration =
                session->hookGeneration.load(std::memory_order_acquire);
        }
        if (lua_glue::detail_internal::enteredSessions.capacity() ==
            lua_glue::detail_internal::enteredSessions.size()) {
            return 0;
        }
        lua_glue::detail_internal::enteredSessions.push_back(
            {mainState, state, session, leaveHook, hookContext,
             tryEnterHook == nullptr, ownsExecutionHook, hookGeneration,
             false});
        if (tryEnterHook != nullptr) {
            if (tryEnterHook(mainState, hookContext) == 0) {
                lua_glue::detail_internal::enteredSessions.pop_back();
                return 0;
            }
        } else {
            bool locked = false;
            try {
                locked = session->fallbackExecutionMutex.try_lock();
            } catch (...) {
                lua_glue::detail_internal::enteredSessions.pop_back();
                return 0;
            }
            if (!locked) {
                lua_glue::detail_internal::enteredSessions.pop_back();
                return 0;
            }
        }
        if (session->phase.load(std::memory_order_acquire) ==
            lua_glue::detail_internal::LuaStatePhase::running) {
            lua_glue::detail_internal::enteredSessions.back().gateAcquired =
                true;
            return 1;
        }
        lua_glue::detail_internal::EnteredSession entered =
            std::move(lua_glue::detail_internal::enteredSessions.back());
        lua_glue::detail_internal::enteredSessions.pop_back();
        if (entered.usesFallback) {
            session->fallbackExecutionMutex.unlock();
        } else {
            entered.leaveHook(mainState, entered.hookContext);
        }
        return 0;
    } catch (...) {
        return 0;
    }
}

void LeaveState(lua_State* state) noexcept {
    if (lua_glue::detail_internal::enteredSessions.empty()) {
        return;
    }
    const lua_glue::detail_internal::EnteredSession& active =
        lua_glue::detail_internal::enteredSessions.back();
    if (state != active.state && state != active.requestedState) {
        return;
    }
    lua_State* mainState = active.state;
    lua_glue::detail_internal::EnteredSession entered =
        std::move(lua_glue::detail_internal::enteredSessions.back());
    lua_glue::detail_internal::enteredSessions.pop_back();
    if (entered.usesFallback) {
        entered.session->fallbackExecutionMutex.unlock();
    } else {
        entered.leaveHook(mainState, entered.hookContext);
    }
}

int TakeDeferredCallbackError(lua_State* state, char* buffer,
                              std::size_t capacity) {
    if (state == nullptr || buffer == nullptr || capacity == 0) {
        return 0;
    }
    const lua_glue::detail_internal::ResolvedSession resolved =
        lua_glue::detail_internal::resolveSession(state);
    const std::shared_ptr<lua_glue::detail_internal::LuaStateSession>& session =
        resolved.session;
    if (session == nullptr) {
        return 0;
    }
    return session->deferredCallbackErrors.dequeue(buffer, capacity) ? 1 : 0;
}

void QuiesceState(lua_State* state) noexcept {
    const lua_glue::detail_internal::ResolvedSession resolved =
        lua_glue::detail_internal::findResolvedSession(state);
    lua_State* mainState = resolved.mainState;
    const std::shared_ptr<lua_glue::detail_internal::LuaStateSession>& session =
        resolved.session;
    if (session == nullptr) {
        return;
    }
    std::unordered_map<const void*, lua_glue::StateQuiesceCallback> callbacks;
    {
        std::scoped_lock lock(session->metadataMutex);
        if (session->state != mainState || session->quiescing) {
            return;
        }
        session->quiescing = true;
        callbacks.swap(session->quiesceCallbacks);
    }
    for (const auto& [owner, callback] : callbacks) {
        static_cast<void>(owner);
        callback();
    }
}

void ShutdownState(lua_State* state) {
    const lua_glue::detail_internal::ResolvedSession resolved =
        lua_glue::detail_internal::resolveSession(state);
    lua_State* mainState = resolved.mainState;
    const std::shared_ptr<lua_glue::detail_internal::LuaStateSession>& session =
        resolved.session;
    if (session == nullptr) {
        return;
    }
    QuiesceState(mainState);
    lua_glue::ExecutionScope execution(mainState);
    if (!execution.active()) {
        return;
    }
    lua_glue::detail_internal::LuaStatePhase expected =
        lua_glue::detail_internal::LuaStatePhase::running;
    if (!session->phase.compare_exchange_strong(
            expected, lua_glue::detail_internal::LuaStatePhase::stopping,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return;
    }
    {
        std::scoped_lock lock(session->metadataMutex);
        session->retainedObjects.clear();
        for (const int reference : session->registryReferences) {
            luaL_unref(mainState, LUA_REGISTRYINDEX, reference);
        }
        session->registryReferences.clear();
        session->state = nullptr;
        session->phase.store(lua_glue::detail_internal::LuaStatePhase::stopped,
                             std::memory_order_release);
    }
    std::scoped_lock lock(lua_glue::detail_internal::sessionsMutex);
    const auto item = lua_glue::detail_internal::sessions.find(mainState);
    if (item != lua_glue::detail_internal::sessions.end() &&
        item->second == session) {
        for (auto alias = lua_glue::detail_internal::stateAliases.begin();
             alias != lua_glue::detail_internal::stateAliases.end();) {
            if (alias->second == mainState) {
                alias = lua_glue::detail_internal::stateAliases.erase(alias);
            } else {
                ++alias;
            }
        }
        lua_glue::detail_internal::sessions.erase(item);
    }
}

}  // namespace lua_glue
