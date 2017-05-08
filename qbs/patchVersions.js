/*
 * The MIT License (MIT)
 *
 * Copyright (c) <2016> <Stephan Gatzka>
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

function patchVersion(inputs, output, product)
{
  var cmd = new JavaScriptCommand();
  cmd.description = "Processing '" + inputs["versionFileToPatch"][0].fileName + "'";
  cmd.highlight = "codegen";
  cmd.sourceCode = function() {
    var gitRevParse = new Process();
    gitRevParse.setWorkingDirectory(product.sourceDirectory);
    gitRevParse.exec("git", ["rev-parse","--short","HEAD"], true);
    var hash = gitRevParse.readLine();
    gitRevParse.close();

    var gitDirty = new Process();
    gitDirty.setWorkingDirectory(product.sourceDirectory);
    ret = gitDirty.exec("git", ["diff","--shortstat"], false);
    var out = gitDirty.readLine();
    var isDirty = true;
    if (out === null || out === "") {
      isDirty = false;
    }
    var dirty;
    if (isDirty) {
      dirty = ".dirty";
    } else {
      dirty = "";
    }
    gitDirty.close();

    var gitDescribe = new Process();
    gitDescribe.setWorkingDirectory(product.sourceDirectory);
    var ret = gitDescribe.exec("git", ["describe","--exact-match","--tags", "HEAD"], false);
    gitDescribe.close();
    var isTag = (ret === 0  && (isDirty === false));
    var preRelease;
    if (isTag) {
      preRelease = "";
    } else {
      preRelease = "-";
      var gitCount = new Process();
      gitCount.setWorkingDirectory(product.sourceDirectory);
      gitCount.exec("git", ["rev-list","HEAD","--count"], true)
      preRelease = preRelease + gitCount.readLine();
      gitCount.close();
    }

	var buildInfo = "+" + hash + dirty;

    var versionFile = new TextFile(inputs["version_file"][0].filePath);
    var versionString = versionFile.readAll().trim();
    versionFile.close()

    var file = new TextFile(inputs["versionFileToPatch"][0].filePath);
    var content = file.readAll();
    file.close()
    content = content.replace(/\${CIO_VERSION}/g, versionString);
    content = content.replace(/\${CIO_LAST}/g, preRelease + buildInfo);
    file = new TextFile(output.filePath,  TextFile.WriteOnly);
    file.truncate();
    file.write(content);
    file.close();
  }
  return  cmd;
}

