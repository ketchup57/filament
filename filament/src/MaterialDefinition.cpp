#include "MaterialDefinition.h"

#include "Froxelizer.h"
#include "MaterialParser.h"

#include <ds/ColorPassDescriptorSet.h>

#include <details/Engine.h>

#include <utils/Hash.h>
#include <utils/Logger.h>
#include <utils/Panic.h>

namespace filament {

using namespace backend;
using namespace utils;

namespace {

static const char* toString(ShaderModel model) {
    switch (model) {
        case ShaderModel::MOBILE:
            return "mobile";
        case ShaderModel::DESKTOP:
            return "desktop";
    }
}

} // namespace

std::unique_ptr<MaterialParser> MaterialDefinition::createParser(Backend const backend,
        FixedCapacityVector<ShaderLanguage> languages, const void* data, size_t size) {
    // unique_ptr so we don't leak MaterialParser on failures below
    auto materialParser = std::make_unique<MaterialParser>(languages, data, size);

    MaterialParser::ParseResult const materialResult = materialParser->parse();

    if (UTILS_UNLIKELY(materialResult == MaterialParser::ParseResult::ERROR_MISSING_BACKEND)) {
        CString languageNames;
        for (auto it = languages.begin(); it != languages.end(); ++it) {
            languageNames.append(CString{shaderLanguageToString(*it)});
            if (std::next(it) != languages.end()) {
                languageNames.append(", ");
            }
        }

        FILAMENT_CHECK_POSTCONDITION(
                materialResult != MaterialParser::ParseResult::ERROR_MISSING_BACKEND)
                << "the material was not built for any of the " << to_string(backend)
                << " backend's supported shader languages (" << languageNames.c_str() << ")\n";
    }

    if (backend == Backend::NOOP) {
        return materialParser;
    }

    FILAMENT_CHECK_POSTCONDITION(materialResult == MaterialParser::ParseResult::SUCCESS)
            << "could not parse the material package";

    uint32_t version = 0;
    materialParser->getMaterialVersion(&version);
    FILAMENT_CHECK_POSTCONDITION(version == MATERIAL_VERSION)
            << "Material version mismatch. Expected " << MATERIAL_VERSION << " but received "
            << version << ".";

    assert_invariant(backend != Backend::DEFAULT && "Default backend has not been resolved.");

    return materialParser;
}

std::optional<MaterialDefinition> MaterialDefinition::create(FEngine& engine,
        const void* UTILS_NONNULL payload, size_t size, std::unique_ptr<MaterialParser> parser) {
    // Try checking CRC32 value for the package and skip if it's unavailable.
    if (downcast(engine).features.material.check_crc32_after_loading) {
        uint32_t parsedCrc32 = 0;
        parser->getMaterialCrc32(&parsedCrc32);

        constexpr size_t crc32ChunkSize = sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t);
        const size_t originalSize = size - crc32ChunkSize;
        assert_invariant(size > crc32ChunkSize);

        std::vector<uint32_t> crc32Table;
        hash::crc32GenerateTable(crc32Table);
        uint32_t expectedCrc32 = hash::crc32Update(0, payload, originalSize, crc32Table);
        if (parsedCrc32 != expectedCrc32) {
            CString name;
            parser->getName(&name);
            LOG(ERROR) << "The material '" << name.c_str_safe()
                       << "' is corrupted: crc32_expected=" << expectedCrc32
                       << ", crc32_parsed=" << parsedCrc32;
            return std::nullopt;
        }
    }

    uint32_t v = 0;
    parser->getShaderModels(&v);
    bitset32 shaderModels;
    shaderModels.setValue(v);

    ShaderModel const shaderModel = downcast(engine).getShaderModel();
    if (!shaderModels.test(static_cast<uint32_t>(shaderModel))) {
        CString name;
        parser->getName(&name);
        char shaderModelsString[16];
        snprintf(shaderModelsString, sizeof(shaderModelsString), "%#x", shaderModels.getValue());
        LOG(ERROR) << "The material '" << name.c_str_safe() << "' was not built for "
                   << toString(shaderModel) << ".";
        LOG(ERROR) << "Compiled material contains shader models " << shaderModelsString << ".";
        return std::nullopt;
    }

