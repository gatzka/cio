#ifndef CIO_BUFFERED_STREAM_H
#define CIO_BUFFERED_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * @brief This file contains the definitions all users of a cio_buffered_stream
 * need to know.
 *
 * A cio_buffered_stream is always greedy to fill its internal read
 * buffer. Therefore, it greatly reduces the amount of read calls into
 * the underlying operating system.
 *
 * Additonally, a cio_buffered_stream gives you the concept of reading
 * an exact number of bytes or reading until a delimiter string is
 * encountered. Both concepts are useful when parsing protocols.
 *
 * A @p writev call into a buffered_stream only writes into the internal
 * write buffer. The write buffer is flushed only if the internal write
 * buffer is called or @p flush is called explicitly
 */

#ifdef __cplusplus
}
#endif

#endif
