# zmqprototype
Prototype code for cross platform zmq rep-req render node

###Build 

##Linux
```
apt install -y libzmq-dev
ldconfig
apt install -y libtool
apt install -y libsodium-dev
apt install -y cmake

git clone https://github.com/zeromq/libzmq
apt install libgnutls28-dev 
apt install pkg-config 
cd libzmq
mkdir build
cd build
cmake .. -DENABLE_CURVE=ON -DWITH_LIBSODIUM=/usr/include/sodium


git https://github.com/zeromq/cppzmq
cd cppzmq
mkdir build
cd build
cmake .. 

g++ server.cpp -o server -lzmq -Wl,-rpath,.
```

##MacOS
```
g++ -std=c++11 server.cpp -o server -I../libzmq/include -I../cppzmq -L../libzmq/build/lib -lzmq -Wl,-rpath,. 
g++ -std=c++11 server.cpp -o server -I../libzmq/include -I../cppzmq -L../libzmq/build/lib -lzmq -Wl,-rpath,. 
```



