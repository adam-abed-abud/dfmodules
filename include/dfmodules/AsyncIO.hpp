/**
 * @file AsyncIO.hpp
 *
 * High level class to use the aio library
 *
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#pragma once

// system
#include <libaio.h>
#include <stdexcept>
#include <functional>
#include <unistd.h> // close
#include <fcntl.h> // open
#include <err.h> //err
#include <sys/types.h> // stat
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>


/**
 * Wrapper for libaio. It performs asynchronous file operations relying on the native kernel AIO interface.
 *
 * Basic use of this class:
 *   1) Create an instance of the class. e.g: AsyncIO asyncio = std::make_unique<AsyncIO>();
 *       You can specify here the maximum number of uncompleted operations to be supported (maxConcurrentOps)
 *   2) Request read or write operations using AsyncIO::read and AsyncIO::write methods, providing also a callback function.
 *      Libaio attempts to perform these operations asynchronously and in non-blocking mode. i.e. actual reads and writes will probably not be performed when these methods exit
 *      Actual reads and writes will be performed at some point in kernel space. When this occurs there is not notification to the caller (see retrieveCompletions below).
 *      If there are already maxConcurrentOps uncompleted operations, the call to read/write forces the retrieval of completed operations and can make the call to become blocking
 *   3) At some point, explicitly check for completed operations using AsyncIO::retrieveCompletions.
 *      The callbacks for the operations that completed will be executed in the user space.
 *
 *
 * The above methods require contiguous memory buffers to be provided and a file descriptor (only O_DIRECT allows non-blocking ops), which should be managed by the caller.
 * To ease the creation of the file descriptor, you can use the AsyncIO::openFile* helper methods (AsyncIO::closeFile)
 * To ease the allocation of contiguous memory, you can use the AsyncIO::memalloc/memfree helper methods
 *
 *  TODO:
 *  - Take it thead safe
 *  - handle operation errors (redo?)
 *  - check avoiding the kernel call to io_getevents (done by fio in user_io_getevents) : https://blog.cloudflare.com/io_submit-the-epoll-alternative-youve-never-heard-about/
 *  - implement sending vector of operations io_prep_pwritev
 *  - the check to avoid overflowing _completed_events is done in the critical path (on every read/write). How does it affect performance? can the user be smarter (and more performant)?
 *  - stat the file descriptor to retrieve parameters (_preferred_block_size, page_size, blockdevice or file system, etc). Once per fd (not every write), so take care of the file descriptor (open, close). Maybe many file descriptor, as the same io_context can write to several files
 *
 */

class AsyncIO {
/***
 * About libaio
 *
 * Performs asynchronous file operations relying on the native kernel AIO interface. Another similar library is the POSIX AIO library which relies on threads.
 * In most systems, libaio does not perform asynchronous operations unless using O_DIRECT. Using O_DIRECT implies: skipping page cache, data must be aligned and multiple of the block size.
 *
 *
 * The workflow for using libaio is:
 *  1 - (io_setup) Create a I/O context. This will be used to submit requests and get completion callbacks.
 *  2 - (io_prep_p*) Create as many request objects as needed. This represent the desired operation (read/write)
 *  3 - (io_submit) Submit the requests to the I/O context. This will effectively send the operations to the drive without blocking the thread
 *  4 - (io_getevents) Check event completion objects (completed operations) from the I/O context. This can be done at any time.
 *
 *  DISADVANTEGES:
 *    - need to implement callbacks manually
 *    - O_DIRECT: aligned memory and multiple of 512
 *    - without O_DIRECT: it uses pagecache
 *
 *   ADVANTAGES:
 *     - no threads
 *     - max throughput after 2 threads
 *
 *    Some links to read:
 *  - on libaio full async with O_DIRECT: https://bert-hubert.blogspot.com/2012/05/on-linux-asynchronous-file-io.html
 *  - explanation of functions: https://github.com/littledan/linux-aio
 *  - Another kernel option still WIP (io_uring): https://lwn.net/Articles/776703/
*/
        
  using CompletedCallback =  std::function<void()>; // callback type
  using libaio_operation = std::function<void(struct iocb*, int, void*, size_t , long long)>; // function type of io_prep_pwrite and io_prep_pread
      
  // Helper functions since libaio requires special conditions (O_DIRECT, aligned memory, block devices, etc) to work
  //   now the user of this class is responsible for memory allocation/deallocation and opening/closing files, but this helper functions do the job as expected by libaio
  public:
    static int openFileWriteOnly(const std::string& filename);
    static int openFileReadOnly(const std::string& filename);
    static int openFileReadWrite(const std::string& filename);
    static int closeFile(int fd);
        
    static void memalloc(void** mem, size_t alignment, size_t size_bytes);
    static void memfree(void* mem);
  private:
    static int _openFile(const std::string& filename, int flags);
        
  protected:
    struct callback_data{
      size_t io_size;
      char* buffer;
      CompletedCallback callback;
    };
        
    // parameters
    unsigned _max_concurrent_ops; // maximum number of concurrent operations that the context is able to hold
    unsigned device_block_size; 
        
    // state variables
    io_context_t _ctx; // async context to send and retrieve operations. See: http://man7.org/linux/man-pages/man2/io_setup.2.html
    struct io_event* _completed_events; // array of completed events, allocated only once
    unsigned _inflight_ops;
        
  public:
    AsyncIO(unsigned maxConcurrentOps = 128); // By default the max concurrent operations is 128
    ~AsyncIO();
        
    unsigned getMaxConcurrentOps(){ return _max_concurrent_ops;}
    unsigned getIncompleOps(){ return _inflight_ops;}
        
    /**
     * Submits a write operation.
     * retrieveCompletions() must be called later to check completions and get callbacks executed.
     * This operation could be blocking if _max_concurrent_ops is exceeded (until another operation completes)
     */
    void write(int fd, long long offset, const void* buffer, size_t io_size, CompletedCallback completed_callback);
        
    /**
     * Submits a read operation.
     * retrieveCompletions() must be called later to check completions and get callbacks executed.
     * This operation could be blocking if _max_concurrent_ops is exceeded (until another operation completes)
     */
    void read(int fd, long long offset, void* buffer, size_t io_size, CompletedCallback completed_callback);
        
    /**
     * Checks on completed operations (executing their callbacks).
     * To block until all operations completed:
     *     while(aio->getIncompleOps()!=0)    aio->retrieveCompletions();
     *
     */
    void retrieveCompletions();
        
    private:
      int _getOperations(size_t io_size);
      void _submit_operation(int fd, long long offset, void* buffer, size_t io_size, CompletedCallback completed_callback, libaio_operation operation);
      void _io_submit_error(int ret);
      void _io_event_error(int ret);
};


