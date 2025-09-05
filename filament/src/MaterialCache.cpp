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

#include <MaterialCache.h>
#include <MaterialParser.h>

#include <backend/DriverEnums.h>

#include <details/Engine.h>

#include <utils/Hash.h>
#include <utils/Logger.h>

namespace filament {

size_t MaterialCache::Specialization::Hash::operator()(
        MaterialCache::Specialization const& value) const noexcept {
    size_t seed = 0;
    utils::hash::combine_fast(seed, value.definition.getHash());
    utils::hash::combine_fast(seed, value.variant.key);
    for (auto const& it: value.specializationConstants) {
        utils::hash::combine_fast(seed, it.id);
        utils::hash::combine_fast(seed, it.value);
    }
    for (auto const& set: value.pushConstants) {
        for (auto const& it: set) {
            utils::hash::combine_fast(seed, utils::CString::Hasher{}(it.name));
            utils::hash::combine_fast(seed, it.type);
        }
    }
    return seed;
}

bool MaterialCache::Specialization::operator==(
        MaterialCache::Specialization const& rhs) const noexcept {
    return definition == rhs.definition && variant == rhs.variant &&
            specializationConstants == rhs.specializationConstants &&
            pushConstants == rhs.pushConstants;
}

MaterialCache::MaterialCache(FEngine& engine) : mEngine(engine) {}

MaterialCache::DefinitionCache::ReturnValue MaterialCache::getDefinition(
        const void* UTILS_NONNULL payload, size_t size) noexcept {
    std::unique_ptr<MaterialParser> parser = MaterialDefinition::createParser(
            downcast(mEngine).getBackend(), downcast(mEngine).getShaderLanguage(),
            payload, size);

    if (!parser) {
        return std::nullopt;
    }

    uint64_t cacheId;
    if (!parser->getCacheId(&cacheId)) {
        return std::nullopt;
    }

    return mDefinitionCache.get(cacheId,
            [this, payload, size, parser = std::move(parser)]() mutable {
                return MaterialDefinition::create(mEngine, payload, size, std::move(parser));
            });
}

MaterialCache::ProgramCache::ReturnValue
MaterialCache::getProgram(Specialization const& specialization,
        backend::CompilerPriorityQueue const priorityQueue) noexcept {
    return mProgramCache.get(specialization, []() {
        return Program{};
    });
}

} // namespace filament
