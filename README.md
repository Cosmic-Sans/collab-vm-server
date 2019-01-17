# collab-vm-server
[![Build status](https://ci.appveyor.com/api/projects/status/lgine3laiy0ojexr/branch/master?svg=true)](https://ci.appveyor.com/project/Cosmic-Sans/collab-vm-server/branch/master)

This repository contains the necessary files to compile the collab-vm-server. collab-vm-server powers CollabVM and it is what you will use to host it. Compilation instructions are below.

Please note that this is currently an incomplete project. This may not build properly, and it does not have full functionality yet. Please use [this](https://github.com/computernewb/collab-vm-server) repository to build/use the current stable version of collab-vm-server.

## Building on Windows

### Visual Studio
Requirements:
* Visual Studio 2017 (any edition)
	* Make sure to install the "Desktop development with C++" workload and the "Visual C++ tools for CMake" component
* [vcpkg](https://github.com/Microsoft/vcpkg)
* About 3 GiB of disk space for vcpkg packages and the [prebuilt Boost
  binaries](https://sourceforge.net/projects/boost/files/boost-binaries/) or 10
  GiB for only vcpkg packages
* [ODB Compiler (odb-2.4.0-i686-windows)](https://www.codesynthesis.com/products/odb/download.xhtml)

1. This repository relies on submodules. To clone both the repo and all of its submodules do:  
	```git clone --recursive https://github.com/Cosmic-Sans/collab-vm-server.git```  
Or if you've already cloned it, you can download only the submodules by doing:  
	```git submodule update --init --recursive```
1. After downloading vcpkg and running bootstrap-vcpkg.bat, use the following command to install all the required dependencies:
	```
	./vcpkg.exe install --triplet x86-windows cairo libjpeg-turbo libodb libodb-sqlite libpng openssl pthreads
	# If the prebuilt Boost binaries are not being used,
	# add 'boost' to the list of packages above
	```
1. Open the collab-vm-server folder in Visual Studio 2017, right-click on the CMakeLists.txt file in the Solution Explorer and click "Change CMake Settings" to create a CMakeSettings.json file. Then add a variables property to the configuration so it looks similar to the following:
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
		  // Only add this if the prebuilt binaries are being used
		  "name": "BOOST_ROOT",
		  // Fix this path
		  "value": "C:\\boost_1_69_0"
		},
		{
		  "name": "OPENSSL_ROOT_DIR",
		  // Fix this path
		  "value": "C:\\vcpkg\\installed\\x86-windows"
		},
		{
		  "name": "ODB-COMPILER_ROOT",
		  // Fix this path
		  "value": "C:\\odb-2.4.0-i686-windows"
		}
	  ]
	},
	...
	```
1. Verify the correct configuration is selected in the dropdown (e.g. x86-Debug) and build the solution.

## Building on Linux
To be written

## Building on anything else
It is currently unknown if this project compiles on any other operating systems. The main focus is Windows and Linux. However, if you can successfully get the collab-vm-server to build on another OS (e.g. MacOS, FreeBSD) then please make a pull request with instructions.
