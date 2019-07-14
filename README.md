# collab-vm-server
[![AppVeyor build status](https://ci.appveyor.com/api/projects/status/lgine3laiy0ojexr/branch/master?svg=true)](https://ci.appveyor.com/project/Cosmic-Sans/collab-vm-server/branch/master)
[![Travis build status](https://travis-ci.org/Cosmic-Sans/collab-vm-server.svg?branch=master)](https://travis-ci.org/Cosmic-Sans/collab-vm-server)

This repository contains the necessary files to compile the collab-vm-server. collab-vm-server powers CollabVM and it is what you will use to host it. Compilation instructions are below.

Please note that this is currently an incomplete project. This may not build properly, and it does not have full functionality yet. Please use [this](https://github.com/computernewb/collab-vm-server) repository to build/use the current stable version of collab-vm-server.

## Building on Windows

### Visual Studio
Requirements:
* Visual Studio 2019 (any edition)
	* Make sure to install the "Desktop development with C++" workload
* [vcpkg](https://github.com/Microsoft/vcpkg)

1. This repository relies on submodules. To clone both the repo and all of its submodules do:  
	```git clone --recursive https://github.com/Cosmic-Sans/collab-vm-server.git```  
Or if you've already cloned it, you can download only the submodules by doing:  
	```git submodule update --init --recursive```
1. After downloading vcpkg and running bootstrap-vcpkg.bat, use the following command to install all the required dependencies:
	```
	./vcpkg.exe install --triplet x86-windows cairo libjpeg-turbo sqlite3 libpng openssl pthreads
	```
1. Open the collab-vm-server folder in Visual Studio 2019, right-click on the CMakeLists.txt file in the Solution Explorer and click "Change CMake Settings" to create a CMakeSettings.json file. Then add a variables property to the configuration so it looks similar to the following:
	```
	...
	{
	  "name": "x86-Debug",
	  "generator": "Ninja",
	  "configurationType": "Debug",
	  "inheritEnvironments": [ "msvc_x86" ],
	  "buildRoot": "${env.USERPROFILE}\\CMakeBuilds\\${workspaceHash}\\build\\${name}",
	  "installRoot": "${env.USERPROFILE}\\CMakeBuilds\\${workspaceHash}\\install\\${name}",
	  "cmakeCommandArgs": "",
	  "buildCommandArgs": "-v",
	  "ctestCommandArgs": "",
	  "variables": [
		{
		  "name": "CMAKE_TOOLCHAIN_FILE",
		  // Fix this path
		  "value": "C:\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake"
		},
		{
		  "name": "VCPKG_TARGET_TRIPLET",
		  "value": "x86-windows"
		},
		{
		  "name": "OPENSSL_ROOT_DIR",
		  // Fix this path
		  "value": "C:\\vcpkg\\installed\\x86-windows"
		}
	  ]
	},
	...
	```
1. Verify the correct configuration is selected in the dropdown (e.g. x86-Debug) and build the solution.

## Building on Linux and macOS
GCC (minimum version: 8) or Clang (minimum version: 8) are required. Clang must be used on macOS.

Build vcpkg and required packages:
```
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
./vcpkg install cairo libjpeg-turbo sqlite3 libpng openssl
```

Build collab-vm-server:
```
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_CXX_COMPILER=/path/to/bin/clang++ -DCMAKE_C_COMPILER=/path/to/bin/clang ..
cmake --build .
```

## Building on anything else
It is currently unknown if this project compiles on any other operating systems. The main focus is Windows and Linux. However, if you can successfully get the collab-vm-server to build on another OS (e.g. MacOS, FreeBSD) then please make a pull request with instructions.
