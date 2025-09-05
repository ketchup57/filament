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
#ifndef TNT_FILAMENT_MATERIALDEFINITION_H
#define TNT_FILAMENT_MATERIALDEFINITION_H

#include <filament/Material.h>

#include <private/filament/Variant.h>
#include <private/filament/BufferInterfaceBlock.h>
#include <private/filament/SamplerInterfaceBlock.h>
#include <private/filament/SubpassInfo.h>
#include <private/filament/ConstantInfo.h>

#include <ds/DescriptorSetLayout.h>

#include <backend/Program.h>

#include <optional>

namespace filament {

class FEngine;
class MaterialParser;

/* A MaterialDefinition is a parsed, unmarshalled material file which can be specialized. */
class MaterialDefinition {
public:
    using BlendingMode = filament::BlendingMode;
    using Shading = filament::Shading;
    using Interpolation = filament::Interpolation;
    using VertexDomain = filament::VertexDomain;
    using TransparencyMode = filament::TransparencyMode;

    using ParameterType = backend::UniformType;
    using Precision = backend::Precision;
    using SamplerType = backend::SamplerType;
    using SamplerFormat = backend::SamplerFormat;
    using CullingMode = backend::CullingMode;
    using ShaderModel = backend::ShaderModel;
    using SubpassType = backend::SubpassType;

    using ParameterInfo = filament::Material::ParameterInfo;

    using AttributeInfoContainer = utils::FixedCapacityVector<std::pair<utils::CString, uint8_t>>;

    using BindingUniformInfoContainer = utils::FixedCapacityVector<
        std::tuple<uint8_t, utils::CString, backend::Program::UniformInfo>>;

    // Called by MaterialCache.
    static std::unique_ptr<MaterialParser> createParser(backend::Backend const backend,
            utils::FixedCapacityVector<backend::ShaderLanguage> languages,
            const void* UTILS_NONNULL data, size_t size);

    // Called by MaterialCache.
    static std::optional<MaterialDefinition> create(FEngine& engine,
            const void* UTILS_NONNULL payload, size_t size, std::unique_ptr<MaterialParser> parser);

    // return the uniform interface block for this material
    const BufferInterfaceBlock& getUniformInterfaceBlock() const noexcept {
        return mUniformInterfaceBlock;
    }

    DescriptorSetLayout const& getPerViewDescriptorSetLayout() const noexcept {
        assert_invariant(mMaterialDomain == MaterialDomain::POST_PROCESS);
        return mPerViewDescriptorSetLayout;
    }

    DescriptorSetLayout const& getDescriptorSetLayout() const noexcept {
        return mDescriptorSetLayout;
    }

    bool hasParameter(const char* UTILS_NONNULL name) const noexcept;

    bool isSampler(const char* UTILS_NONNULL name) const noexcept;

    BufferInterfaceBlock::FieldInfo const* UTILS_NONNULL reflect(
            std::string_view name) const noexcept ;

    bool isVariantLit() const noexcept { return mIsVariantLit; }

    const utils::CString& getName() const noexcept { return mName; }
    backend::FeatureLevel getFeatureLevel() const noexcept { return mFeatureLevel; }
    backend::RasterState getRasterState() const noexcept  { return mRasterState; }

    UserVariantFilterMask getSupportedVariants() const noexcept {
        return UserVariantFilterMask(UserVariantFilterBit::ALL) & ~mVariantFilterMask;
    }

    Shading getShading() const noexcept { return mShading; }
    Interpolation getInterpolation() const noexcept { return mInterpolation; }
    BlendingMode getBlendingMode() const noexcept { return mBlendingMode; }
    VertexDomain getVertexDomain() const noexcept { return mVertexDomain; }
    MaterialDomain getMaterialDomain() const noexcept { return mMaterialDomain; }
    CullingMode getCullingMode() const noexcept { return mCullingMode; }
    TransparencyMode getTransparencyMode() const noexcept { return mTransparencyMode; }
    bool isColorWriteEnabled() const noexcept { return mRasterState.colorWrite; }
    bool isDepthWriteEnabled() const noexcept { return mRasterState.depthWrite; }
    bool isDepthCullingEnabled() const noexcept {
        return mRasterState.depthFunc != backend::RasterState::DepthFunc::A;
    }
    bool isDoubleSided() const noexcept { return mDoubleSided; }
    bool hasDoubleSidedCapability() const noexcept { return mDoubleSidedCapability; }
    bool isAlphaToCoverageEnabled() const noexcept { return mRasterState.alphaToCoverage; }
    float getMaskThreshold() const noexcept { return mMaskThreshold; }
    bool hasShadowMultiplier() const noexcept { return mHasShadowMultiplier; }
    AttributeBitset getRequiredAttributes() const noexcept { return mRequiredAttributes; }
    RefractionMode getRefractionMode() const noexcept { return mRefractionMode; }
    RefractionType getRefractionType() const noexcept { return mRefractionType; }
    ReflectionMode getReflectionMode() const noexcept { return mReflectionMode; }
    bool hasCustomDepthShader() const noexcept { return mHasCustomDepthShader; }
    bool hasSpecularAntiAliasing() const noexcept { return mSpecularAntiAliasing; }
    float getSpecularAntiAliasingVariance() const noexcept { return mSpecularAntiAliasingVariance; }
    float getSpecularAntiAliasingThreshold() const noexcept { return mSpecularAntiAliasingThreshold; }

