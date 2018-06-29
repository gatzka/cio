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

Project {

  name: "unity"
  minimumQbsVersion: "1.6.0"

  Product {
    type: "staticlibrary"
    name: "unity"

    Depends { name: "cpp" }

    cpp.warningLevel: "all"
    cpp.treatWarningsAsErrors: true

    Group {
      name: "Unity sources"
      
      cpp.cLanguageVersion: "c99"
      
      prefix: "unity/src/"
      files: [
        "*.c",
        "*.h"
      ]
    }

    Export {
      Depends { name: 'cpp' }
      cpp.includePaths: ["./unity/src/", product.buildDirectory]
      cpp.dynamicLibraries: ["gcov"]
      cpp.driverFlags: {
        var flags = [];
        if (qbs.buildVariant === "debug") {
          flags.push("-fprofile-arcs");
          flags.push("-ftest-coverage");
        }

        return flags;
      }
    }
  }
}