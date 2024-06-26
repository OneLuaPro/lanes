/*
 * DEEP.CPP                         Copyright (c) 2024, Benoit Germain
 *
 * Deep userdata support, separate in its own source file to help integration
 * without enforcing a Lanes dependency
 */

/*
===============================================================================

Copyright (C) 2002-10 Asko Kauppi <akauppi@gmail.com>
              2011-24 Benoit Germain <bnt.germain@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

===============================================================================
*/

#include "deep.h"

#include "compat.h"
#include "tools.h"
#include "uniquekey.h"
#include "universe.h"

#include <bit>
#include <cassert>

/*-- Metatable copying --*/

/*---=== Deep userdata ===---*/

/*
 * 'registry[REGKEY]' is a two-way lookup table for 'factory's and those type's
 * metatables:
 *
 *   metatable   ->  factory
 *   factory      ->  metatable
 */
// xxh64 of string "kDeepLookupRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kDeepLookupRegKey{ 0xC6788345703C6059ull };

/*
 * The deep proxy cache is a weak valued table listing all deep UD proxies indexed by the deep UD that they are proxying
 * xxh64 of string "kDeepProxyCacheRegKey" generated at https://www.pelock.com/products/hash-calculator
 */
static constexpr RegistryUniqueKey kDeepProxyCacheRegKey{ 0xEBCD49AE1A3DD35Eull };

/*
 * Sets up [-1]<->[-2] two-way lookups, and ensures the lookup table exists.
 * Pops the both values off the stack.
 */
void DeepFactory::storeDeepLookup(lua_State* L_) const
{
    // the deep metatable is at the top of the stack                                               // L_: mt
    STACK_GROW(L_, 3);
    STACK_CHECK_START_REL(L_, 0);                                                                  // L_: mt
    std::ignore = kDeepLookupRegKey.getSubTable(L_, 0, 0);                                         // L_: mt {}
    lua_pushvalue(L_, -2);                                                                         // L_: mt {} mt
    lua_pushlightuserdata(L_, std::bit_cast<void*>(this));                                         // L_: mt {} mt factory
    lua_rawset(L_, -3);                                                                            // L_: mt {}
    STACK_CHECK(L_, 1);

    lua_pushlightuserdata(L_, std::bit_cast<void*>(this));                                         // L_: mt {} factory
    lua_pushvalue(L_, -3);                                                                         // L_: mt {} factory mt
    lua_rawset(L_, -3);                                                                            // L_: mt {}
    STACK_CHECK(L_, 1);

    lua_pop(L_, 1);                                                                                // L_: mt
    STACK_CHECK(L_, 0);
}

// #################################################################################################

// Pops the key (metatable or factory) off the stack, and replaces with the deep lookup value (factory/metatable/nil).
static void LookupDeep(lua_State* L_)
{
    STACK_GROW(L_, 1);
    STACK_CHECK_START_REL(L_, 1);                                                                  // L_: a
    kDeepLookupRegKey.pushValue(L_);                                                               // L_: a {}
    if (!lua_isnil(L_, -1)) {
        lua_insert(L_, -2);                                                                        // L_: {} a
        lua_rawget(L_, -2);                                                                        // L_: {} b
    }
    lua_remove(L_, -2);                                                                            // L_: a|b
    STACK_CHECK(L_, 1);
}

// #################################################################################################

// Return the registered factory for 'index' (deep userdata proxy),  or nullptr if 'index' is not a deep userdata proxy.
[[nodiscard]] static inline DeepFactory* LookupFactory(lua_State* L_, int index_, LookupMode mode_)
{
    // when looking inside a keeper, we are 100% sure the object is a deep userdata
    if (mode_ == LookupMode::FromKeeper) {
        DeepPrelude* const proxy{ *lua_tofulluserdata<DeepPrelude*>(L_, index_) };
        // we can (and must) cast and fetch the internally stored factory
        return &proxy->factory;
    } else {
        // essentially we are making sure that the metatable of the object we want to copy is stored in our metatable/factory database
        // it is the only way to ensure that the userdata is indeed a deep userdata!
        // of course, we could just trust the caller, but we won't
        STACK_GROW(L_, 1);
        STACK_CHECK_START_REL(L_, 0);

        if (!lua_getmetatable(L_, index_)) {                                                       // L_: deep ... metatable?
            return nullptr; // no metatable: can't be a deep userdata object!
        }

        // replace metatable with the factory pointer, if it is actually a deep userdata
        LookupDeep(L_);                                                                            // L_: deep ... factory|nil

        DeepFactory* const ret{ lua_tolightuserdata<DeepFactory>(L_, -1) }; // nullptr if not a userdata
        lua_pop(L_, 1);
        STACK_CHECK(L_, 0);
        return ret;
    }
}

