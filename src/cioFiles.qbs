Group {
  name: "cio library files"

  cpp.warningLevel: "all"
  cpp.treatWarningsAsErrors: true
  cpp.includePaths: {
    var paths = [".", buildDirectory + "/generated/"];
    if (qbs.targetOS.contains("linux")) {
      paths.push("./linux/");
    }

    return paths;
  }

  Group {
    name: "version file"
    prefix: product.sourceDirectory + "/"
   
    files: [
      "version"
    ]
    fileTags: ["version_file"]
  }

  Group {
    name: "version header"
    files: [
      "cio_version.h.in"
    ]
    fileTags: ["versionFileToPatch"]
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
    name: "third party"
    cpp.cLanguageVersion: "c99"
    cpp.warningLevel: "none"
    files: [
      "http-parser/http_parser.c",
      "http-parser/http_parser.h",
      "sha1/sha1.c",
      "sha1/sha1.h",
    ]
  }

  Group {
    condition: qbs.targetOS.contains("linux")
    name: "linux specific"
    prefix: "linux/"
    cpp.cLanguageVersion: "c99"

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
