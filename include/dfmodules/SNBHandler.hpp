/**
 * @file SNBHandler.hpp
 *
 * Class to simplify usage of the AsyncIO library
 *  
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2021.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#ifndef DFMODULES_INCLUDE_DFMODULES_SNBHANDLER_HPP_
#define DFMODULES_INCLUDE_DFMODULES_SNBHANDLER_HPP_


// System
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <iostream>

#include "AsyncIO.hpp"
class SNBHandler {

public:
   SNBHandler(std::string path, size_t block_size, bool isRandom);
   ~SNBHandler() {}
   SNBHandler(const SNBHandler&) {}; // copy constructor
   SNBHandler& operator=(const SNBHandler&);
   //SNBHandler(SNBHandler&&);
   //SNBHandler& operator=(SNBHandler&&);


   int getFD();

   int getMaxSize();
   void init();
   void store(char* membuffer, bool test_finished, int coreID);   
   void getResults(std::shared_ptr<AsyncIO> asyncio, int elapsed_ms);
   void close(char* membuffer);

private:
   std::string m_path;
   size_t m_block_size = 0;
   bool m_isRandom;
   char* m_buffer;
   size_t m_sent_ops = 0;
   size_t m_completed_ops = 0;
   size_t file_offset = 0;
   std::unique_ptr<AsyncIO> m_asyncio;
   int m_fd;


};

#endif //DFMODULES_INCLUDE_DFMODULES_SNBHANDLER_HPP_