// #################################################################################################

void DeepFactory::DeleteDeepObject(lua_State* L_, DeepPrelude* o_)
{
    STACK_CHECK_START_REL(L_, 0);
    o_->factory.deleteDeepObjectInternal(L_, o_);
    STACK_CHECK(L_, 0);
}

// #################################################################################################

/*
 * void= mt.__gc( proxy_ud )
 *
 * End of life for a proxy object; reduce the deep reference count and clean it up if reaches 0.
 *
 */
[[nodiscard]] static int deep_userdata_gc(lua_State* L_)
{
    DeepPrelude* const* const proxy{ lua_tofulluserdata<DeepPrelude*>(L_, 1) };
    DeepPrelude* const p{ *proxy };

    // can work without a universe if creating a deep userdata from some external C module when Lanes isn't loaded
    // in that case, we are not multithreaded and locking isn't necessary anyway
    bool const isLastRef{ p->refcount.fetch_sub(1, std::memory_order_relaxed) == 1 };

    if (isLastRef) {
        // retrieve wrapped __gc
        lua_pushvalue(L_, lua_upvalueindex(1));                                                    // L_: self __gc?
        if (!lua_isnil(L_, -1)) {
            lua_insert(L_, -2);                                                                    // L_: __gc self
            lua_call(L_, 1, 0);                                                                    // L_:
        } else {
            // need an empty stack in case we are GC_ing from a Keeper, so that empty stack checks aren't triggered
            lua_pop(L_, 2);                                                                        // L_:
        }
        DeepFactory::DeleteDeepObject(L_, p);
    }
    return 0;
}

// #################################################################################################

// TODO: convert to UniqueKey::getSubTableMode
static void push_registry_subtable_mode(lua_State* L_, RegistryUniqueKey key_, const char* mode_)
{
    STACK_GROW(L_, 4);
    STACK_CHECK_START_REL(L_, 0);
    if (!key_.getSubTable(L_, 0, 0)) {                                                             // L_: {}
        // Set its metatable if requested
        if (mode_) {
            lua_createtable(L_, 0, 1);                                                             // L_: {} mt
            lua_pushliteral(L_, "__mode");                                                         // L_: {} mt "__mode"
            lua_pushstring(L_, mode_);                                                             // L_: {} mt "__mode" mode
            lua_rawset(L_, -3);                                                                    // L_: {} mt
            lua_setmetatable(L_, -2);                                                              // L_: {}
        }
    }
    STACK_CHECK(L_, 1);
}

// #################################################################################################

/*
 * Push a proxy userdata on the stack.
 * returns nullptr if ok, else some error string related to bad factory behavior or module require problem
 * (error cannot happen with mode_ == LookupMode::ToKeeper)
 *
 * Initializes necessary structures if it's the first time 'factory' is being
 * used in this Lua state (metatable, registring it). Otherwise, increments the
 * reference count.
 */
