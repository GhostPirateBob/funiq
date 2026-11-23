package main

import (
	"bufio"
	"errors"
	"io"
	"runtime"
)

// Input lines as bytes (spec §3, Input): a line ends at \n, a last line
// without one still counts, and on Windows one trailing \r is dropped (what
// the C++ build's CRLF handling does); 0x1A and every other byte is data.

var stripCR = runtime.GOOS == "windows"

// lineReader reads through a 1 MiB buffer, like the C++ LineReader. Not
// bufio.Scanner: it strips \r everywhere and fails on lines over 64 KB.
type lineReader struct {
	r   *bufio.Reader
	buf []byte // a line longer than the reader's buffer is assembled here
}

func newLineReader(r io.Reader) *lineReader {
	return &lineReader{r: bufio.NewReaderSize(r, 1<<20)}
}

// next returns the next line without its terminator, or ok == false at the
// end of the input. The slice is valid only until the next call.
func (l *lineReader) next() (line []byte, ok bool, err error) {
	chunk, err := l.r.ReadSlice('\n')
	if errors.Is(err, bufio.ErrBufferFull) {
		l.buf = append(l.buf[:0], chunk...)
		for errors.Is(err, bufio.ErrBufferFull) {
			chunk, err = l.r.ReadSlice('\n')
			l.buf = append(l.buf, chunk...)
		}
		chunk = l.buf
	}
	switch {
	case errors.Is(err, io.EOF):
		if len(chunk) == 0 {
			return nil, false, nil
		}
	case err != nil:
		return nil, false, err
	}
	if n := len(chunk); n > 0 && chunk[n-1] == '\n' {
		chunk = chunk[:n-1]
	}
	if n := len(chunk); stripCR && n > 0 && chunk[n-1] == '\r' {
		chunk = chunk[:n-1]
	}
	return chunk, true, nil
}
