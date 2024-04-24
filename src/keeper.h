#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
#include "lua.h"
#ifdef __cplusplus
}
#endif // __cplusplus

#include "threading.h"
#include "uniquekey.h"

#include <optional>
#include <mutex>

// forwards
enum class LookupMode;
class Universe;

using KeeperState = Unique<lua_State*>;

struct Keeper
{
    std::mutex m_mutex;
    KeeperState L{ nullptr };
    // int count;
};

struct Keepers
{
    int gc_threshold{ 0 };
    int nb_keepers{ 0 };
    Keeper keeper_array[1];
};

static constexpr uintptr_t KEEPER_MAGIC_SHIFT{ 3 };
// crc64/we of string "NIL_SENTINEL" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey NIL_SENTINEL{ 0x7EAAFA003A1D11A1ull, "linda.null" };

void init_keepers(Universe* U, lua_State* L);
void close_keepers(Universe* U);

[[nodiscard]] Keeper* which_keeper(Keepers* keepers_, uintptr_t magic_);
[[nodiscard]] Keeper* keeper_acquire(Keepers* keepers_, uintptr_t magic_);
void keeper_release(Keeper* K_);
void keeper_toggle_nil_sentinels(lua_State* L, int val_i_, LookupMode const mode_);
[[nodiscard]] int keeper_push_linda_storage(Universe* U, DestState L, void* ptr_, uintptr_t magic_);

using keeper_api_t = lua_CFunction;
#define KEEPER_API(_op) keepercall_##_op
#define PUSH_KEEPER_FUNC lua_pushcfunction
// lua_Cfunctions to run inside a keeper state
[[nodiscard]] int keepercall_clear(lua_State* L);
[[nodiscard]] int keepercall_send(lua_State* L);
[[nodiscard]] int keepercall_receive(lua_State* L);
[[nodiscard]] int keepercall_receive_batched(lua_State* L);
[[nodiscard]] int keepercall_limit(lua_State* L);
[[nodiscard]] int keepercall_get(lua_State* L);
[[nodiscard]] int keepercall_set(lua_State* L);
[[nodiscard]] int keepercall_count(lua_State* L);

using KeeperCallResult = Unique<std::optional<int>>;
[[nodiscard]] KeeperCallResult keeper_call(Universe* U, KeeperState K, keeper_api_t _func, lua_State* L, void* linda, int starting_index);
