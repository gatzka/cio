/*
 *The MIT License (MIT)
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
import 'versions.js' as Versions

Product {
  name: "hardening"

  property bool enableHardening: true

  Export {
    Depends { name: 'cpp' }
    cpp.defines: {
      var defines = [];
      if (product.enableHardening) {
        var toolchain = qbs.toolchain[0];
        if ((toolchain === "gcc" || toolchain === "clang") && (qbs.buildVariant === "release")) {
          defines.push("_FORTIFY_SOURCE=2");
        }
      }
      return defines;
    }

    cpp.driverFlags: {
      var flags = [];
      if (product.enableHardening) {
        var toolchain = qbs.toolchain[0];
        var compilerVersion = cpp.compilerVersionMajor + "." + cpp.compilerVersionMinor + "." + cpp.compilerVersionPatch;
        if (((toolchain === "gcc") && (Versions.versionIsAtLeast(compilerVersion, "4.9"))) ||
            ((toolchain === "clang") && (Versions.versionIsAtLeast(compilerVersion, "3.5")))) {
          flags.push("-fstack-protector-strong", "-fpie");
        }
        if (toolchain === "gcc") {
          flags.push("-pie");
        }
      }
      return flags;
    }

    cpp.linkerFlags: {
      var flags = [];
      if (product.enableHardening) {
        var toolchain = qbs.toolchain[0];
        var compilerVersion = cpp.compilerVersionMajor + "." + cpp.compilerVersionMinor + "." + cpp.compilerVersionPatch;
        if (((toolchain === "gcc") && (Versions.versionIsAtLeast(compilerVersion, "4.9"))) ||
            ((toolchain === "clang") && (Versions.versionIsAtLeast(compilerVersion, "3.5")))) {
          flags.push("-z","relro");
          flags.push("-z","now");
          flags.push("-z","noexecstack");
        }
      }
      return flags;
    }
  }
}

