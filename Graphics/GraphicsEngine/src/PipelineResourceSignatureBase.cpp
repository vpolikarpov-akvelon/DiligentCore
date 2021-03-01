/*
 *  Copyright 2019-2021 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *  
 *      http://www.apache.org/licenses/LICENSE-2.0
 *  
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

#include "PipelineResourceSignatureBase.hpp"

#include <unordered_map>

#include "HashUtils.hpp"
#include "StringTools.hpp"

namespace Diligent
{

#define LOG_PRS_ERROR_AND_THROW(...) LOG_ERROR_AND_THROW("Description of a pipeline resource signature '", (Desc.Name ? Desc.Name : ""), "' is invalid: ", ##__VA_ARGS__)

void ValidatePipelineResourceSignatureDesc(const PipelineResourceSignatureDesc& Desc, bool ShaderResourceRuntimeArraySupported) noexcept(false)
{
    if (Desc.BindingIndex >= MAX_RESOURCE_SIGNATURES)
        LOG_PRS_ERROR_AND_THROW("Desc.BindingIndex (", Uint32{Desc.BindingIndex}, ") exceeds the maximum allowed value (", MAX_RESOURCE_SIGNATURES - 1, ").");

    if (Desc.NumResources > MAX_RESOURCES_IN_SIGNATURE)
        LOG_PRS_ERROR_AND_THROW("Desc.NumResources (", Uint32{Desc.NumResources}, ") exceeds the maximum allowed value (", MAX_RESOURCES_IN_SIGNATURE, ").");

    if (Desc.NumResources != 0 && Desc.Resources == nullptr)
        LOG_PRS_ERROR_AND_THROW("Desc.NumResources (", Uint32{Desc.NumResources}, ") is not zero, but Desc.Resources is null.");

    if (Desc.NumImmutableSamplers != 0 && Desc.ImmutableSamplers == nullptr)
        LOG_PRS_ERROR_AND_THROW("Desc.NumImmutableSamplers (", Uint32{Desc.NumImmutableSamplers}, ") is not zero, but Desc.ImmutableSamplers is null.");

    if (Desc.UseCombinedTextureSamplers && (Desc.CombinedSamplerSuffix == nullptr || Desc.CombinedSamplerSuffix[0] == '\0'))
        LOG_PRS_ERROR_AND_THROW("Desc.UseCombinedTextureSamplers is true, but Desc.CombinedSamplerSuffix is null or empty");

    std::unordered_map<HashMapStringKey, SHADER_TYPE, HashMapStringKey::Hasher> ResourceShaderStages;
    // Hash map of resources by name
    std::unordered_multimap<HashMapStringKey, PipelineResourceDesc, HashMapStringKey::Hasher> ResourcesByName;

    for (Uint32 i = 0; i < Desc.NumResources; ++i)
    {
        const auto& Res = Desc.Resources[i];

        if (Res.Name == nullptr)
            LOG_PRS_ERROR_AND_THROW("Desc.Resources[", i, "].Name must not be null");

        if (Res.Name[0] == '\0')
            LOG_PRS_ERROR_AND_THROW("Desc.Resources[", i, "].Name must not be empty");

        if (Res.ShaderStages == SHADER_TYPE_UNKNOWN)
            LOG_PRS_ERROR_AND_THROW("Desc.Resources[", i, "].ShaderStages must not be SHADER_TYPE_UNKNOWN");

        if (Res.ArraySize == 0)
            LOG_PRS_ERROR_AND_THROW("Desc.Resources[", i, "].ArraySize must not be 0");

        auto& UsedStages = ResourceShaderStages[Res.Name];
        if ((UsedStages & Res.ShaderStages) != 0)
        {
            LOG_PRS_ERROR_AND_THROW("Multiple resources with name '", Res.Name,
                                    "' specify overlapping shader stages. There may be multiple resources with the same name in different shader stages, "
                                    "but the stages must not overlap.");
        }
        UsedStages |= Res.ShaderStages;

        if ((Res.Flags & PIPELINE_RESOURCE_FLAG_RUNTIME_ARRAY) != 0 && !ShaderResourceRuntimeArraySupported)
        {
            LOG_PRS_ERROR_AND_THROW("Incorrect Desc.Resources[", i, "].Flags: RUNTIME_ARRAY can only be used if ShaderResourceRuntimeArray device feature is enabled.");
        }

        static_assert(SHADER_RESOURCE_TYPE_LAST == 8, "Please add the new resource type to the switch below");

        auto AllowedResourceFlags = GetValidPipelineResourceFlags(Res.ResourceType);
        if ((Res.Flags & ~AllowedResourceFlags) != 0)
        {
            LOG_PRS_ERROR_AND_THROW("Incorrect Desc.Resources[", i, "].Flags (", GetPipelineResourceFlagsString(Res.Flags),
                                    "). Only the following flags are valid for a ", GetShaderResourceTypeLiteralName(Res.ResourceType),
                                    ": ", GetPipelineResourceFlagsString(AllowedResourceFlags, false, ", "), ".");
        }

        ResourcesByName.emplace(Res.Name, Res);

        // NB: when creating immutable sampler array, we have to define the sampler as both resource and
        //     immutable sampler. The sampler will not be exposed as a shader variable though.
        //if (Res.ResourceType == SHADER_RESOURCE_TYPE_SAMPLER)
        //{
        //    if (FindImmutableSampler(Desc.ImmutableSamplers, Desc.NumImmutableSamplers, Res.ShaderStages, Res.Name,
        //                             Desc.UseCombinedTextureSamplers ? Desc.CombinedSamplerSuffix : nullptr) >= 0)
        //    {
        //        LOG_PRS_ERROR_AND_THROW("Sampler '", Res.Name, "' is defined as both shader resource and immutable sampler.");
        //    }
        //}
    }

    if (Desc.UseCombinedTextureSamplers)
    {
        VERIFY_EXPR(Desc.CombinedSamplerSuffix != nullptr);
        for (Uint32 i = 0; i < Desc.NumResources; ++i)
        {
            const auto& Res = Desc.Resources[i];
            if (Res.ResourceType == SHADER_RESOURCE_TYPE_TEXTURE_SRV)
            {
                const auto AssignedSamplerName = String{Res.Name} + Desc.CombinedSamplerSuffix;

                auto sam_range = ResourcesByName.equal_range(AssignedSamplerName.c_str());
                for (auto sam_it = sam_range.first; sam_it != sam_range.second; ++sam_it)
                {
                    const auto& Sam = sam_it->second;
                    VERIFY_EXPR(AssignedSamplerName == Sam.Name);

                    if ((Sam.ShaderStages & Res.ShaderStages) != 0)
                    {
                        if (Sam.ResourceType != SHADER_RESOURCE_TYPE_SAMPLER)
                        {
                            LOG_PRS_ERROR_AND_THROW("Resource '", Sam.Name, "' combined with texture '", Res.Name, "' is not a sampler.");
                        }

                        if (Sam.ShaderStages != Res.ShaderStages)
                        {
                            LOG_PRS_ERROR_AND_THROW("Texture '", Res.Name, "' and sampler '", Sam.Name, "' assigned to it use different shader stages.");
                        }

                        if (Sam.VarType != Res.VarType)
                        {
                            LOG_PRS_ERROR_AND_THROW("The type (", GetShaderVariableTypeLiteralName(Res.VarType), ") of texture resource '", Res.Name,
                                                    "' does not match the type (", GetShaderVariableTypeLiteralName(Sam.VarType),
                                                    ") of sampler '", Sam.Name, "' that is assigned to it.");
                        }

                        ResourcesByName.erase(sam_it);

                        break;
                    }
                }
            }
        }

        for (auto& res_it : ResourcesByName)
        {
            if (res_it.second.ResourceType == SHADER_RESOURCE_TYPE_SAMPLER)
            {
                LOG_PRS_ERROR_AND_THROW("Sampler '", res_it.second.Name, "' is not assigned to any texture. All samplers must be assigned to textures when combined texture samplers are used.");
            }
        }
    }

    std::unordered_map<HashMapStringKey, SHADER_TYPE, HashMapStringKey::Hasher> ImtblSamShaderStages;
    for (Uint32 i = 0; i < Desc.NumImmutableSamplers; ++i)
    {
        const auto& SamDesc = Desc.ImmutableSamplers[i];
        if (SamDesc.SamplerOrTextureName == nullptr)
            LOG_PRS_ERROR_AND_THROW("Desc.ImmutableSamplers[", i, "].SamplerOrTextureName must not be null");

        if (SamDesc.SamplerOrTextureName[0] == '\0')
            LOG_PRS_ERROR_AND_THROW("Desc.ImmutableSamplers[", i, "].SamplerOrTextureName must not be empty");

        auto& UsedStages = ImtblSamShaderStages[SamDesc.SamplerOrTextureName];
        if ((UsedStages & SamDesc.ShaderStages) != 0)
        {
            LOG_PRS_ERROR_AND_THROW("Multiple immutable samplers with name '", SamDesc.SamplerOrTextureName,
                                    "' specify overlapping shader stages. There may be multiple immutable samplers with the same name in different shader stages, "
                                    "but the stages must not overlap.");
        }
        UsedStages |= SamDesc.ShaderStages;
    }
}

#undef LOG_PRS_ERROR_AND_THROW


Uint32 FindImmutableSampler(const ImmutableSamplerDesc* ImtblSamplers,
                            Uint32                      NumImtblSamplers,
                            SHADER_TYPE                 ShaderStages,
                            const char*                 ResourceName,
                            const char*                 SamplerSuffix)
{
    for (Uint32 s = 0; s < NumImtblSamplers; ++s)
    {
        const auto& Sam = ImtblSamplers[s];
        if (((Sam.ShaderStages & ShaderStages) != 0) && StreqSuff(ResourceName, Sam.SamplerOrTextureName, SamplerSuffix))
        {
            DEV_CHECK_ERR((Sam.ShaderStages & ShaderStages) == ShaderStages,
                          "Resource '", ResourceName, "' is defined for the following shader stages: ", GetShaderStagesString(ShaderStages),
                          ", but immutable sampler '", Sam.SamplerOrTextureName, "' specifes only some of these stages: ", GetShaderStagesString(Sam.ShaderStages),
                          ". A resource that is present in multiple shader stages can't use different immutable samples in different stages. "
                          "Either use separate resources for different stages, or define the immutable sample for all stages that the resource uses.");
            return s;
        }
    }

    return InvalidImmutableSamplerIndex;
}

/// Returns true if two pipeline resources are compatible
inline bool PipelineResourcesCompatible(const PipelineResourceDesc& lhs, const PipelineResourceDesc& rhs)
{
    // Ignore resource names.
    // clang-format off
    return lhs.ShaderStages == rhs.ShaderStages &&
           lhs.ArraySize    == rhs.ArraySize    &&
           lhs.ResourceType == rhs.ResourceType &&
           lhs.VarType      == rhs.VarType      &&
           lhs.Flags        == rhs.Flags;
    // clang-format on
}

bool PipelineResourceSignaturesCompatible(const PipelineResourceSignatureDesc& Desc0,
                                          const PipelineResourceSignatureDesc& Desc1) noexcept
{
    if (Desc0.BindingIndex != Desc1.BindingIndex)
        return false;

    if (Desc0.NumResources != Desc1.NumResources)
        return false;

    for (Uint32 r = 0; r < Desc0.NumResources; ++r)
    {
        if (!PipelineResourcesCompatible(Desc0.Resources[r], Desc1.Resources[r]))
            return false;
    }

    if (Desc0.NumImmutableSamplers != Desc1.NumImmutableSamplers)
        return false;

    for (Uint32 s = 0; s < Desc0.NumImmutableSamplers; ++s)
    {
        const auto& Samp0 = Desc0.ImmutableSamplers[s];
        const auto& Samp1 = Desc1.ImmutableSamplers[s];

        if (Samp0.ShaderStages != Samp1.ShaderStages ||
            !(Samp0.Desc == Samp1.Desc))
            return false;
    }

    return true;
}

size_t CalculatePipelineResourceSignatureDescHash(const PipelineResourceSignatureDesc& Desc) noexcept
{
    if (Desc.NumResources == 0 && Desc.NumImmutableSamplers == 0)
        return 0;

    size_t Hash = ComputeHash(Desc.NumResources, Desc.NumImmutableSamplers, Desc.BindingIndex);

    for (Uint32 i = 0; i < Desc.NumResources; ++i)
    {
        const auto& Res = Desc.Resources[i];
        HashCombine(Hash, Uint32{Res.ShaderStages}, Res.ArraySize, Uint32{Res.ResourceType}, Uint32{Res.VarType}, Uint32{Res.Flags});
    }

    for (Uint32 i = 0; i < Desc.NumImmutableSamplers; ++i)
    {
        HashCombine(Hash, Uint32{Desc.ImmutableSamplers[i].ShaderStages}, Desc.ImmutableSamplers[i].Desc);
    }

    return Hash;
}

} // namespace Diligent
