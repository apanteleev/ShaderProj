/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "ShaderProj.h"
#include "Log.h"

#include <glslang/Include/ShHandle.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <StandAlone/ResourceLimits.h>

using namespace std;

void InitCompiler()
{
    glslang::InitializeProcess();
}

void ShutdownCompiler()
{
    glslang::FinalizeProcess();
}

static EShLanguage GetShaderStage(const string& fileName)
{
    if (fileName.find(".vert") != string::npos)
        return EShLangVertex;
    else if (fileName.find(".frag") != string::npos)
        return EShLangFragment;
    else if (fileName.find(".comp") != string::npos)
        return EShLangCompute;

    return EShLangFragment;
}

bool CompileShader(const fs::path& shaderFile, const vector<blob*>& preambles, blob& output)
{
    if (!fs::exists(shaderFile))
    {
        LOG("ERROR: shader file '%s' does not exist\n", shaderFile.generic_string().c_str());
        return false;
    }

    fs::path outputFile = shaderFile;
    outputFile.replace_extension(".spv");

    if (fs::exists(outputFile))
    {
        auto inputTime = fs::last_write_time(shaderFile);
        auto outputTime = fs::last_write_time(outputFile);
        if (outputTime > inputTime)
        {
            if (ReadFile(outputFile, output))
            {
                LOG("Using cached shader file '%s'\n", outputFile.generic_string().c_str());
                return true;
            }

            output.clear();
        }
    }

    blob contents;
    if (ReadFile(shaderFile, contents))
    {
        LOG("Compiling shader '%s'... ", shaderFile.generic_string().c_str());

        auto shaderStage = GetShaderStage(shaderFile.generic_string());
        glslang::TShader* shader = new glslang::TShader(shaderStage);
        glslang::TProgram* program = new glslang::TProgram;

        blob mergedSource;
        for (auto preamble : preambles)
            mergedSource.insert(mergedSource.end(), preamble->begin(), preamble->end());
        mergedSource.insert(mergedSource.end(), contents.begin(), contents.end());

        const char* shaderText = mergedSource.data();
        const int shaderTextLen = (int)mergedSource.size();
        shader->setStringsWithLengths(&shaderText, &shaderTextLen, 1);

        shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
        shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

        int defaultVersion = 400;
        EShMessages messages = EShMsgDefault;

        static TBuiltInResource Resources = glslang::DefaultTBuiltInResource;

        if (!shader->parse(&Resources, defaultVersion, false, messages))
        {
            const char* infoLog = shader->getInfoLog();
            LOG("ERROR\n%s\n", infoLog);
            return false;
        }

        program->addShader(shader);
        if (!program->link(messages))
        {
            const char* infoLog = program->getInfoLog();
            LOG("ERROR\n%s\n", infoLog);
            return false;
        }

        LOG("OK\n");

        glslang::TIntermediate* intermediate = program->getIntermediate(shaderStage);
        assert(intermediate);

        glslang::SpvOptions spvOptions;
        vector<unsigned int> spirv;
        glslang::GlslangToSpv(*intermediate, spirv, &spvOptions);

        glslang::OutputSpvBin(spirv, outputFile.generic_string().c_str());

        output.resize(spirv.size() * sizeof(spirv[0]));
        memcpy(output.data(), spirv.data(), output.size());

        return true;
    }

    LOG("ERROR: couldn't read shader file '%s'\n", shaderFile.generic_string().c_str());
    return false;
}
