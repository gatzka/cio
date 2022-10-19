# cio - An ANSI C Conformant I/O Library.
## License
Copyright (c) 2017 Stephan Gatzka. See the [LICENSE](LICENSE) file for license rights and
limitations (MIT).

## Build Status
[![Github action](https://github.com/gatzka/cio/workflows/CI%20build/badge.svg?branch=master)](https://github.com/gatzka/cio/actions)
[![Coverity](https://scan.coverity.com/projects/12722/badge.svg)](https://scan.coverity.com/projects/gatzka-cio)
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/gatzka/cio/master/LICENSE)
[![codecov](https://codecov.io/gh/gatzka/cio/branch/master/graph/badge.svg)](https://codecov.io/gh/gatzka/cio)
[![Quality Gate](https://sonarcloud.io/api/project_badges/measure?project=org.gatzka.cio&metric=alert_status)](https://sonarcloud.io/dashboard?id=org.gatzka.cio)

[![Open Hub](https://img.shields.io/badge/Open-Hub-0185CA.svg)](https://www.openhub.net/p/cio)

## Howto Build
Create a build directory and change into it:
```
mkdir /tmp/cio && cd /tmp/cio
```
Configure the project using cmake:
```
cmake -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchains/x86-linux-clang-14.cmake <path/to/cio/src/dir>
```
Please not that the passage with ```-DCMAKE_TOOLCHAIN_FILE``` is optional, if you want to use the build hosts gcc. By default cio is build as a static library. If you want to build a shared library instead, add ```-DBUILD_SHARED_LIBS=ON``` to the configuration command line.
If you want to speed up the build, choose the Ninja generator by adding ```-GNinja``` to the configuration command line.

Then build the project:
```
cmake --build .
```

Run the unit test by issueing the following command:
```
cmake --build . --target test
```

### CI builds
Continuous Integration builds are done using ctest. You can also run them locally. For instance, to run the CI
build that gives you the code coverage of the unit test,run:
```
ctest -S ~/workspace/git/cio/build.cmake -DCTEST_TOOLCHAIN_FILE=toolchains/x86-linux-gcc.cmake -DCTEST_CONFIGURATION_TYPE:STRING=Coverage
```

## Documentation

The generated [doxygen](http://www.doxygen.nl/) documentation can be found
[here](https://gatzka.github.io/cio/doc/html).
