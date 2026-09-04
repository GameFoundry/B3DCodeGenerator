[![Build Status](https://travis-ci.org/BearishSun/BansheeSBGen.svg?branch=master)](https://travis-ci.org/BearishSun/BansheeSBGen)
[![Build status](https://ci.appveyor.com/api/projects/status/lfpbyfy08jvuh0kt?svg=true)](https://ci.appveyor.com/project/BearishSun/bansheesbgen)


Tool used for automatic script binding generation for Banshee 3D game engine.

# Toolchain requirements

- CMake 4.2 or newer
- A C++17 compiler
- On Windows, Visual Studio 2026 with the v145 platform toolset (MSVC 14.50 or newer) and Windows 11 SDK 10.0.26100.0 or newer

# Setting up dependencies
This tool depends on Clang & LLVM. 

Build Clang:
 - Download latest release source from https://github.com/llvm/llvm-project/releases:
 - Follow guide here: https://clang.llvm.org/get_started.html
 - In short:
  - git clone https://github.com/llvm/llvm-project.git
  - cd llvm-project
  - git checkout llvmorg-20.1.7 (or the version bundled by B3D Framework)
  - mkdir build
  - cd build
  - cmake -S ../llvm -B . -G "Visual Studio 18 2026" -A x64 -DLLVM_ENABLE_PROJECTS=clang -DCMAKE_INSTALL_PREFIX="F:/ThirdParty/llvm-20-install" -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_RTTI=ON
  - For makefiles/ninja builds also specify -DCMAKE_BUILD_TYPE=Release (Not needed for VS/XCode)
  - Build Clang using the release configuration
  - Make sure to execute the 'install' target in your build tool
 - If the build fails due to out of memory, limit the amount of simultaneous project builds
 
Build SBGen:
- In CMake set clang_INSTALL_DIR variable pointing to the LLVM install folder
- In CMake set CMAKE_INSTALL_PREFIX variable to the SBGen dependencies folder of your Banshee install (i.e. BansheeRoot/Dependencies/tools/BansheeSBGen/)
- Configure with `cmake -S . -B Build -G "Visual Studio 18 2026" -A x64 -Dclang_INSTALL_DIR="F:/ThirdParty/llvm-20-install" -DCMAKE_INSTALL_PREFIX="F:/Banshee/Dependencies/tools/BansheeSBGen"`
- Build and install with `cmake --build Build --config Release --target INSTALL`
