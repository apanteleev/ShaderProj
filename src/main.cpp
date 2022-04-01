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

using namespace std;

enum ExitCodes
{
    E_OK = 0,
    E_CommandLineError = 1,
    E_NoScript = 2,
    E_NoPrograms = 3,
    E_ShaderError = 4,
    E_VulkanError = 5
};

int main(int argc, char** argv)
{
    CommandLineOptions options;

    if (!options.parse(argc, argv))
    {
        LOG("%s\n", options.errorMessage.c_str());
        return ExitCodes::E_CommandLineError;
    }

    auto projectPath = options.projectPath.empty()
        ? fs::current_path()
        : fs::path(options.projectPath);

    auto scriptPath = options.scriptFile.empty()
        ? projectPath / "script.json"
        : fs::path(options.scriptFile);

    vector<ScriptEntry> script;
    if (options.shader.empty())
    {
        if (!LoadScript(scriptPath, script))
            return ExitCodes::E_NoScript;
    }
    else
    {
        ScriptEntry entry;
        entry.programName = options.shader;
        script.push_back(entry);
    }
    
    unordered_set<string> programNames;
    for (const auto& entry : script)
    {
        programNames.insert(entry.programName);
    }
    
    vector<shared_ptr<ShProgram>> programs;
    for (const auto& shaderName : programNames)
    {
        fs::path descriptionFile = projectPath / shaderName / "description.json";

        shared_ptr<ShProgram> program = make_shared<ShProgram>(shaderName);

        if (program->Load(descriptionFile, projectPath))
            programs.push_back(program);
    }

    if (programs.empty())
    {
        LOG("ERROR: No programs loaded.\n");
        return ExitCodes::E_NoPrograms;
    }
    
    InitImageCache();
    InitCompiler();
    
    unique_ptr<ShaderProj> application = make_unique<ShaderProj>(programs);
    if (!application->LoadShaders())
        return ExitCodes::E_ShaderError;

    application->SetScript(script, options.interval);

    VulkanAppParameters appParams;
    appParams.windowWidth = options.width;
    appParams.windowHeight = options.height;
    appParams.refreshRate = options.refreshRate;
    appParams.enableDebugRuntime = options.debug;
    appParams.startFullscreen = options.fullscreen;
    appParams.monitorIndex = options.monitor;
    appParams.enableVsync = true;

    if (!application->InitVulkan(appParams, "ShaderProj"))
        return ExitCodes::E_VulkanError;

    application->Init();

    application->RunMessageLoop();
    application->GetDevice().waitIdle();

    programs.clear();
    ShutdownImageCache(application->GetDevice());

    application->Shutdown();
    
    ShutdownCompiler();

    return ExitCodes::E_OK;
}
