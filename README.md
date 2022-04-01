# ShaderProj

ShaderProj is a simple standalone player for Shadertoy programs. The player uses Vulkan for rendering and works on Linux and Windows. Multiple programs can be loaded at once and played in a loop, according to a script that specifies the order and duration of each program.

The primary use case for this player is holiday decorations: select a set of shaders that you like and set a projector to show them on a wall, in a loop, unattended.

## Building ShaderProj

To build ShaderProj, you'll need CMake and a C++17 compiler (Visual Studio 2019, GCC, Clang).

- Checkout this repository.
- Initialize the submodules: `git submodule update --init --recursive`
- Create a build folder: `mkdir build && cd build`
- Generate the project file: `cmake ..`
- Build with `make`, `ninja` or Visual Studio

## Downloading Programs

First, create a project folder where your shaders and scripts will be.

Then, identify the shaders that you want to download and use the `download.py` script. The script needs Python 3.9+ and the `requests` module.

`/path/to/download.py <id> <outputPath>` will download (and patch) the shader description JSON file, the shaders, and the resources.

- `id` is either a full Shadertoy URL like `https://www.shadertoy.com/view/7lKSWW` or just the shader ID like `7lKSWW`
- `outputPath` is an optional argument that specifies where the program will be places. By default, it's just the shader ID in current directory.

## Creating a Script

In order to play multiple shaders in a loop, a script must be created. Without a script, ShaderProj can only play one shader.

The script is a JSON file with simple structure, for example:

```json
[
  { "program": "Path1", "duration": 2 },
  { "program": "Path2" },
  { "program": "Path3" }
]
```

- The `program` parameters are program paths relative to the script location, normally just folder names.
- The `duration` parameters are optional and specify the duration factors for each program in the script; the default is 1.0. Base duration that is multiplied by these factors is set from the ShaderProj command line.

## Running ShaderProj

To run a single program without a script:

`shaderproj --shader <path-to-folder>`

To run a script:

`shaderproj --script <path-to-json> --duration <seconds>`

For a full list of command line options, run `shaderproj --help`.

At runtime, the following keys are processed:

- `Left` and `Right` to switch the program.
- `Space` to pause.
- `R` to reload and recompile the programs.
- `Q` to quit.

## Limitations

ShaderProj can run many programs found on Shadertoy just fine, including multipass programs, but there are some missing features.

- No sound.
- No video inputs.
- No keyboard input.
- No rendering to cubemap.
- Possibly something else that I haven't encountered.

Additionally, there are some minor differences in how GLSL shaders are processed by WebGL and Vulkan/glslang. This means that you'll occasionally need to do some debugging to make a program work correctly. Most notably, Vulkan doesn't automatically initialize variables to zero, which can lead to garbage output if a shader leaves some variables uninitialized.

## License

ShaderProj is distributed under the terms of the [MIT License](LICENSE.txt).

The copyright is assigned to NVIDIA Corporation, although it is a personal project of Alexey Panteleev, a DevTech engineer at NVIDIA.

The original Shadertoy programs have different license terms; please respect them when downloading and sharing the programs. The Shadertoy Terms of Service can be found [here](https://www.shadertoy.com/terms).