    // Print a warning if the material's stereo type doesn't align with the engine's
    // setting.
    MaterialDomain materialDomain;
    UserVariantFilterMask variantFilterMask;
    parser->getMaterialDomain(&materialDomain);
    parser->getMaterialVariantFilterMask(&variantFilterMask);
    bool const hasStereoVariants =
            !(variantFilterMask & UserVariantFilterMask(UserVariantFilterBit::STE));
    if (materialDomain == MaterialDomain::SURFACE && hasStereoVariants) {
        StereoscopicType const engineStereoscopicType = engine.getConfig().stereoscopicType;
        // Default materials are always compiled with either 'instanced' or 'multiview'.
        // So, we only verify compatibility if the engine is set up for stereo.
        if (engineStereoscopicType != StereoscopicType::NONE) {
            StereoscopicType materialStereoscopicType = StereoscopicType::NONE;
            parser->getStereoscopicType(&materialStereoscopicType);
            if (materialStereoscopicType != engineStereoscopicType) {
                CString name;
                parser->getName(&name);
                LOG(WARNING) << "The stereoscopic type in the compiled material '"
                             << name.c_str_safe() << "' is " << (int) materialStereoscopicType
                             << ", which is not compatible with the engine's setting "
                             << (int) engineStereoscopicType << ".";
            }
        }
    }

    return MaterialDefinition(engine, std::move(parser));
}

MaterialDefinition::MaterialDefinition(FEngine& engine,
        std::unique_ptr<MaterialParser> materialParser)
        : mMaterialParser(std::move(materialParser)) {
    MaterialParser* const parser = mMaterialParser.get();

    UTILS_UNUSED_IN_RELEASE bool const nameOk = parser->getName(&mName);
    assert_invariant(nameOk);

    mFeatureLevel = [parser]() -> FeatureLevel {
        // code written this way so the IDE will complain when/if we add a FeatureLevel
        uint8_t level = 1;
        parser->getFeatureLevel(&level);
        assert_invariant(level <= 3);
        FeatureLevel featureLevel = FeatureLevel::FEATURE_LEVEL_1;
        switch (FeatureLevel(level)) {
            case FeatureLevel::FEATURE_LEVEL_0:
            case FeatureLevel::FEATURE_LEVEL_1:
            case FeatureLevel::FEATURE_LEVEL_2:
            case FeatureLevel::FEATURE_LEVEL_3:
                featureLevel = FeatureLevel(level);
                break;
        }
        return featureLevel;
    }();

    UTILS_UNUSED_IN_RELEASE bool success;

    success = parser->getCacheId(&mCacheId);
    assert_invariant(success);

    success = parser->getSIB(&mSamplerInterfaceBlock);
    assert_invariant(success);

    success = parser->getUIB(&mUniformInterfaceBlock);
    assert_invariant(success);

    if (UTILS_UNLIKELY(parser->getShaderLanguage() == ShaderLanguage::ESSL1)) {
        success = parser->getAttributeInfo(&mAttributeInfo);
        assert_invariant(success);

        success = parser->getBindingUniformInfo(&mBindingUniformInfo);
        assert_invariant(success);
    }

    // Older materials will not have a subpass chunk; this should not be an error.
    if (!parser->getSubpasses(&mSubpassInfo)) {
        mSubpassInfo.isValid = false;
    }

    parser->getShading(&mShading);
    parser->getMaterialProperties(&mMaterialProperties);
    parser->getInterpolation(&mInterpolation);
    parser->getVertexDomain(&mVertexDomain);
    parser->getMaterialDomain(&mMaterialDomain);
    parser->getMaterialVariantFilterMask(&mVariantFilterMask);
    parser->getRequiredAttributes(&mRequiredAttributes);
    parser->getRefractionMode(&mRefractionMode);
    parser->getRefractionType(&mRefractionType);
    parser->getReflectionMode(&mReflectionMode);
    parser->getTransparencyMode(&mTransparencyMode);
    parser->getDoubleSided(&mDoubleSided);
    parser->getCullingMode(&mCullingMode);

    if (mShading == Shading::UNLIT) {
        parser->hasShadowMultiplier(&mHasShadowMultiplier);
    }

    mIsVariantLit = mShading != Shading::UNLIT || mHasShadowMultiplier;

    // color write
    bool colorWrite = false;
    parser->getColorWrite(&colorWrite);
    mRasterState.colorWrite = colorWrite;

    // depth test
    bool depthTest = false;
    parser->getDepthTest(&depthTest);
    mRasterState.depthFunc = depthTest ? RasterState::DepthFunc::GE : RasterState::DepthFunc::A;

    // if doubleSided() was called we override culling()
    bool doubleSideSet = false;
    parser->getDoubleSidedSet(&doubleSideSet);
    if (doubleSideSet) {
        mDoubleSidedCapability = true;
        mRasterState.culling = mDoubleSided ? CullingMode::NONE : mCullingMode;
    } else {
        mRasterState.culling = mCullingMode;
    }

    // specular anti-aliasing
    parser->hasSpecularAntiAliasing(&mSpecularAntiAliasing);
    if (mSpecularAntiAliasing) {
        parser->getSpecularAntiAliasingVariance(&mSpecularAntiAliasingVariance);
        parser->getSpecularAntiAliasingThreshold(&mSpecularAntiAliasingThreshold);
    }

    parser->hasCustomDepthShader(&mHasCustomDepthShader);

    bool const isLit = mIsVariantLit || mHasShadowMultiplier;
    bool const isSSR = mReflectionMode == ReflectionMode::SCREEN_SPACE ||
            mRefractionMode == RefractionMode::SCREEN_SPACE;
    bool const hasFog = !(mVariantFilterMask & UserVariantFilterMask(UserVariantFilterBit::FOG));

    mPerViewLayoutIndex = ColorPassDescriptorSet::getIndex(isLit, isSSR, hasFog);

    processBlendingMode(parser);
    processSpecializationConstants(parser);
    processDescriptorSets(engine, parser);
}

