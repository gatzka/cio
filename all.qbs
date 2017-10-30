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

import "qbs/unittestRunner.qbs" as UnittestRunner

Project {
  name: "cio with tests and documentation"
  minimumQbsVersion: "1.4.0"

  qbsSearchPaths: "qbs/"

  references : [
    //"qbs/gccClang.qbs",
    //"qbs/hardening.qbs",

  ] 

  Project {
    name: "library"
    references: [
      "src/cio.qbs"
    ]
  }

  Project {
    name: "examples"
    references : [
      "examples/periodic_timer.qbs",
      "examples/http_server.qbs",
      "examples/socket_echo.qbs"
    ]
  }

  Project {
    name: "tests"
    references : [
      "src/unity.qbs",
      "src/fff.qbs",
      "src/unittestsettings.qbs",

      "src/tests/test_cio_buffered_stream.qbs",
      "src/tests/test_cio_http_server.qbs",
      "src/tests/test_cio_read_buffer.qbs",
      "src/tests/test_cio_write_buffer.qbs",
    ] 

    Project {
      name: "linux"
      references : [
        "src/linux/tests/test_cio_linux_epoll.qbs",
        "src/linux/tests/test_cio_linux_server_socket.qbs",
        "src/linux/tests/test_cio_linux_socket.qbs",
        "src/linux/tests/test_cio_linux_socket_utils.qbs",
        "src/linux/tests/test_cio_linux_timer.qbs",
      ] 
    }
  }

  SubProject {
    filePath: "src/cio_doc.qbs"
  }

  UnittestRunner {
    lcovRemovePatterns: [
      "*/tests/*",
      "*/http-parser/*",
      "*/linux/tests/*",
      "/usr/include/*",
    ]
    wrapper: [
      "valgrind",
      "--errors-for-leak-kinds=all",
      "--show-leak-kinds=all",
      "--leak-check=full",
      "--error-exitcode=1",
    ]
  }
}

