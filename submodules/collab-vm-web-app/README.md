collab-vm-web-app
-----------------
[![Travis build status](https://travis-ci.org/Cosmic-Sans/collab-vm-web-app.svg?branch=master)](https://travis-ci.org/Cosmic-Sans/collab-vm-web-app)

Building
--------
Requirements:
 * CMake
 * Emscripten
 * npm
 
First, clone this project. This project relies on submodules, so to clone the project and its submodules, run the following command:

``git clone --recursive https://github.com/Cosmic-Sans/collab-vm-web-app.git``

Then, build the capnp tool as you normally would for your platform:
```
mkdir build/capnp_tool
cd build/capnp_tool
cmake -DCMAKE_INSTALL_PREFIX=install -DBUILD_TESTING=OFF ../../submodules/collab-vm-common/submodules/capnproto/
cmake --build . --target install
```

Finally, build the Emscripten part:
```
mkdir build/collab-vm-web-app
cd build/collab-vm-web-app
emcmake cmake -DCAPNP_ROOT="../capnp_tool/install" ../..
cmake --build .
```