void MaterialDefinition::processBlendingMode(MaterialParser const* parser) {
    parser->getBlendingMode(&mBlendingMode);

    if (mBlendingMode == BlendingMode::MASKED) {
        parser->getMaskThreshold(&mMaskThreshold);
    }

    if (mBlendingMode == BlendingMode::CUSTOM) {
        parser->getCustomBlendFunction(&mCustomBlendFunctions);
    }

    // blending mode
    switch (mBlendingMode) {
        // Do not change the MASKED behavior without checking for regressions with
        // AlphaBlendModeTest and TextureLinearInterpolationTest, with and without
        // View::BlendMode::TRANSLUCENT.
        case BlendingMode::MASKED:
        case BlendingMode::OPAQUE:
            mRasterState.blendFunctionSrcRGB   = BlendFunction::ONE;
            mRasterState.blendFunctionSrcAlpha = BlendFunction::ONE;
            mRasterState.blendFunctionDstRGB   = BlendFunction::ZERO;
            mRasterState.blendFunctionDstAlpha = BlendFunction::ZERO;
            mRasterState.depthWrite = true;
            break;
        case BlendingMode::TRANSPARENT:
        case BlendingMode::FADE:
            mRasterState.blendFunctionSrcRGB   = BlendFunction::ONE;
            mRasterState.blendFunctionSrcAlpha = BlendFunction::ONE;
            mRasterState.blendFunctionDstRGB   = BlendFunction::ONE_MINUS_SRC_ALPHA;
            mRasterState.blendFunctionDstAlpha = BlendFunction::ONE_MINUS_SRC_ALPHA;
            mRasterState.depthWrite = false;
            break;
        case BlendingMode::ADD:
            mRasterState.blendFunctionSrcRGB   = BlendFunction::ONE;
            mRasterState.blendFunctionSrcAlpha = BlendFunction::ONE;
            mRasterState.blendFunctionDstRGB   = BlendFunction::ONE;
            mRasterState.blendFunctionDstAlpha = BlendFunction::ONE;
            mRasterState.depthWrite = false;
            break;
        case BlendingMode::MULTIPLY:
            mRasterState.blendFunctionSrcRGB   = BlendFunction::ZERO;
            mRasterState.blendFunctionSrcAlpha = BlendFunction::ZERO;
            mRasterState.blendFunctionDstRGB   = BlendFunction::SRC_COLOR;
            mRasterState.blendFunctionDstAlpha = BlendFunction::SRC_COLOR;
            mRasterState.depthWrite = false;
            break;
        case BlendingMode::SCREEN:
            mRasterState.blendFunctionSrcRGB   = BlendFunction::ONE;
            mRasterState.blendFunctionSrcAlpha = BlendFunction::ONE;
            mRasterState.blendFunctionDstRGB   = BlendFunction::ONE_MINUS_SRC_COLOR;
            mRasterState.blendFunctionDstAlpha = BlendFunction::ONE_MINUS_SRC_COLOR;
            mRasterState.depthWrite = false;
            break;
        case BlendingMode::CUSTOM:
            mRasterState.blendFunctionSrcRGB   = mCustomBlendFunctions[0];
            mRasterState.blendFunctionSrcAlpha = mCustomBlendFunctions[1];
            mRasterState.blendFunctionDstRGB   = mCustomBlendFunctions[2];
            mRasterState.blendFunctionDstAlpha = mCustomBlendFunctions[3];
            mRasterState.depthWrite = false;
    }

    // depth write
    bool depthWriteSet = false;
    parser->getDepthWriteSet(&depthWriteSet);
    if (depthWriteSet) {
        bool depthWrite = false;
        parser->getDepthWrite(&depthWrite);
        mRasterState.depthWrite = depthWrite;
    }

    // alpha to coverage
    bool alphaToCoverageSet = false;
    parser->getAlphaToCoverageSet(&alphaToCoverageSet);
    if (alphaToCoverageSet) {
        bool alphaToCoverage = false;
        parser->getAlphaToCoverage(&alphaToCoverage);
        mRasterState.alphaToCoverage = alphaToCoverage;
    } else {
        mRasterState.alphaToCoverage = mBlendingMode == BlendingMode::MASKED;
    }
}