char const* DeepFactory::PushDeepProxy(DestState L_, DeepPrelude* prelude_, int nuv_, LookupMode mode_)
{
    // Check if a proxy already exists
    push_registry_subtable_mode(L_, kDeepProxyCacheRegKey, "v");                                   // L_: DPC
    lua_pushlightuserdata(L_, prelude_);                                                           // L_: DPC deep
    lua_rawget(L_, -2);                                                                            // L_: DPC proxy
    if (!lua_isnil(L_, -1)) {
        lua_remove(L_, -2);                                                                        // L_: proxy
        return nullptr;
    } else {
        lua_pop(L_, 1);                                                                            // L_: DPC
    }

    STACK_GROW(L_, 7);
    STACK_CHECK_START_REL(L_, 0);

    // a new full userdata, fitted with the specified number of uservalue slots (always 1 for Lua < 5.4)
    DeepPrelude** const proxy{ lua_newuserdatauv<DeepPrelude*>(L_, nuv_) };                        // L_: DPC proxy
    LUA_ASSERT(L_, proxy);
    *proxy = prelude_;
    prelude_->refcount.fetch_add(1, std::memory_order_relaxed); // one more proxy pointing to this deep data

    // Get/create metatable for 'factory' (in this state)
    DeepFactory& factory = prelude_->factory;
    lua_pushlightuserdata(L_, std::bit_cast<void*>(&factory));                                     // L_: DPC proxy factory
    LookupDeep(L_);                                                                                // L_: DPC proxy metatable|nil

    if (lua_isnil(L_, -1)) { // No metatable yet.
        lua_pop(L_, 1);                                                                            // L_: DPC proxy
        int const oldtop{ lua_gettop(L_) };
        // 1 - make one and register it
        if (mode_ != LookupMode::ToKeeper) {
            factory.createMetatable(L_);                                                           // L_: DPC proxy metatable
            if (lua_gettop(L_) - oldtop != 1 || !lua_istable(L_, -1)) {
                // factory didn't push exactly 1 value, or the value it pushed is not a table: ERROR!
                lua_settop(L_, oldtop);                                                            // L_: DPC proxy X
                lua_pop(L_, 3);                                                                    // L_:
                return "Bad DeepFactory::createMetatable overload: unexpected pushed value";
            }
            // if the metatable contains a __gc, we will call it from our own
            lua_getfield(L_, -1, "__gc");                                                          // L_: DPC proxy metatable __gc
        } else {
            // keepers need a minimal metatable that only contains our own __gc
            lua_createtable(L_, 0, 1);                                                             // L_: DPC proxy metatable
            lua_pushnil(L_);                                                                       // L_: DPC proxy metatable nil
        }
        if (lua_isnil(L_, -1)) {
            // Add our own '__gc' method
            lua_pop(L_, 1);                                                                        // L_: DPC proxy metatable
            lua_pushcfunction(L_, deep_userdata_gc);                                               // L_: DPC proxy metatable deep_userdata_gc
        } else {
            // Add our own '__gc' method wrapping the original
            lua_pushcclosure(L_, deep_userdata_gc, 1); // DPC proxy metatable deep_userdata_gc
        }
        lua_setfield(L_, -2, "__gc");                                                              // L_: DPC proxy metatable

        // Memorize for later rounds
        factory.storeDeepLookup(L_);

        // 2 - cause the target state to require the module that exported the factory
        if (char const* const modname{ factory.moduleName() }; modname) { // we actually got a module name
            // L.registry._LOADED exists without having registered the 'package' library.
            lua_getglobal(L_, "require"); // DPC proxy metatable require()
            // check that the module is already loaded (or being loaded, we are happy either way)
            if (lua_isfunction(L_, -1)) {
                lua_pushstring(L_, modname);                                                       // L_: DPC proxy metatable require() "module"
                lua_getfield(L_, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);                             // L_: DPC proxy metatable require() "module" _R._LOADED
                if (lua_istable(L_, -1)) {
                    lua_pushvalue(L_, -2);                                                         // L_: DPC proxy metatable require() "module" _R._LOADED "module"
                    lua_rawget(L_, -2);                                                            // L_: DPC proxy metatable require() "module" _R._LOADED module
                    int const alreadyloaded = lua_toboolean(L_, -1);
                    if (!alreadyloaded) { // not loaded
                        int require_result;
                        lua_pop(L_, 2);                                                            // L_: DPC proxy metatable require() "module"
                        // require "modname"
                        require_result = lua_pcall(L_, 1, 0, 0);                                   // L_: DPC proxy metatable error?
                        if (require_result != LUA_OK) {
                            // failed, return the error message
                            lua_pushfstring(L_, "error while requiring '%s' identified by DeepFactory::moduleName: ", modname);
                            lua_insert(L_, -2);                                                    // L_: DPC proxy metatable prefix error
                            lua_concat(L_, 2);                                                     // L_: DPC proxy metatable error
                            return lua_tostring(L_, -1);
                        }
                    } else { // already loaded, we are happy
                        lua_pop(L_, 4);                                                            // L_: DPC proxy metatable
                    }
                } else { // no L.registry._LOADED; can this ever happen?
                    lua_pop(L_, 6);                                                                // L_:
                    return "unexpected error while requiring a module identified by DeepFactory::moduleName";
                }
            } else { // a module name, but no require() function :-(
                lua_pop(L_, 4);                                                                    // L_:
                return "lanes receiving deep userdata should register the 'package' library";
            }
        }
    }
    STACK_CHECK(L_, 2); // DPC proxy metatable
    LUA_ASSERT(L_, lua_type_as_enum(L_, -2) == LuaType::USERDATA);
    LUA_ASSERT(L_, lua_istable(L_, -1));
    lua_setmetatable(L_, -2); // DPC proxy

    // If we're here, we obviously had to create a new proxy, so cache it.
    lua_pushlightuserdata(L_, prelude_);                                                           // L_: DPC proxy deep
    lua_pushvalue(L_, -2);                                                                         // L_: DPC proxy deep proxy
    lua_rawset(L_, -4);                                                                            // L_: DPC proxy
    lua_remove(L_, -2);                                                                            // L_: proxy
    LUA_ASSERT(L_, lua_type_as_enum(L_, -1) == LuaType::USERDATA);
    STACK_CHECK(L_, 0);
    return nullptr;
}

// #################################################################################################

