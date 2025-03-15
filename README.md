# poomer-zmq-client_server
c++ cross platform prototype zmq rep-req client and server with encryption

[ZeroMQ messaging library](https://zeromq.org)

server starts in pubkey server mode, allowing one client to grab the pubkey
- keypairs are generated on every start creating a purely encrypted connection. 
- Host verification and Trust on First Usage security design pattern are not incorporated. (ie .ssh/know_hosts)
- used tcp ports 5555, 5556, 5557
- [TODO] backport bellatui Args, requires making bella_scene_sdk a dependency

# Usage
- [TODO] Fill in


# Build 

```
workdir/
├── libzmq/
├── cppzmq/
├── poomer-zmq-client_server/

( additional Windows package manager dependency )
├── vcpkg/

( additional MacOS package manager dependency )
└── homebrew/

```

## Linux
```
//apt install -y libzmq-dev
//ldconfig
apt install -y libtool
apt install -y libsodium-dev
apt install -y cmake
mkdir workdir
cd workdir
apt install libgnutls28-dev 
apt install pkg-config 
git clone https://github.com/zeromq/libzmq
cd libzmq
mkdir build
cd build
cmake .. -DENABLE_CURVE=ON -DWITH_LIBSODIUM=/usr/include/sodium
cd ../..
git https://github.com/zeromq/cppzmq
cd cppzmq
mkdir build
cd build
cmake .. 
g++ server.cpp -o server -lzmq -Wl,-rpath,.
```

## MacOS
Install Cmake to /Applications
```
curl -LO https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-macos-universal.dmg
open cmake-3.31.6-macos-universal.dmg 
```
Install Xcode

```
mkdir -p workdir/homebrew
cd workdir
curl -L https://github.com/Homebrew/brew/tarball/master | tar xz --strip-components 1 -C homebrew
eval "$(homebrew/bin/brew shellenv)"
brew update --force --quiet
//chmod -R go-w "$(brew --prefix)/share/zsh"
brew install libsodium
//brew install gnutls
brew install pkg-config
git clone https://github.com/zeromq/libzmq
cd libzmq
mkdir build
cd build
/Applications/CMake.app/Contents/bin/cmake .. -DENABLE_CURVE=ON -DWITH_LIBSODIUM=../../homebrew/Cellar/libsodium/1.0.20/include/sodium -DSODIUM_INCLUDE_DIRS=../../homebrew/Cellar/libsodium/1.0.20/include -DSODIUM_LIBRARIES=../../homebrew/Cellar/libsodium/1.0.20/lib/libsodium.a
make -j4
cd ../..
git clone https://github.com/zeromq/cppzmq
git clone https://github.com/oomer/poomer-zmq-client_server.git
cd poomer-zmq-client_server
g++ -std=c++11 server.cpp -o server -I../libzmq/include -I../cppzmq -L../libzmq/build/lib -lzmq -Wl,-rpath,. 
g++ -std=c++11 client.cpp -o client -I../libzmq/include -I../cppzmq -L../libzmq/build/lib -lzmq -Wl,-rpath,. 
cp ../libzmq/build/lib/libzmq.5.dylib .
```
- [TODO] wrap in a makefile, see bellatui
- [TODO] put in bin/Darwin
- [TODO] clears compile time warnings

## Windows
Install Visual Studio Community with Desktop C++
open a x64 Developer Shell

```
mkdir workdir
cd workdir
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg; .\bootstrap-vcpkg.bat
.\vcpkg.exe install boost:x64-windows zeromq[sodium]:x64-windows
.\vcpkg.exe install cppzmq:x64-windows
.\vcpkg integrate install
cd ..
git clone https://github.com/oomer/bellatui.git
cd bellatui
cl /std:c++17 client.cpp -Fe:client.exe -Ic:..\vcpkg\installed\x64-windows\include\ /link ..\vcpkg\installed\x64-windows\lib\libzmq-mt-4_3_5.lib
cl /std:c++17 server.cpp -Fe:server.exe -Ic:..\vcpkg\installed\x64-windows\include\ /link ..\vcpkg\installed\x64-windows\lib\libzmq-mt-4_3_5.lib
```