void MaterialDefinition::processSpecializationConstants(MaterialParser const* parser) {
    // Older materials won't have a constants chunk, but that's okay.
    parser->getConstants(&mMaterialConstants);
    for (size_t i = 0, c = mMaterialConstants.size(); i < c; i++) {
        auto& item = mMaterialConstants[i];
        // the key can be a string_view because mMaterialConstant owns the CString
        std::string_view const key{ item.name.data(), item.name.size() };
        mSpecializationConstantsNameToIndex[key] = i;
    }
}

void MaterialDefinition::processDescriptorSets(FEngine& engine, MaterialParser const* parser) {
    UTILS_UNUSED_IN_RELEASE bool success;

    success = parser->getDescriptorBindings(&mProgramDescriptorBindings);
    assert_invariant(success);

    backend::DescriptorSetLayout descriptorSetLayout;
    success = parser->getDescriptorSetLayout(&descriptorSetLayout);
    assert_invariant(success);

    // get the PER_VIEW descriptor binding info
    bool const isLit = mIsVariantLit || mHasShadowMultiplier;
    bool const isSSR = mReflectionMode == ReflectionMode::SCREEN_SPACE ||
            mRefractionMode == RefractionMode::SCREEN_SPACE;
    bool const hasFog = !(mVariantFilterMask & UserVariantFilterMask(UserVariantFilterBit::FOG));

    auto perViewDescriptorSetLayout = descriptor_sets::getPerViewDescriptorSetLayout(
            mMaterialDomain, isLit, isSSR, hasFog, false);

    auto perViewDescriptorSetLayoutVsm = descriptor_sets::getPerViewDescriptorSetLayout(
            mMaterialDomain, isLit, isSSR, hasFog, true);

    // set the labels
    descriptorSetLayout.label = CString{ mName }.append("_perMat");
    perViewDescriptorSetLayout.label = CString{ mName }.append("_perView");
    perViewDescriptorSetLayoutVsm.label = CString{ mName }.append("_perViewVsm");

    // get the PER_RENDERABLE and PER_VIEW descriptor binding info
    for (auto&& [bindingPoint, descriptorSetLayout] : {
            std::pair{ DescriptorSetBindingPoints::PER_RENDERABLE,
                    descriptor_sets::getPerRenderableLayout() },
            std::pair{ DescriptorSetBindingPoints::PER_VIEW,
                    perViewDescriptorSetLayout }}) {
        Program::DescriptorBindingsInfo& descriptors = mProgramDescriptorBindings[+bindingPoint];
        descriptors.reserve(descriptorSetLayout.bindings.size());
        for (auto const& entry: descriptorSetLayout.bindings) {
            auto const& name = descriptor_sets::getDescriptorName(bindingPoint, entry.binding);
            descriptors.push_back({ name, entry.type, entry.binding });
        }
    }

    mDescriptorSetLayout = {
            engine.getDescriptorSetLayoutFactory(),
            engine.getDriverApi(), std::move(descriptorSetLayout) };

    mPerViewDescriptorSetLayout = {
            engine.getDescriptorSetLayoutFactory(),
            engine.getDriverApi(), std::move(perViewDescriptorSetLayout) };

    mPerViewDescriptorSetLayoutVsm = {
            engine.getDescriptorSetLayoutFactory(),
            engine.getDriverApi(), std::move(perViewDescriptorSetLayoutVsm) };
}

