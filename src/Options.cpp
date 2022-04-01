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
#include <cstring>

bool CommandLineOptions::novalue(const char* arg)
{
    errorMessage = "expected value for " + std::string(arg);
    return false;
}

bool CommandLineOptions::parse(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        const char* value = (i + 1 < argc) ? argv[i+1] : nullptr;

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
        {
            errorMessage = 
                "Standalone player for Shadertoys.\n"
                "Available options:\n"
                "   -h, --help: show this message\n"
                "   -W, --width <value>: set window or screen width\n"
                "   -H, --height <value>: set window or screen height\n"
                "   -R, --rate <value>: set refresh rate in full screen mode\n"
                "   -f, --fullscreen: enable full screen mode\n"
                "   -m, --monitor <index>: set the monitor index for full screen mode\n"
                "   -d, --debug: enable the Vulkan validation layer\n"
                "   -p, --project <path>: path to the project, default is cwd\n"
                "   -s, --shader <name>: start with a particular shader\n"
                "   -t, --script <path>: path to the script file, default is script.json\n"
                "   -i, --interval <value>: set the interval between shaders in seconds\n"
            ;
            return false;
        }
        else if (strcmp(arg, "-W") == 0 || strcmp(arg, "--width") == 0)
        {
            if (!value) return novalue(arg);
            width = atoi(value);
            ++i;
        }
        else if (strcmp(arg, "-H") == 0 || strcmp(arg, "--height") == 0)
        {
            if (!value) return novalue(arg);
            height = atoi(value);
            ++i;
        }
        else if (strcmp(arg, "-R") == 0 || strcmp(arg, "--rate") == 0)
        {
            if (!value) return novalue(arg);
            refreshRate = atoi(value);
            ++i;
        }
        else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--fullscreen") == 0)
        {
            fullscreen = true;
        }
        else if (strcmp(arg, "-m") == 0 || strcmp(arg, "--monitor") == 0)
        {
            if (!value) return novalue(arg);
            monitor = atoi(value);
            ++i;
        }
        else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--debug") == 0)
        {
            debug = true;
        }
        else if (strcmp(arg, "-p") == 0 || strcmp(arg, "--project") == 0)
        {
            if (!value) return novalue(arg);
            projectPath = value;
            ++i;
        }
        else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--shader") == 0)
        {
            if (!value) return novalue(arg);
            shader = value;
            ++i;
        }
        else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--script") == 0)
        {
            if (!value) return novalue(arg);
            scriptFile = value;
            ++i;
        }
        else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--interval") == 0)
        {
            if (!value) return novalue(arg);
            interval = atoi(value);
            ++i;
        }
        else
        {
            errorMessage = "unrecognized option " + std::string(arg);
            return false;
        }
    }

    return true;
}
