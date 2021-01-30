/**
 * @file AsyncIO.cpp
 *
 * High level class to use the aio library
 *
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

// Package
#include "dfmodules/AsyncIO.hpp"

// System
#include <iostream>
#include <memory>
#include <boost/log/trivial.hpp>
#include <sys/ioctl.h>
//#include <linux/aio_abi.h> //defines the io_setup and other components

#define THROW_ERROR(msg) do { std::stringstream ss; ss << msg; BOOST_LOG_TRIVIAL(fatal) << ss.str() ; throw std::runtime_error(ss.str()); } while(0);


AsyncIO::AsyncIO(unsigned maxConcurrentOps) : _ctx(0),
_max_concurrent_ops(maxConcurrentOps),
_inflight_ops(0){
    // setup the io context
    memset(&_ctx, 0, sizeof(_ctx));
    if (io_setup(_max_concurrent_ops, &_ctx) != 0) {
        THROW_ERROR("Libaio error: unable to io_setup");
    }
    
    _completed_events = new io_event[maxConcurrentOps]; // allocated here and deleted in destructor
}

AsyncIO::~AsyncIO() {
    delete[] _completed_events;
    if(_ctx) io_destroy(_ctx);
}

void AsyncIO::write(int fd, long long offset, const void* buffer, size_t io_size, CompletedCallback completed_callback){
    _submit_operation(fd, offset, const_cast<void*>(buffer), io_size,  completed_callback, io_prep_pwrite); // casting to be able to reuse the same method as read to perform the operation. But should not change the buffer if programmed correctly
}

void AsyncIO::read(int fd, long long offset, void* buffer, size_t io_size, CompletedCallback completed_callback){
    _submit_operation(fd, offset, buffer, io_size,  completed_callback, io_prep_pread);
}

void AsyncIO::_submit_operation(int fd, long long offset, void* buffer, size_t io_size, CompletedCallback completed_callback, libaio_operation operation){
    // if there no space for a new operation, block until at least one operation completes
    while (_inflight_ops + _getOperations(io_size) >= _max_concurrent_ops){ // TODO: might be better to block without burning CPU relying on io_getevents(_ctx, 1, _max_concurrent_ops, _completed_events, NULL); // see: http://man7.org/linux/man-pages/man2/io_getevents.2.html
        retrieveCompletions();
    }
    
    // 2- Prepare operation. see: https://manpages.debian.org/testing/libaio-dev/io_prep_pwrite.3.en.html
    struct iocb iocb;
    struct iocb* iocbs = &iocb;
    operation(&iocb, fd, buffer, io_size, offset);
    iocb.data = (void *) new callback_data{io_size, (char*) buffer, completed_callback}; // TODO: check if it is possible to avoid this allocation. Pool of callbacks if needed ;
    
    // 3 - submit operation. see: http://man7.org/linux/man-pages/man2/io_submit.2.html
    if (int ret; (ret = io_submit(_ctx, 1, &iocbs)) != 1) { // TODO: verify the use of flags, e.g.: RWF_HIPRI and RWF_NOWAIT
        _io_submit_error(ret);
    }
    _inflight_ops += _getOperations(io_size);
}

void AsyncIO::retrieveCompletions(){
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 0; // do not block waiting
    int    completed = io_getevents(_ctx, 0, _max_concurrent_ops, _completed_events, &timeout); // see: http://man7.org/linux/man-pages/man2/io_getevents.2.html
    
    // check for errors and execute the completed callback
    for (int i = 0; i < completed; ++i) {
        struct io_event event = _completed_events[i];
        auto* data = (callback_data*) event.data;
        // TODO: this should be treated as a bad error, but we need to investigate why it occurs and how to handle it
        if(event.res != data->io_size) {
            // TODO: this error occurs rarely, when io_size > PREFERED_BLOCK_SIZE. Try to partition in several operations to avoid errors. Use io_prep_pwriteV
            THROW_ERROR("[AsyncIO::retrieveCompletions]: Operation incomplete or some error occurred error (error code=" << (int) event.res <<
                             "). data->io_size=" << data->io_size << "   data->buffer=" << (void*) data->buffer);
            _io_event_error(event.res);
            
        }
// EF commented error if writing did not suceed
        
        // execute callback
        data->callback();
        
        _inflight_ops -= _getOperations(data->io_size);
        delete data; // TODO: check if it is possible to avoid this delete
    }
}

int AsyncIO::_getOperations(size_t io_size){
    auto preferred_block_size = 16384*5; // TODO: this should be retrieved from stat or device
    if(io_size <= preferred_block_size) return 1;
    return io_size / preferred_block_size;
}

void AsyncIO::_io_submit_error(int ret){
    if(ret == EAGAIN) THROW_ERROR("io_submit: Insufficient resources are available to queue any iocbs.");
    if(ret == EBADF) THROW_ERROR("io_submit: The file descriptor specified in the first iocb is invalid");
    if(ret == EFAULT) THROW_ERROR("io_submit: One of the data structures points to invalid data.");
    if(ret == EINVAL) THROW_ERROR("io_submit: The AIO context specified by ctx_id is invalid.  nr is less than 0.  The iocb at *iocbpp[0] is not properly initialized, the operation specified is invalid for the file descriptor in the iocb, or the value in the aio_reqprio field is invalid.");
    if(ret == ENOSYS) THROW_ERROR("io_submit() is not implemented on this architecture.");
    if(ret == EPERM) THROW_ERROR("io_submit: The aio_reqprio field is set with the class IOPRIO_CLASS_RT, but the submitting context does not have the CAP_SYS_ADMIN capability.");
    THROW_ERROR("io_submit: Unknown error " + std::to_string(ret));
}

void AsyncIO::_io_event_error(int ret){
    ret = -ret; // libaio negates errors numbers!
    if(ret == EAGAIN) THROW_ERROR("io_event: Try again");
    if(ret == EBADF) THROW_ERROR("io_event: Bad file number");
    if(ret == EFAULT) THROW_ERROR("io_event: Bad address");
    if(ret == EINVAL) std::cerr << "io_event: Invalid arguments. Eg: is the offset and io_size multiple of page size?";
    else std::cerr << "io_event: Unknown error " << ret << std::endl;
    err(ret, "io_event");
}



///////// static functions /////////
int AsyncIO::closeFile(int fd) {
	int ret = close(fd);
	if (ret != 0) err(1, "Error in AsyncIO::closeFile - calling close in file descriptor");

	return ret;
}
int AsyncIO::_openFile(const std::string& filename, int flags){
    int fd = open(filename.c_str(), flags, 0664);
/*
 * AAA: 20/10/20 - get the device block size from file descriptor
 *
   int device_block_size;
   std::cout << "BENZINA: " << *device_block_size << " \n\n\n\n"; 


#ifdef BLKSSZGET
int blkdev_get_sector_size(int fd, int *sector_size)
{
    if (ioctl(fd, BLKSSZGET, sector_size) >= 0)
        return 0;
    return -1;
}
#else
int blkdev_get_sector_size(int fd __attribute__((__unused__)), int *sector_size)
{
    *sector_size = DEFAULT_SECTOR_SIZE;
    return 0;
}
#endif




*/

    if (fd < 0) err(1, ("open " + filename).c_str());
    
    return fd;
}
int AsyncIO::openFileWriteOnly(const std::string& filename){ return _openFile(filename, O_WRONLY | O_DIRECT | O_CREAT/*| O_NOATIME <-- requires owner permissions on block devices*/); }
int AsyncIO::openFileReadOnly(const std::string& filename){ return _openFile(filename, O_RDONLY | O_DIRECT); }
int AsyncIO::openFileReadWrite(const std::string& filename){ return _openFile(filename, O_RDWR | O_DIRECT | O_CREAT); };

void AsyncIO::memfree(void* mem){ free(mem);}
void AsyncIO::memalloc(void** mem, size_t alignment, size_t size_bytes){
    // see http://man7.org/linux/man-pages/man3/posix_memalign.3.html
    if (int ret; (ret = posix_memalign(mem, alignment, size_bytes)) != 0){
        if(ret == EINVAL) THROW_ERROR("posix_memalign failed! The alignment argument (" + std::to_string(alignment) + ") was not a power of two, or was not a multiple of sizeof(void *)=" + std::to_string(sizeof(void *)));
        if(ret == ENOMEM) THROW_ERROR("posix_memalign failed! There was insufficient memory to fulfill the allocation request.");
        err(1, "posix_memalign");
        THROW_ERROR("posix_memalign failed! Unknown error.");
    }
}

