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
import qbs.Process
import '../../patchVersions.js' as Patch

Module {
  property bool createGraphs: false
  property string sourceDirectory

  Rule {
    inputs: ["doxy_version_patched", "source"];
    multiplex: "true";
    prepare: {
      var cmd = new JavaScriptCommand();
      cmd.description = "generating documentation from doxygen config";
      cmd.highlight = "doxygen";
      cmd.sourceCode = function() {
        for (var idx = 0; idx < inputs["doxy_version_patched"].length; idx++) {
          var file = inputs["doxy_version_patched"][idx].filePath;
          var proc = new Process();
          proc.setWorkingDirectory(product.generateDoxygen.sourceDirectory);
          proc.exec("doxygen", [file], true);
          proc.close();
        }
      }
      return cmd;
    }

    Artifact {
        fileTags: ["docs"];
        filePath: "force.doc";
    }
  }

  Rule {
    id: doxy_graph_option_patcher
    inputs:  ["doxy_input"]

    Artifact {
      filePath: "generated/Doxyfile.src.graph.in"
      fileTags: ["doxy_graph_option_patched"]
    }

    prepare: {
      var cmd = new JavaScriptCommand();
      cmd.description = "Processing '" + input.fileName + "'";
      cmd.highlight = "codegen";
      cmd.sourceCode = function() {
        var file = new TextFile(input.filePath);
        var content = file.readAll();
        file.close()
        var createGraphs = (input.moduleProperty("generateDoxygen","createGraphs") == true)? "YES" : "NO";
        content = content.replace("\${CIO_HAVE_DOT}", createGraphs);
        content = content.replace("\${CIO_SET_CALL_GRAPH}", createGraphs);
        content = content.replace("\${CIO_SET_CALLER_GRAPH}", createGraphs);
        file = new TextFile(output.filePath, TextFile.WriteOnly);
        file.truncate();
        file.write(content);
        file.close();
      }
      return cmd;
    }
  }

  Rule {
    id: doxy_outpath_patcher
    inputs:  ["doxy_graph_option_patched"]

    Artifact {
      filePath: "generated/Doxyfile.src.in"
      fileTags: ["doxyVersionFileToPatch"]
    }

    prepare: {
      var cmd = new JavaScriptCommand();
      cmd.description = "Processing '" + input.fileName + "'";
      cmd.highlight = "codegen";
      cmd.sourceCode = function() {
        var file = new TextFile(input.filePath);
        var content = file.readAll();
        file.close()
        content = content.replace(/\${CIO_BUILD_DIR}/g, product.buildDirectory);
        content = content.replace(/\${CIO_DOXY_INPUT}/g, product.generateDoxygen.sourceDirectory + " " + product.buildDirectory + "/generated/");
        content = content.replace(/\${CIO_DOXY_STRIP_FROM_PATH}/g, product.generateDoxygen.sourceDirectory + " " + product.buildDirectory + "/generated/");
        file = new TextFile(output.filePath, TextFile.WriteOnly);
        file.truncate();
        file.write(content);
        file.close();
      }
      return cmd;
    }
  }

  Rule {
    id: doxy_version_patcher
    multiplex: "true";
    inputs:  ["version_file", "doxyVersionFileToPatch"]

    Artifact {
      filePath: "generated/Doxyfile"
      fileTags: ["doxy_version_patched"]
    }

    prepare: Patch.patchVersion(inputs["doxyVersionFileToPatch"][0], inputs["version_file"][0], output, product)
  }
}
