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
import "../../../qbs/unittestProduct.qbs" as UnittestProduct

Project {
  name: "cio linux unit tests"
  minimumQbsVersion: "1.6.0"

  condition: qbs.targetOS.contains("linux")

  UnittestProduct {
    name: "test_cio_linux_epoll"
    type: ["application", "unittest"]

    cpp.includePaths: ["../", "../../"]

    files: [
      "test_cio_linux_epoll.c",
      "../cio_linux_epoll.c",
    ]
  }

  UnittestProduct {
    name: "test_cio_linux_server_socket"
    type: ["application", "unittest"]

    cpp.includePaths: ["../", "../../"]
  
    files: [
      "test_cio_linux_server_socket.c",
      "../cio_linux_server_socket.c"
    ]
  }

  UnittestProduct {
    name: "test_cio_linux_socket"
    type: ["application", "unittest"]

    cpp.includePaths: ["../", "../../"]

    files: [
      "test_cio_linux_socket.c",
      "../cio_linux_socket.c",
    ]
  }

  UnittestProduct {
    name: "test_cio_linux_socket_utils"
    type: ["application", "unittest"]

    cpp.includePaths: ["../", "../../"]

    files: [
      "test_cio_linux_socket_utils.c",
      "../cio_linux_socket_utils.c",
    ]
  }

  UnittestProduct {
    name: "test_cio_linux_timer"
    type: ["application", "unittest"]

    cpp.includePaths: ["../", "../../"]

    files: [
      "test_cio_linux_timer.c",
      "../cio_linux_timer.c",
    ]
  }
}