/*
 * The MIT License (MIT)
 *
 * Copyright (c) <2014> <Stephan Gatzka>
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

Product {
  name: "gccClang"

  Export {
    Depends { name: 'cpp' }

    cpp.cFlags: {
      var flags = [];

      if (qbs.toolchain.indexOf("clang") >= 0 || qbs.toolchain.indexOf("gcc") >= 0) {
        flags.push("-Wshadow");
        flags.push("-Winit-self");
        flags.push("-Wstrict-overflow=5");
        flags.push("-Wunused-result");
        flags.push("-Wcast-qual");
        flags.push("-Wcast-align");
        flags.push("-Wformat=2");
        flags.push("-Wwrite-strings");
        flags.push("-Wmissing-prototypes");
        flags.push("-pedantic");
        flags.push("-fno-common");
        if (qbs.buildVariant === "release") {
          flags.push("-fno-asynchronous-unwind-tables");
        }
      }
      return flags;
    }

    cpp.linkerFlags: {
      var flags = [];
      if (qbs.toolchain.indexOf("clang") >= 0 || qbs.toolchain.indexOf("gcc") >= 0) {
        flags.push("--hash-style=gnu");
        flags.push("--as-needed");
        if (qbs.buildVariant === "release") {
          flags.push("-O2");
          flags.push("--gc-sections");
          flags.push("-s");
        }
      }
      return flags;
    }
  }
}