/*
 * Create a deep userdata
 *
 *   proxy_ud= deep_userdata( [...] )
 *
 * Creates a deep userdata entry of the type defined by the factory.
 * Parameters found on the stack are left as is and passed on to DeepFactory::newDeepObjectInternal.
 *
 * Reference counting and true userdata proxying are taken care of for the actual data type.
 *
 * Types using the deep userdata system (and only those!) can be passed between
 * separate Lua states via 'luaG_inter_move()'.
 *
 * Returns: 'proxy' userdata for accessing the deep data via 'DeepFactory::toDeep()'
 */
int DeepFactory::pushDeepUserdata(DestState L_, int nuv_) const
{
    STACK_GROW(L_, 1);
    STACK_CHECK_START_REL(L_, 0);
    int const oldtop{ lua_gettop(L_) };
    DeepPrelude* const prelude{ newDeepObjectInternal(L_) };
    if (prelude == nullptr) {
        raise_luaL_error(L_, "DeepFactory::newDeepObjectInternal failed to create deep userdata (out of memory)");
    }

    if (prelude->magic != kDeepVersion) {
        // just in case, don't leak the newly allocated deep userdata object
        deleteDeepObjectInternal(L_, prelude);
        raise_luaL_error(L_, "Bad Deep Factory: kDeepVersion is incorrect, rebuild your implementation with the latest deep implementation");
    }

    LUA_ASSERT(L_, prelude->refcount.load(std::memory_order_relaxed) == 0); // 'DeepFactory::PushDeepProxy' will lift it to 1
    LUA_ASSERT(L_, &prelude->factory == this);

    if (lua_gettop(L_) - oldtop != 0) {
        // just in case, don't leak the newly allocated deep userdata object
        deleteDeepObjectInternal(L_, prelude);
        raise_luaL_error(L_, "Bad DeepFactory::newDeepObjectInternal overload: should not push anything on the stack");
    }

    char const* const errmsg{ DeepFactory::PushDeepProxy(L_, prelude, nuv_, LookupMode::LaneBody) }; // proxy
    if (errmsg != nullptr) {
        raise_luaL_error(L_, errmsg);
    }
    STACK_CHECK(L_, 1);
    return 1;
}

// #################################################################################################

/*
 * Access deep userdata through a proxy.
 *
 * Reference count is not changed, and access to the deep userdata is not
 * serialized. It is the module's responsibility to prevent conflicting usage.
 */
DeepPrelude* DeepFactory::toDeep(lua_State* L_, int index_) const
{
    STACK_CHECK_START_REL(L_, 0);
    // ensure it is actually a deep userdata we created
    if (LookupFactory(L_, index_, LookupMode::LaneBody) != this) {
        return nullptr; // no metatable, or wrong kind
    }
    STACK_CHECK(L_, 0);

    DeepPrelude** const proxy{ lua_tofulluserdata<DeepPrelude*>(L_, index_) };
    return *proxy;
}

// #################################################################################################

// Copy deep userdata between two separate Lua states (from L1 to L2)
// Returns false if not a deep userdata, else true (unless an error occured)
[[nodiscard]] bool InterCopyContext::tryCopyDeep() const
{
    DeepFactory* const factory{ LookupFactory(L1, L1_i, mode) };
    if (factory == nullptr) {
        return false; // not a deep userdata
    }

    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);

    // extract all uservalues of the source. unfortunately, the only way to know their count is to iterate until we fail
    int nuv = 0;
    while (lua_getiuservalue(L1, L1_i, nuv + 1) != LUA_TNONE) {                                    // L1: ... u [uv]* nil
        ++nuv;
    }
    // last call returned TNONE and pushed nil, that we don't need
    lua_pop(L1, 1);                                                                                // L1: ... u [uv]*
    STACK_CHECK(L1, nuv);

    DeepPrelude* const u{ *lua_tofulluserdata<DeepPrelude*>(L1, L1_i) };
    char const* errmsg{ DeepFactory::PushDeepProxy(L2, u, nuv, mode) };                            // L1: ... u [uv]*                               L2: u
    if (errmsg != nullptr) {
        raise_luaL_error(getErrL(), errmsg);
    }

    // transfer all uservalues of the source in the destination
    {
        InterCopyContext c{ U, L2, L1, L2_cache_i, {}, VT::NORMAL, mode, name };
        int const clone_i{ lua_gettop(L2) };
        while (nuv) {
            c.L1_i = SourceIndex{ lua_absindex(L1, -1) };
            if (!c.inter_copy_one()) {                                                             // L1: ... u [uv]*                               L2: u uv
                raise_luaL_error(getErrL(), "Cannot copy upvalue type '%s'", luaL_typename(L1, -1));
            }
            lua_pop(L1, 1);                                                                        // L1: ... u [uv]*
            // this pops the value from the stack
            lua_setiuservalue(L2, clone_i, nuv);                                                   //                                               L2: u
            --nuv;
        }
    }

    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);

    return true;
}