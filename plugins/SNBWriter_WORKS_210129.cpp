/**
 * @file SNBWriter.cpp SNBWriter class implementation
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "SNBWriter.hpp"
#include "dfmodules/CommonIssues.hpp"
#include "dfmodules/KeyedDataBlock.hpp"
#include "dfmodules/StorageKey.hpp"
#include "dfmodules/snbwriter/Nljs.hpp"

#include "appfwk/DAQModuleHelper.hpp"
#include "dataformats/Fragment.hpp"
#include "dfmessages/TriggerDecision.hpp"
#include "dfmessages/TriggerInhibit.hpp"

#include "TRACE/trace.h"
#include "ers/ers.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/**
 * @brief Name used by TRACE TLOG calls from this source file
 */
#define TRACE_NAME "SNBWriter"                   // NOLINT
#define TLVL_ENTER_EXIT_METHODS TLVL_DEBUG + 5    // NOLINT
#define TLVL_CONFIG TLVL_DEBUG + 7                // NOLINT
#define TLVL_WORK_STEPS TLVL_DEBUG + 10           // NOLINT
#define TLVL_FRAGMENT_HEADER_DUMP TLVL_DEBUG + 17 // NOLINT
#define PAGE_SIZE 4096LL // NOLINT 

namespace dunedaq {
namespace dfmodules {

SNBWriter::SNBWriter(const std::string& name)
  : dunedaq::appfwk::DAQModule(name)
  , thread_(std::bind(&SNBWriter::do_work, this, std::placeholders::_1))
  , queueTimeout_(100)
  , triggerRecordInputQueue_(nullptr)
{
  register_command("conf", &SNBWriter::do_conf);
  register_command("start", &SNBWriter::do_start);
  register_command("stop", &SNBWriter::do_stop);
  register_command("scrap", &SNBWriter::do_scrap);
}

void
SNBWriter::init(const data_t& init_data)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering init() method";
  auto qi = appfwk::qindex(
    init_data, { "trigger_record_input_queue", "trigger_decision_for_inhibit", "trigger_inhibit_output_queue" });
  try {
    triggerRecordInputQueue_.reset(new trigrecsource_t(qi["trigger_record_input_queue"].inst));
  } catch (const ers::Issue& excpt) {
    throw InvalidQueueFatalError(ERS_HERE, get_name(), "trigger_record_input_queue", excpt);
  }

  using trigdecsource_t = dunedaq::appfwk::DAQSource<dfmessages::TriggerDecision>;
  std::unique_ptr<trigdecsource_t> trig_dec_queue_for_inh;
  try {
    trig_dec_queue_for_inh.reset(new trigdecsource_t(qi["trigger_decision_for_inhibit"].inst));
  } catch (const ers::Issue& excpt) {
    throw InvalidQueueFatalError(ERS_HERE, get_name(), "trigger_decision_for_inhibit", excpt);
  }
  using triginhsink_t = dunedaq::appfwk::DAQSink<dfmessages::TriggerInhibit>;
  std::unique_ptr<triginhsink_t> trig_inh_output_queue;
  try {
    trig_inh_output_queue.reset(new triginhsink_t(qi["trigger_inhibit_output_queue"].inst));
  } catch (const ers::Issue& excpt) {
    throw InvalidQueueFatalError(ERS_HERE, get_name(), "trigger_inhibit_output_queue", excpt);
  }
  trigger_inhibit_agent_.reset(
    new TriggerInhibitAgent(get_name(), std::move(trig_dec_queue_for_inh), std::move(trig_inh_output_queue)));

  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting init() method";
}

void
SNBWriter::do_conf(const data_t& payload)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_conf() method";

  snbwriter::ConfParams conf_params = payload.get<snbwriter::ConfParams>();
  trigger_inhibit_agent_->set_threshold_for_inhibit(conf_params.threshold_for_inhibit);
  TLOG(TLVL_CONFIG) << get_name() << ": threshold_for_inhibit is " << conf_params.threshold_for_inhibit;
  TLOG(TLVL_CONFIG) << get_name() << ": data_store_parameters are " << conf_params.data_store_parameters;

  // create the DataStore instance here
  data_writer_ = makeDataStore(payload["data_store_parameters"]);

