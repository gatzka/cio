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
import qbs.Process

Product {
  name: "clang format"
  type: ["clangformat-result"]

  Group {
    files: ["*.h", "*.c"];
    fileTags: "source";

    Group {
      files: ["*.h", "*.c"];
      prefix: "linux/"
    }

    Group {
      files: ["*.h", "*.c"];
      prefix: "tests/"
    }

    Group {
      files: ["*.h", "*.c"];
      prefix: "linux/tests/"
    }
  }

  Rule {
    multiplex: false

    inputs: ["source"]

    Artifact {
      filePath: input.filePath + ".result.dummy" // Will never exist.
      fileTags: "clangformat-result"
      alwaysUpdated: false
    }

    prepare: {
      var cmd = new JavaScriptCommand();
      cmd.description = "formatting file " + input.filePath
      cmd.highlight = "codegen"
      cmd.sourceCode = function() {
        var proc = new Process();
        proc.setWorkingDirectory(product.sourceDirectory);
        proc.exec("clang-format", ["-style=file", "-i", input.filePath], true);
        proc.close();
      }
      return [cmd];
    }
  }
}
