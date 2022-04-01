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

#include <fstream>
#include <json/reader.h>

ShProgram::ShProgram(const std::string& name)
    : m_Name(name)
{ }

bool ShProgram::Load(const fs::path& descriptionFileName, const fs::path& projectPath)
{
    std::ifstream shaderFile(descriptionFileName.generic_string());
    if (!shaderFile.is_open())
    {
        LOG("WARNING: Cannot open file '%s'\n", descriptionFileName.generic_string().c_str());
        return false;
    }

    Json::Value root;
    try
    {
        shaderFile >> root;
    }
    catch(const std::exception& e)
    {
        LOG("WARNING: Cannot parse '%s': %s\n", descriptionFileName.generic_string().c_str(), e.what());
    }
    shaderFile.close();
    
    std::shared_ptr<ShRenderpass> imagePass;
    for (const auto& node : root[0]["renderpass"])
    {
        if (node["type"] == "buffer" || node["type"] == "image")
        {
            auto pass = std::make_shared<ShRenderpass>(m_Name, node, descriptionFileName, projectPath);
            if (node["type"] == "image")
                imagePass = pass;
            else
                m_Passes.push_back(pass);
        }
        else if (node["type"] == "common")
        {
            m_CommonSourcePath = descriptionFileName.parent_path() / node["code"].asString();
        }
    }
    if (!imagePass)
    {
        LOG("ERROR: program '%s' has no 'image' type pass.\n", m_Name.c_str());
        return false;
    }

    m_ImagePassIndex = int(m_Passes.size());
    m_Passes.push_back(imagePass);
    
    return true;
}

bool ShProgram::CompileShaders(blob& preamble)
{
    blob commonSource;
    if (!m_CommonSourcePath.empty())
        ReadFile(m_CommonSourcePath, commonSource);

    for (auto& pass : m_Passes)
    {
        if (!pass->CompilePassShader(preamble, commonSource))
            return false;
    }

    return true;
}
