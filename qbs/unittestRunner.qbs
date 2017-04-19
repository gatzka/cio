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
import qbs.ModUtils
import qbs.Utilities

Product {
  name: "unittestRunner"
  type: ["unittest-result"]

  property stringList arguments: []
  property stringList environment: ModUtils.flattenEnvironmentDictionary(qbs.commonRunEnvironment)
  property stringList wrapper: []
  property stringList lcovExtractPatterns: []
  property stringList lcovRemovePatterns: []
  property bool showCoverageData: false

  Depends {
    productTypes: "unittest"
  }

  Rule {
    multiplex: true
    inputsFromDependencies: "application"

    Artifact {
      filePath: Utilities.getHash(inputs.application[0].filePath) + ".result.dummy" // Will never exist.
      fileTags: "unittest-result"
      alwaysUpdated: false
    }

    prepare: {
      if ((product.lcovRemovePatterns.length > 0) && (product.lcovExtractPatterns.length > 0)) {
        throw "Only either covRemovePatterns or lcovExtractPatterns can be set! "
      }

      var cmds = [];
      var args = [
        "--zerocounters",
        "--directory", project.buildDirectory,
        "--quiet",
        "--rc", "lcov_branch_coverage=1",
      ];

      var cmd = new Command("lcov", args);
      cmd.description = "Resetting coverage data...";
      cmds.push(cmd);

      for (var i = 0; i < inputs.application.length; i++) {
        var fullCommandLine = product.wrapper
          .concat([inputs.application[i].filePath])
          .concat(product.arguments);
        var cmd = new Command(fullCommandLine[0], fullCommandLine.slice(1));
        cmd.description = "Running test " + inputs.application[i].fileName;
        cmd.environment = product.environment;
        cmds.push(cmd);
      }

      var coverageFile = project.buildDirectory + "/coverage.info";
      var filteredCoverageFile = project.buildDirectory + "/filtered-coverage.info";
      var args = [
        "--capture",
        "--directory", project.buildDirectory,
        "--quiet",
        "--output-file", coverageFile,
        "--rc", "lcov_branch_coverage=1",
      ];
      var cmd = new Command("lcov", args);
      cmd.description = "Collecting coverage data...";
      cmds.push(cmd);

      if ((product.lcovRemovePatterns.length > 0) || (product.lcovExtractPatterns.length > 0)) {
        var args = [];
        if (product.lcovExtractPatterns.length > 0) {
          args.push("--extract");
          args.push(coverageFile);
          for (var i = 0; i < product.lcovExtractPatterns.length; i++) {
            args.push(product.lcovExtractPatterns[i]);
          }
        }
        if (product.lcovRemovePatterns.length > 0) {
          args.push("--remove");
          args.push(coverageFile);
          for (var i = 0; i < product.lcovRemovePatterns.length; i++) {
            args.push(product.lcovRemovePatterns[i]);
          }
        }
        args.push("--output-file");
        args.push(filteredCoverageFile);
        args.push("--rc");
        args.push("lcov_branch_coverage=1");
        var cmd = new Command("lcov", args);
        cmd.description = "Filtering coverage data...";
        cmds.push(cmd);
        coverageFile = filteredCoverageFile;
      }

      var reportDir = project.buildDirectory + "/coverage/report";
      var args = [
        "--quiet",
        coverageFile,
        "--branch-coverage",
        "-o", reportDir,
        "-t", project.name + " " + product.moduleProperty("qbs", "buildVariant")
      ];

      var cmd = new Command("genhtml", args);
      cmd.description = "Creating html from coverage data...";
      cmd.workingDirectory = project.buildDirectory;
      cmds.push(cmd);

      if (product.showCoverageData) {
        var args = [
          reportDir + "/index.html"
        ];
        var cmd = new Command("xdg-open", args);
        cmd.description = "Show coverage data...";
        cmds.push(cmd);
      }
      return cmds;
    }
  }
}
