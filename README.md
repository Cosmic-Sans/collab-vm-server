# collab-vm-server

## Building on Windows

Requirements:
* Visual Studio 2017 (any edition)
	* Make sure to install the "Desktop development with C++" workload and the "Visual C++ tools for CMake" component
* [vcpkg](https://github.com/Microsoft/vcpkg)
* About 10 GiB of disk space for vcpkg packages
* [ODB Compiler (odb-2.4.0-i686-windows)](https://www.codesynthesis.com/products/odb/download.xhtml)

1. After downloading vcpkg and running bootstrap-vcpkg.bat, use the following command to install all the required dependencies:
```
./vcpkg.exe install --triplet x86-windows boost cairo libjpeg-turbo libodb libodb-sqlite libpng openssl pthreads
```
2. Open the collab-vm-server folder in Visual Studio 2017, right-click on the CMakeLists.txt file in the Solution Explorer and click "Change CMake Settings" to create a CMakeSettings.json file. Then add a variables property to the configuration so it looks similar to the following:
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
	  /* Make sure these paths are correct */
	  "value": "C:\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake"
	},
	{
	  "name": "VCPKG_TARGET_TRIPLET",
	  "value": "x86-windows"
	},
	{
	  "name": "OPENSSL_ROOT_DIR",
	  "value": "C:\\vcpkg\\installed\\x86-windows"
	},
	{
	  "name": "ODB-COMPILER_ROOT",
	  "value": "C:\\odb-2.4.0-i686-windows"
	}
  ]
},
...
```
3. Verify the correct configuration is selected in the dropdown (e.g. x86-Debug) and build the solution.
