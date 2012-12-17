#ifndef UTIL_STREAM_IO__
#define UTIL_STREAM_IO__

#include "util/exception.hh"

namespace util {
namespace stream {

class ChainPosition;

class ReadSizeException : public util::Exception {
  public:
    ReadSizeException() throw();
    ~ReadSizeException() throw();
};

class Read {
  public:
    explicit Read(int fd) : file_(fd) {}
    void Run(const ChainPosition &position); 
  private:
    int file_;
};

// Like read but uses pread so that the file can be accessed from multiple threads.  
class PRead {
  public:
    explicit PRead(int fd) : file_(fd) {}
    void Run(const ChainPosition &position);
  private:
    int file_;
};

class Write {
  public:
    explicit Write(int fd) : file_(fd) {}
    void Run(const ChainPosition &position);
  private:
    int file_;
};

class WriteAndRecycle {
  public:
    explicit WriteAndRecycle(int fd) : file_(fd) {}
    void Run(const ChainPosition &position);
  private:
    int file_;
};

} // namespace stream
} // namespace util
#endif // UTIL_STREAM_IO__