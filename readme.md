[![Build Status](https://travis-ci.org/BearishSun/BansheeSBGen.svg?branch=master)](https://travis-ci.org/BearishSun/BansheeSBGen)
[![Build status](https://ci.appveyor.com/api/projects/status/lfpbyfy08jvuh0kt?svg=true)](https://ci.appveyor.com/project/BearishSun/bansheesbgen)


Tool used for automatic script binding generation for Banshee 3D game engine.

# Setting up dependencies
This tool depends on Clang & LLVM. 

Build Clang:
 - Download latest release source from https://github.com/llvm/llvm-project/releases:
 - Follow guide here: https://clang.llvm.org/get_started.html
 - In short:
  - git clone https://github.com/llvm/llvm-project.git
  - git checkout llvmorg-15.0.1 (Or tag matching the release you wish to compile)
  - cd llvm-project
  - mkdir build
  - cd build
  - cmake -DLLVM_ENABLE_PROJECTS=clang -DCMAKE_INSTALL_PREFIX="F:/ThirdParty/llvm-15-install" -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_RTTI=ON -Thost=x64 ..\llvm
  - For makefiles/ninja builds also specify -DCMAKE_BUILD_TYPE=Release (Not needed for VS/XCode)
  - Build Clang using the release configuration
  - Make sure to execute the 'install' target in your build tool
 - If the build fails due to out of memory, limit the amount of simultaneous project builds
 
Build SBGen:
- In CMake set clang_INSTALL_DIR variable pointing to the LLVM install folder
- In CMake set CMAKE_INSTALL_PREFIX variable to the SBGen dependencies folder of your Banshee install (i.e. BansheeRoot/Dependencies/tools/BansheeSBGen/)
- Build SBGen using the release configuration
 - Execute the 'install' target