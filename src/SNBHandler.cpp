/**
 * @file SNBHandler.cpp
 *
 * Class to simplify usage of the AsyncIO library
 *  
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

// Std
#include <memory>
#include <random> // for getRandom
#include <chrono>
#include <iostream>
#include <fstream>

// Packages 
#include "dfmodules/SNBHandler.hpp"

#define PREFERED_BLOCK_SIZE 4096LL // used to align device offset
#define PAGE_SIZE 4096LL // used to align memory

// Leave at least 1MB without writing (partition table?)
//constexpr uint MIN_OFFSET = 1*1024*1024; 
// Leave at least 4 KiB without writing (partition table?)
#define MIN_OFFSET 4096LL 



// Random generator
// Get a random number between min and max
long long int getRandom(long long min, long long max){
  std::random_device rd;     //Get a random seed from the OS entropy device
  std::mt19937_64 gen(rd()); //Use the 64-bit Mersenne Twister 19937 generator and seed it with entropy.
  std::uniform_int_distribution<long long> rnd_generator; // random generator
  return rnd_generator(gen) % (max-min+1) + min;
}


// Clear PageCache, dentries and inodes.
void drop_cache() {
  int fd;
  sync();
  fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
  write(fd, "3", 1);
  close(fd);
}



// Set CPU affinity of the processing thread
void SetAffinityThread(int executorId) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(executorId, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
       std::cerr << "Error calling pthread_setaffinity_np Readout: " << rc << "\n";
    }
}



// Constructor and initialization
SNBHandler::SNBHandler(std::string path, size_t block_size, bool isRandom) {
  m_path = path;
  m_block_size = block_size;
  m_isRandom = isRandom;
}


// Get file descriptor
int SNBHandler::getFD() {
    int fd = AsyncIO::openFileWriteOnly(m_path);
    return fd;
}


// Get the maximum writable space of the block device
int SNBHandler::getMaxSize() {
  //int fd = getFD();
  auto max_file_size_bytes = lseek(m_fd, 0, SEEK_END);
  lseek(m_fd, 0, SEEK_SET);
  if(max_file_size_bytes <= 0){
      throw std::runtime_error("File size is 0. If you are working on a filesystem, pre-allocate the file \n(eg: truncate -s 512M " + m_path + ")");
  } 
  
  return max_file_size_bytes; 
}


// Initialize the libaio wrapper
void SNBHandler::init() {
  m_fd = getFD(); 
  drop_cache();
  std::ofstream ofs(m_path, std::ios::trunc);
  truncate(m_path.data(), 0);

  // Create the libaio wrapper
  m_asyncio = std::make_unique<AsyncIO>(128);
}



// Store a given memory buffer
void SNBHandler::store(char* membuffer, bool test_finished, int coreID) {

  SetAffinityThread(coreID);


  // Offset to write on disk
  // Sequential write
  file_offset = MIN_OFFSET + (m_sent_ops * m_block_size) ;
 
  
  // Perform operation (read or write)
  auto callback = [&]() {    
    if(test_finished) {
      std::cout << " Operation completed \n";
      return;
    } else {
      m_completed_ops++;
      // std::cout << m_completed_ops << " completed ops. Buffer=" << membuffer[0] << " ;  file_offset=" << file_offset << std::endl;
    }
  };
  
  m_asyncio->write(m_fd, file_offset, membuffer, m_block_size, callback);
  m_sent_ops++;
  
  // retrieve completed operations
  m_asyncio->retrieveCompletions();    

  
  // First check if the requests have finished
  while(m_asyncio->getIncompleOps()!= 0) {
    m_asyncio->retrieveCompletions();
  }   
}




void SNBHandler::getResults( std::shared_ptr<AsyncIO> asyncio, int elapsed_ms ){

  // First check if the requests have finished
  while(asyncio->getIncompleOps()!= 0) {
    asyncio->retrieveCompletions();
  }   
 

}

void SNBHandler::close(char* membuffer){
  AsyncIO::memfree(membuffer);
  AsyncIO::closeFile(m_fd);
}