/*******************************************************************************
 * Getters
 */

bool MaterialDefinition::hasParameter(const char* name) const noexcept {
    return mUniformInterfaceBlock.hasField(name) ||
           mSamplerInterfaceBlock.hasSampler(name) ||
            mSubpassInfo.name == CString(name);
}

bool MaterialDefinition::isSampler(const char* name) const noexcept {
    return mSamplerInterfaceBlock.hasSampler(name);
}

BufferInterfaceBlock::FieldInfo const* MaterialDefinition::reflect(
        std::string_view const name) const noexcept {
    return mUniformInterfaceBlock.getFieldInfo(name);
}


descriptor_binding_t MaterialDefinition::getSamplerBinding(
        std::string_view const& name) const {
    return mSamplerInterfaceBlock.getSamplerInfo(name)->binding;
}

size_t MaterialDefinition::getParameters(ParameterInfo* parameters, size_t count) const noexcept {
    count = std::min(count, getParameterCount());

    const auto& uniforms = mUniformInterfaceBlock.getFieldInfoList();
    size_t i = 0;
    size_t const uniformCount = std::min(count, size_t(uniforms.size()));
    for ( ; i < uniformCount; i++) {
        ParameterInfo& info = parameters[i];
        const auto& uniformInfo = uniforms[i];
        info.name = uniformInfo.name.c_str();
        info.isSampler = false;
        info.isSubpass = false;
        info.type = uniformInfo.type;
        info.count = std::max(1u, uniformInfo.size);
        info.precision = uniformInfo.precision;
    }

    const auto& samplers = mSamplerInterfaceBlock.getSamplerInfoList();
    size_t const samplerCount = samplers.size();
    for (size_t j = 0; i < count && j < samplerCount; i++, j++) {
        ParameterInfo& info = parameters[i];
        const auto& samplerInfo = samplers[j];
        info.name = samplerInfo.name.c_str();
        info.isSampler = true;
        info.isSubpass = false;
        info.samplerType = samplerInfo.type;
        info.count = 1;
        info.precision = samplerInfo.precision;
    }

    if (mSubpassInfo.isValid && i < count) {
        ParameterInfo& info = parameters[i];
        info.name = mSubpassInfo.name.c_str();
        info.isSampler = false;
        info.isSubpass = true;
        info.subpassType = mSubpassInfo.type;
        info.count = 1;
        info.precision = mSubpassInfo.precision;
    }

    return count;
}

std::optional<uint32_t> MaterialDefinition::getSpecializationConstantId(
        std::string_view const name) const noexcept {
    auto const pos = mSpecializationConstantsNameToIndex.find(name);
    if (pos != mSpecializationConstantsNameToIndex.end()) {
        return pos->second + CONFIG_MAX_RESERVED_SPEC_CONSTANTS;
    }
    return std::nullopt;
}

} // namespace filament