    backend::descriptor_binding_t getSamplerBinding(
            std::string_view const& name) const;

    bool hasMaterialProperty(Property property) const noexcept {
        return bool(mMaterialProperties & uint64_t(property));
    }

    SamplerInterfaceBlock const& getSamplerInterfaceBlock() const noexcept {
        return mSamplerInterfaceBlock;
    }

    size_t getParameterCount() const noexcept {
        return mUniformInterfaceBlock.getFieldInfoList().size() +
               mSamplerInterfaceBlock.getSamplerInfoList().size() +
               (mSubpassInfo.isValid ? 1 : 0);
    }

    size_t getParameters(ParameterInfo* UTILS_NONNULL parameters, size_t count) const noexcept;

    // return the id of a specialization constant specified by name for this material
    std::optional<uint32_t> getSpecializationConstantId(std::string_view name) const noexcept;

    uint8_t getPerViewLayoutIndex() const noexcept { return mPerViewLayoutIndex; }

private:
    MaterialDefinition(FEngine& engine, std::unique_ptr<MaterialParser> parser);

    void processBlendingMode(MaterialParser const* UTILS_NONNULL parser);
    void processSpecializationConstants(MaterialParser const* UTILS_NONNULL parser);
    void processDescriptorSets(FEngine& engine, MaterialParser const* UTILS_NONNULL parser);

    // try to order by frequency of use
    DescriptorSetLayout mPerViewDescriptorSetLayout;
    DescriptorSetLayout mPerViewDescriptorSetLayoutVsm;
    DescriptorSetLayout mDescriptorSetLayout;
    backend::Program::DescriptorSetInfo mProgramDescriptorBindings;

    backend::RasterState mRasterState;
    TransparencyMode mTransparencyMode = TransparencyMode::DEFAULT;
    bool mIsVariantLit = false;
    backend::FeatureLevel mFeatureLevel = backend::FeatureLevel::FEATURE_LEVEL_1;
    Shading mShading = Shading::UNLIT;

    BlendingMode mBlendingMode = BlendingMode::OPAQUE;
    std::array<backend::BlendFunction, 4> mCustomBlendFunctions = {};
    Interpolation mInterpolation = Interpolation::SMOOTH;
    VertexDomain mVertexDomain = VertexDomain::OBJECT;
    MaterialDomain mMaterialDomain = MaterialDomain::SURFACE;
    CullingMode mCullingMode = CullingMode::NONE;
    AttributeBitset mRequiredAttributes;
    UserVariantFilterMask mVariantFilterMask = 0;
    RefractionMode mRefractionMode = RefractionMode::NONE;
    RefractionType mRefractionType = RefractionType::SOLID;
    ReflectionMode mReflectionMode = ReflectionMode::DEFAULT;
    uint64_t mMaterialProperties = 0;
    uint8_t mPerViewLayoutIndex = 0;

    float mMaskThreshold = 0.4f;
    float mSpecularAntiAliasingVariance = 0.0f;
    float mSpecularAntiAliasingThreshold = 0.0f;

    bool mDoubleSided = false;
    bool mDoubleSidedCapability = false;
    bool mHasShadowMultiplier = false;
    bool mHasCustomDepthShader = false;
    bool mSpecularAntiAliasing = false;

    SamplerInterfaceBlock mSamplerInterfaceBlock;
    BufferInterfaceBlock mUniformInterfaceBlock;
    SubpassInfo mSubpassInfo;

    BindingUniformInfoContainer mBindingUniformInfo;
    AttributeInfoContainer mAttributeInfo;

    // Constants defined by this Material
    utils::FixedCapacityVector<MaterialConstant> mMaterialConstants;
    // A map from the Constant name to the mMaterialConstant index
    std::unordered_map<std::string_view, uint32_t> mSpecializationConstantsNameToIndex;

    utils::CString mName;
    uint64_t mCacheId = 0;

    // TODO: shared_ptr so that we can copy this struct from the MaterialCache to Material objects.
    // We may eventually want to only rely on MaterialCache::DefinitionHandle.
    std::shared_ptr<MaterialParser> mMaterialParser;
};

} // namespace filament

#endif  // TNT_FILAMENT_MATERIALDEFINITION_H
