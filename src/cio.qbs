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
import "cioFiles.qbs" as CioFiles

Project {
  name: "cio libraries"
  minimumQbsVersion: "1.6.0"

  qbsSearchPaths: "../qbs/"

  references: [
    "../qbs/gccClang.qbs",
    "../qbs/hardening.qbs",
  ]

  Product {
    type: "staticlibrary"
    name: "cio-static"

    Depends { name: "cpp" }
    Depends { name: "gccClang" }
    Depends { name: "hardening" }
    Depends { name: "generateVersion" }

    CioFiles {}

    Export {
      Depends { name: "cpp" }

      cpp.includePaths: {
        var paths = [".", buildDirectory + "/generated/"];
        if (qbs.targetOS.contains("linux")) {
          paths.push("./linux/");
        }

        return paths;
      }
    }
  }

  Product {
    type: "dynamiclibrary"
    name: "cio-dynamic"

    Depends { name: "cpp" }
    Depends { name: "gccClang" }
    Depends { name: "hardening" }
    Depends { name: "generateVersion" }

    CioFiles {}

    Export {
      Depends { name: "cpp" }

      cpp.includePaths: {
        var paths = [".", buildDirectory + "/generated/"];
        if (qbs.targetOS.contains("linux")) {
          paths.push("./linux/");
        }

        return paths;
      }
    }
  }

  InstallPackage {
    archiver.type: "tar"
    archiver.archiveBaseName: "cio"
    name: "installation archive for linux"
    condition: qbs.targetOS.contains("linux")
    builtByDefault: true
    Depends { name: "cio-dynamic" }
    Depends { name: "cio-static" }
  }

  InstallPackage {
    archiver.type: "zip"
    archiver.archiveBaseName: "cio"
    name: "installation archive for windows"
    condition: qbs.targetOS.contains("windows")
    builtByDefault: true
    Depends { name: "cio-dynamic" }
    Depends { name: "cio-static" }
  }
}
