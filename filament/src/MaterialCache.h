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
#ifndef TNT_FILAMENT_MATERIALCACHE_H
#define TNT_FILAMENT_MATERIALCACHE_H

#include <Cache.h>
#include <MaterialDefinition.h>

#include <backend/Handle.h>
#include <backend/Program.h>

namespace filament {

class FEngine;

/*
 * A cache of all materials and shader programs compiled by Filament.
 */
class MaterialCache {
public:
    // Definitions are keyed by the cache ID of the material file.
    using DefinitionCache = Cache<uint64_t, MaterialDefinition>;
    using DefinitionHandle = DefinitionCache::Handle;

    /*
     * A Specialization contains all specialization options which would result in a different
     * Program. In other words, a Program is a pure function of the fields of Specialization.
     */
    struct Specialization {
        DefinitionHandle definition;
        Variant variant;
        utils::FixedCapacityVector<backend::Program::SpecializationConstant>
        specializationConstants;
        std::array<utils::FixedCapacityVector<backend::Program::PushConstant>,
                backend::Program::SHADER_TYPE_COUNT>
                pushConstants;

        bool operator==(const Specialization& rhs) const noexcept;

        struct Hash {
            size_t operator()(const filament::MaterialCache::Specialization& value) const noexcept;
        };
    };

    using Program = backend::Handle<backend::HwProgram>;
    // Programs are keyed by the specialization options of the program.
    using ProgramCache = Cache<Specialization, Program, Specialization::Hash>;
    using ProgramHandle = ProgramCache::Handle;

    DefinitionCache::ReturnValue getDefinition(
            const void* UTILS_NONNULL payload, size_t size) noexcept;

    ProgramCache::ReturnValue getProgram(Specialization const& specialization,
            backend::CompilerPriorityQueue const priorityQueue) noexcept;

    MaterialCache(FEngine& engine);

private:
    FEngine& mEngine;

    DefinitionCache mDefinitionCache;
    ProgramCache mProgramCache;
};

} // namespace filament


#endif  // TNT_FILAMENT_MATERIALCACHE_H
