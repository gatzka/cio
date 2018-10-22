# cio - An ANSI C Conformant I/O Library.
## License
Copyright (c) 2017 Stephan Gatzka. See the [LICENSE](LICENSE) file for license rights and
limitations (MIT).

## Build Status
[![Travis CI](https://travis-ci.org/gatzka/cio.svg?branch=master)](https://travis-ci.org/gatzka/cio)
[![AppVeyor CI](https://ci.appveyor.com/api/projects/status/gc5qmie31mnh9nww/branch/master?svg=true)](https://ci.appveyor.com/project/gatzka/cio/branch/master)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/32bbf2ee527148d0ba593586b7a83019)](https://www.codacy.com/app/gatzka/cio?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=gatzka/cio&amp;utm_campaign=Badge_Grade)
[![Coverity](https://scan.coverity.com/projects/12722/badge.svg)](https://scan.coverity.com/projects/gatzka-cio)
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/gatzka/cio/master/LICENSE)
[![codecov](https://codecov.io/gh/gatzka/cio/branch/master/graph/badge.svg)](https://codecov.io/gh/gatzka/cio)
[![Quality Gate](https://sonarcloud.io/api/project_badges/measure?project=org.gatzka.cio&metric=alert_status)](https://sonarcloud.io/dashboard?id=org.gatzka.cio)

[![Waffle.io - Columns and their card count](https://badge.waffle.io/gatzka/cio.svg?columns=all)](https://waffle.io/gatzka/cio)

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
[here](https://gatzka.github.io/cio/doc/).
