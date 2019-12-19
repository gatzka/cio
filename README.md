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
[![Total alerts](https://img.shields.io/lgtm/alerts/g/gatzka/cio.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/gatzka/cio/alerts/)
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/gatzka/cio.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/gatzka/cio/context:cpp)

[![Open Hub](https://img.shields.io/badge/Open-Hub-0185CA.svg)](https://www.openhub.net/p/cio)

## Howto Build
```
git submodule update --init
```

cio lets you choose if you want to build a static or dynamic library.
The default is static. If you want a shared library, execute:
```
cmake -DBUILD_SHARED_LIBS=ON <path-to-cio-project>
```



## Documentation

The generated [doxygen](https://www.stack.nl/~dimitri/doxygen/) documentation can be found
[here](https://gatzka.github.io/cio/doc/html).
