/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef TNT_FILAMENT_CACHE_H
#define TNT_FILAMENT_CACHE_H

#include <utils/Panic.h>
#include <utils/compiler.h>

#include <tsl/robin_map.h>

#include <optional>

namespace filament {

/* A reference-counted cache. */
template<typename Key, typename T, typename Hash = std::hash<Key>>
class Cache {
public:
    class Handle;
    using ReturnValue = std::optional<std::pair<Cache::Handle, T&>>;

private:
    // Use references for the key if the size of the key type is greater than the size of a pointer.
    using KR = std::conditional_t<(sizeof(Key) > sizeof(void*)), const Key&, Key>;

    // An entry in the cache.
    struct Entry {
        uint32_t referenceCount;
        T value;
    };

    // Class containing the actual implementation. This is allocated on the heap since Handle
    // requires stable pointers.
    class Impl {
    public:
        using Map = tsl::robin_map<Key, Entry, Hash>;

        ~Impl();

        template<typename F>
        inline ReturnValue get(KR key, F factory) noexcept;

        inline Map::iterator at(KR key, size_t hash) noexcept;
        inline void acquire(KR key, size_t hash) noexcept;
        inline void release(KR key, size_t hash) noexcept;

    private:
        Map mMap;
    };

public:
    /* A handle which manages acquisition and release of a resource in Cache. */
    class Handle {
        friend class Cache;
    public:
        Handle(Handle&& rhs) noexcept
            : mImpl(rhs.mImpl),
              mKey(rhs.mKey),
              mHash(rhs.mHash) {
            rhs.mImpl = nullptr;
        }

        Handle& operator=(Handle&& rhs) noexcept {
            mImpl = rhs.mImpl;
            mKey = rhs.mKey;
            mHash = rhs.mHash;
            rhs.mImpl = nullptr;
            return *this;
        }

        Handle(const Handle& rhs) noexcept
            : mImpl(rhs.mImpl),
              mKey(rhs.mKey),
              mHash(rhs.mHash) {
            if (mImpl) {
                mImpl->acquire(mKey, mHash);
            }
        }

        Handle& operator=(const Handle& rhs) noexcept {
            if (mImpl) {
                mImpl->release(mKey, mHash);
            }
            if (rhs.mImpl) {
                rhs.mImpl->acquire(rhs.mKey, rhs.mHash);
            }

            mImpl = rhs.mImpl;
            mKey = rhs.mKey;
            mHash = rhs.mHash;

            return *this;
        }

        ~Handle() noexcept {
            if (mImpl) {
                mImpl->release(mKey, mHash);
            }
        }

        T& operator*() noexcept { return mImpl->at(mKey, mHash).value().value; }
        const T& operator*() const noexcept { return mImpl->at(mKey, mHash)->second.value; }

        bool operator==(const Handle& rhs) const noexcept {
            return mImpl == rhs.mImpl && mKey == rhs.mKey;
        }

        KR getKey() const noexcept { return mKey; }
        size_t getHash() const noexcept { return mHash; }

    private:
        Handle(Impl* UTILS_NONNULL impl, Key key, size_t hash)
            : mImpl(impl),
              mKey(std::move(key)),
              mHash(hash) {}

        Impl* UTILS_NULLABLE mImpl; // null if moved
        Key mKey;
        size_t mHash;
    };

    template<typename F>
    inline ReturnValue get(KR key, F factory) noexcept {
        return mImpl->get(key, std::move(factory));
    }

    Cache() : mImpl(std::make_unique<Impl>()) {}

private:
    std::unique_ptr<Impl> mImpl;
};

template<typename Key, typename T, typename Hash>
Cache<Key, T, Hash>::Impl::~Impl() {
    FILAMENT_CHECK_PRECONDITION(mMap.empty())
            << "Freed a non-empty Cache";
}

template<typename Key, typename T, typename Hash>
template<typename F>
Cache<Key, T, Hash>::ReturnValue Cache<Key, T, Hash>::Impl::get(
        KR key, F factory) noexcept {
    size_t hash = Hash{}(key);
    auto it = mMap.find(key, hash);
    if (it != mMap.end()) {
        it.value().referenceCount++;
        return ReturnValue({Handle(this, key, hash), it.value().value});
    }
    std::optional<T> value = factory();
    if (value) {
        // TODO: how to use above computed hash here?
        T& valueRef = mMap.insert({key, Entry{
                .referenceCount = 1,
                .value = std::move(*value),
            }}).first.value().value;
        return ReturnValue({Handle(this, key, hash), valueRef});
    }
    return std::nullopt;
}

template<typename Key, typename T, typename Hash>
inline Cache<Key, T, Hash>::Impl::Map::iterator Cache<Key, T, Hash>::Impl::at(
        Cache<Key, T, Hash>::KR key, size_t hash) noexcept {
    auto it = mMap.find(key, hash);
    FILAMENT_CHECK_PRECONDITION(it != mMap.end())
            << "Cache is somehow missing entry";
    return it;
}

template <typename Key, typename T, typename Hash>
inline void Cache<Key, T, Hash>::Impl::acquire(KR key, size_t hash) noexcept {
    at(key, hash).value().referenceCount++;
}

template <typename Key, typename T, typename Hash>
inline void Cache<Key, T, Hash>::Impl::release(KR key, size_t hash) noexcept {
    auto it = at(key, hash);
    if (--it.value().referenceCount == 0) {
        // TODO: change to erase_fast
        mMap.erase(it);
    }
}

}

#endif  // TNT_FILAMENT_CACHE_H
