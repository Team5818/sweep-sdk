# Libsweep Examples

Examples for the `libsweep` library.

This can be either the dummy library always returning static point cloud data or the device library requiring the Scanse Sweep device to be plugged in.

### Quick Start

#### To build on Linux: 

Requires `libsweep.so` be installed.

```bash
# build the examples
mkdir build
cd build
cmake ..
cmake --build .
```

```bash
    # run the examples
    ./example-c /dev/ttyUSB0
    ./example-c++ /dev/ttyUSB0
```

Real-time viewer:

**Note:** The viewer requires SFML2 to be installed.

```bash
    ./example-viewer /dev/ttyUSB0
```

Pub-Sub networking example:

**Note:** The pub-sub networking example requires Protobuf and ZeroMQ to be installed.

Start a publisher sending out full 360 degree scans via the network (localhost).
Then start some subscribers connecting to the publisher.

```bash
./example-net publisher
./example-net subscriber
```


#### To build on Windows with MinGW-w64 and MSYS2:
Requires that `libsweep.dll` be installed somewhere on the user environment variable "PATH", such as `"C:\msys32\mingw64\bin"` for example. You can edit the installation path in `config.mk`.

```bash
# build the examples
mkdir build
cd build
cmake -G "MSYS Makefiles" ..
cmake --build .
```

**Note:** the viewer & pub/sub examples are not compatible with windows currently.

```bash
# run the examples (the number 5 is just an example, replace it with your COM port number)
./example-c COM5
./example-c++ COM5
```

### License

Copyright © 2016 Daniel J. Hofmann

Distributed under the MIT License (MIT).
