/*
 * The MIT License (MIT)
 *
 * Copyright (c) <2017> <Stephan Gatzka>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

import qbs 1.0
import qbs.TextFile
import "./version.qbs" as cioVersionFile

Project {
  name: "cio libraries"
  minimumQbsVersion: "1.6.0"

  qbsSearchPaths: "../qbs/"

  SubProject {
    filePath: "../qbs/hardening.qbs"
    Properties {
      name: "hardening settings"
    }
  }

  SubProject {
    filePath: "../qbs/gccClang.qbs"
    Properties {
      name: "GCC/Clang switches"
    }
  }

  Product {
    type: "staticlibrary"
    name: "cio-static"

    Depends { name: "cpp" }
    Depends { name: "gccClang" }
    Depends { name: "hardening" }
    Depends { name: "generateVersion" }

    cpp.warningLevel: "all"
    cpp.treatWarningsAsErrors: true
    cpp.includePaths: [".", buildDirectory + "/generated/"]

    cioVersionFile {
      prefix: product.sourceDirectory + "/"
    }

    Group {
      name: "version header"
      files: [
        "cio_version.h.in"
      ]
      fileTags: ["versionHeaderToPatch"]
    }

    Group {
      name: "ANSI C conformant"
      
      cpp.cLanguageVersion: "c99"
      
      files: [
        "*.c",
      ]
    
      Group {
        name: "public headers"
        files: ["*.h"]
      }
    }

    Group {
      condition: qbs.targetOS.contains("linux")
      name: "linux specific"
      prefix: "linux/"

      files: [
        "*.c",
      ]

      Group {
        name: "public linux headers"
        prefix: "linux/"
        files: ["*.h"]
      }
    }

    Group {
      fileTagsFilter: product.type
      qbs.install: true
      qbs.installDir: "lib"
    }
  }

  DynamicLibrary {
    name: "cio-dynamic"
    version: versionInfo.result

    Probe {
      id: versionInfo
      property string result
      property path sourceDir: product.sourceDirectory
      configure: {
        var file = new TextFile(sourceDir + "/version");
        var content = file.readLine();
        result = content;
      }
    }

    Depends { name: "cpp" }
    Depends { name: "gccClang" }
    Depends { name: "hardening" }
    Depends { name: "generateVersion" }

    cpp.warningLevel: "all"
    cpp.treatWarningsAsErrors: true
    cpp.includePaths: [".", buildDirectory + "/generated/"]

    cioVersionFile {
      prefix: product.sourceDirectory + "/"
    }

    Group {
      name: "version header"
      files: [
        "cio_version.h.in"
      ]
      fileTags: ["versionHeaderToPatch"]
    }

    Group {
      name: "ANSI C conformant"
      
      cpp.cLanguageVersion: "c99"
      
      files: [
        "*.c",
      ]
    
      Group {
        name: "public headers"
        files: ["*.h"]
        qbs.install: true
        qbs.installDir: "include"
      }
    }

    Group {
      condition: qbs.targetOS.contains("linux")
      name: "linux specific"
      prefix: "linux/"
      files: [
        "*.c",
      ]

      Group {
        name: "public linux headers"
        prefix: "linux/"
        files: ["*.h"]
        qbs.install: true
        qbs.installDir: "include/linux"
      }
    }

    Group {
      fileTagsFilter: product.type
      qbs.install: true
      qbs.installDir: "lib"
    }

    Group {
      fileTagsFilter: "versionHeader"
      qbs.install: true
      qbs.installDir: "include/"
    }
  }

  InstallPackage {
    archiver.type: "tar"
    archiver.archiveBaseName: "cio"
    name: "cio-linux"
    condition: qbs.targetOS.contains("linux")
    builtByDefault: true
    Depends { name: "cio-dynamic" }
    Depends { name: "cio-static" }
  }

  InstallPackage {
    archiver.type: "zip"
    archiver.archiveBaseName: "cio"
    name: "cio-win"
    condition: qbs.targetOS.contains("windows")
    builtByDefault: true
    Depends { name: "cio-dynamic" }
    Depends { name: "cio-static" }
  }
}
