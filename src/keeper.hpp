#pragma once

#include "uniquekey.hpp"

// forwards
class Linda;
enum class LookupMode;
class Universe;

DECLARE_UNIQUE_TYPE(KeeperState,lua_State*);
DECLARE_UNIQUE_TYPE(LindaLimit, int);
DECLARE_UNIQUE_TYPE(KeeperIndex, int);

// #################################################################################################

enum class LindaRestrict
{
    None,
    SetGet,
    SendReceive
};

// #################################################################################################

struct Keeper
{
    std::mutex mutex;
    KeeperState K{ static_cast<lua_State*>(nullptr) };

    [[nodiscard]]
    static void* operator new[](size_t size_, Universe* U_) noexcept;
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete[](void* p_, Universe* U_);


    ~Keeper() = default;
    Keeper() = default;
    // non-copyable, non-movable
    Keeper(Keeper const&) = delete;
    Keeper(Keeper const&&) = delete;
    Keeper& operator=(Keeper const&) = delete;
    Keeper& operator=(Keeper const&&) = delete;

    [[nodiscard]]
    static int PushLindaStorage(Linda& linda_, DestState L_);
};

// #################################################################################################

struct Keepers
{
    private:
    struct DeleteKV
    {
        Universe* U{};
        size_t count{};
        void operator()(Keeper* k_) const;
    };
    // can't use std::vector<Keeper> because Keeper contains a mutex, so we need a raw memory buffer
    struct KV
    {
        std::unique_ptr<Keeper[], DeleteKV> keepers;
        size_t nbKeepers{};
    };
    std::variant<std::monostate, Keeper, KV> keeper_array;
    std::atomic_flag isClosing;

    public:
    int gc_threshold{ 0 };

    public:
    // can only be instanced as a data member
    static void* operator new(size_t size_) = delete;

    Keepers() = default;
    void close();
    [[nodiscard]]
    Keeper* getKeeper(KeeperIndex idx_);
    [[nodiscard]]
    int getNbKeepers() const;
    void initialize(Universe& U_, lua_State* L_, size_t nbKeepers_, int gc_threshold_);
};

// #################################################################################################

DECLARE_UNIQUE_TYPE(KeeperCallResult, std::optional<int>);

// xxh64 of string "kNilSentinel" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kNilSentinel{ 0xC457D4EDDB05B5E4ull, "lanes.null" };

// xxh64 of string "kRestrictedChannel" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kRestrictedChannel{ 0x4C8B879ECDE110F7ull };

using keeper_api_t = lua_CFunction;
#define KEEPER_API(_op) keepercall_##_op

// lua_Cfunctions to run inside a keeper state
[[nodiscard]]
int keepercall_count(lua_State* L_);
[[nodiscard]]
int keepercall_destruct(lua_State* L_);
[[nodiscard]]
int keepercall_get(lua_State* L_);
[[nodiscard]]
int keepercall_limit(lua_State* L_);
[[nodiscard]]
int keepercall_receive(lua_State* L_);
[[nodiscard]]
int keepercall_receive_batched(lua_State* L_);
[[nodiscard]]
int keepercall_restrict(lua_State* L_);
[[nodiscard]]
int keepercall_send(lua_State* L_);
[[nodiscard]]
int keepercall_set(lua_State* L_);

[[nodiscard]]
KeeperCallResult keeper_call(KeeperState K_, keeper_api_t func_, lua_State* L_, Linda* linda_, StackIndex starting_index_);
