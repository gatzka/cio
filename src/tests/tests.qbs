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
import "../../qbs/unittestProduct.qbs" as UnittestProduct

Project {
  name: "cio unit tests"

  UnittestProduct {
    name: "test_cio_buffered_stream"
    type: ["application", "unittest"]
    
    cpp.includePaths: ["..", "../linux/"]

    files: [
      "test_cio_buffered_stream.c",
      "../cio_buffered_stream.c",
      "../linux/cio_linux_string.c",
    ]
  }

  UnittestProduct {
    name: "test_cio_http_server_iostream"
    type: ["application", "unittest"]
    
    cpp.includePaths: ["..", "../linux/"]
    cpp.driverFlags: ["-Wno-error"]
    
    files: [
      "test_cio_http_server_iostream.c",
      "../cio_buffered_stream.c",
      "../cio_http_location_handler.c",
      "../cio_http_location.c",
      "../cio_http_server.c",
    ]
  
    Group {
      name: "third party"
      cpp.cLanguageVersion: "c99"
      cpp.warningLevel: "none"
      files: [
        "../http-parser/http_parser.c",
        "../http-parser/http_parser.h"
      ]
    }
  
    Group {
      condition: qbs.targetOS.contains("linux")
      name: "linux specific"
      prefix: "../linux/"
      cpp.cLanguageVersion: "c99"
  
      files: [
        "cio_linux_string.c",
      ]
    }
  }

  UnittestProduct {
    name: "test_cio_http_server"
    type: ["application", "unittest"]
    
    cpp.includePaths: ["..", "../linux/"]
    cpp.driverFlags: ["-Wno-error"]
    
    files: [
      "test_cio_http_server.c",
      "../cio_http_location.c",
      "../cio_http_location_handler.c",
      "../cio_http_server.c",
    ]
  
    Group {
      name: "third party"
      cpp.cLanguageVersion: "c99"
      cpp.warningLevel: "none"
      files: [
        "../http-parser/http_parser.c",
        "../http-parser/http_parser.h"
      ]
    }
  }

  UnittestProduct {
    name: "test_cio_read_buffer"
    type: ["application", "unittest"]
    
    cpp.includePaths: ["..", "../linux/"]
    
    files: [
      "test_cio_read_buffer.c",
    ]
  }

  UnittestProduct {
    name: "test_cio_write_buffer"
    type: ["application", "unittest"]
    
    cpp.includePaths: ["..", "../linux/"]

    files: [
      "test_cio_write_buffer.c",
    ]
  }

  UnittestProduct {
    name: "test_cio_websocket_mask"
    type: ["application", "unittest"]
    
    cpp.includePaths: [".."]

    files: [
      "test_cio_websocket_mask.c",
      "../cio_websocket_masking.c"
    ]
  }
}