  // Reserve aligned memory for SNB data store to write. Setting size of buffer to 1 MB.
  AsyncIO::memalloc(reinterpret_cast<void**>(&membuffer_), PAGE_SIZE, m_alloc_size);
  //memset(membuffer_, 'X', io_size_);

  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_conf() method";
}

void
SNBWriter::do_start(const data_t& /*args*/)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_start() method";
  trigger_inhibit_agent_->start_checking();
  thread_.start_working_thread();
  ERS_LOG(get_name() << " successfully started");
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_start() method";
}

void
SNBWriter::do_stop(const data_t& /*args*/)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_stop() method";
  trigger_inhibit_agent_->stop_checking();
  thread_.stop_working_thread();
  ERS_LOG(get_name() << " successfully stopped");
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_stop() method";
}

void
SNBWriter::do_scrap(const data_t& /*payload*/)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_scrap() method";

  // clear/reset the DataStore instance here
  data_writer_.reset();

  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_scrap() method";
}

void
SNBWriter::do_work(std::atomic<bool>& running_flag)
{
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Entering do_work() method";
  int32_t received_count = 0;

  // ensure that we have a valid snbwriter instance
  //if (data_writer_.get() == nullptr) {
  //  throw InvalidSNBWriterError(ERS_HERE, get_name());
  //}

  // Prepare SNB data store
  SNBHandler snb_data_store("/mnt/micron1/output.bin", io_size_, false);
  int fd = snb_data_store.getFD();
  //snb_data_store.store(fd, membuffer_, false, 1 );
  std::future<void> executing_thread; 
  size_t total_block_size = 0;

  while (running_flag.load()) {

    std::unique_ptr<dataformats::TriggerRecord> trigRecPtr;

    // receive the next TriggerRecord
    try {
      triggerRecordInputQueue_->pop(trigRecPtr, queueTimeout_);
      ++received_count;
      TLOG(TLVL_WORK_STEPS) << get_name() << ": Popped the TriggerRecord for trigger number "
                            << trigRecPtr->get_header().get_trigger_number() << " off the input queue";
    } catch (const dunedaq::appfwk::QueueTimeoutExpired& excpt) {
      // it is perfectly reasonable that there might be no data in the queue
      // some fraction of the times that we check, so we just continue on and try again
      continue;
    }


    // First store the trigger record header
    void* trh_ptr = trigRecPtr->get_header().get_storage_location();
    size_t trh_size = trigRecPtr->get_header().get_total_size_bytes(); 
    memcpy(membuffer_,(char*)trh_ptr, trh_size);
    snb_data_store.store(fd, membuffer_, false, 2 );
    //executing_thread = std::async(std::launch::async, &SNBHandler::store, &snb_data_store, fd, membuffer_, false, 2);
    //executing_thread.get();

    // Write the fragments
    const auto& frag_vec = trigRecPtr->get_fragments();
    for (const auto& frag_ptr : frag_vec) {
      void* data_block = frag_ptr->get_storage_location();
      size_t data_block_size = frag_ptr->get_size();
      //std::cout << data_block_size << "\n";  
      memcpy(membuffer_ , (char*)data_block, data_block_size);
      snb_data_store.store(fd, membuffer_, false, 2 );
    }
     
    // progress updates
    
    if ((received_count % 500) == 0) {
      std::ostringstream oss_prog;
      oss_prog << ": Processing trigger number " << trigRecPtr->get_header().get_trigger_number() << ", this is one of "
               << received_count << " trigger records received so far.";
      ers::log(ProgressUpdate(ERS_HERE, get_name(), oss_prog.str()));
    }
    

    // tell the TriggerInhibitAgent the trigger_number of this TriggerRecord so that
    // it can check whether an Inhibit needs to be asserted or cleared.
    trigger_inhibit_agent_->set_latest_trigger_number(trigRecPtr->get_header().get_trigger_number());
  }

  std::ostringstream oss_summ;
  oss_summ << ": Exiting the do_work() method, received trigger record messages for " << received_count << " triggers.";
  ers::log(ProgressUpdate(ERS_HERE, get_name(), oss_summ.str()));
  TLOG(TLVL_ENTER_EXIT_METHODS) << get_name() << ": Exiting do_work() method";
}

} // namespace dfmodules
} // namespace dunedaq

DEFINE_DUNE_DAQ_MODULE(dunedaq::dfmodules::SNBWriter)
