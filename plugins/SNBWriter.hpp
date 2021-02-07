/**
 * @file SNBWriter.hpp
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#ifndef DFMODULES_PLUGINS_SNBWriter_HPP_
#define DFMODULES_PLUGINS_SNBWriter_HPP_

#include "dfmodules/DataStore.hpp"
#include "dfmodules/TriggerInhibitAgent.hpp"
#include "dfmodules/SNBHandler.hpp"

#include "appfwk/DAQModule.hpp"
#include "appfwk/DAQSource.hpp"
#include "appfwk/ThreadHelper.hpp"
#include "dataformats/TriggerRecord.hpp"

#include <memory>
#include <string>
#include <vector>

namespace dunedaq {
namespace dfmodules {

/**
 * @brief SNBWriter is a shell for what we might write for the MiniDAQApp.
 */
class SNBWriter : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief SNBWriter Constructor
   * @param name Instance name for this SNBWriter instance
   */
  explicit SNBWriter(const std::string& name);

  SNBWriter(const SNBWriter&) = delete;            ///< SNBWriter is not copy-constructible
  SNBWriter& operator=(const SNBWriter&) = delete; ///< SNBWriter is not copy-assignable
  SNBWriter(SNBWriter&&) = delete;                 ///< SNBWriter is not move-constructible
  SNBWriter& operator=(SNBWriter&&) = delete;      ///< SNBWriter is not move-assignable

  void init(const data_t&) override;

private:
  // Commands
  void do_conf(const data_t&);
  void do_start(const data_t&);
  void do_stop(const data_t&);
  void do_scrap(const data_t&);

  // Threading
  dunedaq::appfwk::ThreadHelper thread_;
  void do_work(std::atomic<bool>&);

  // Configuration
  // size_t sleepMsecWhileRunning_;
  std::chrono::milliseconds queueTimeout_;

  // Queue(s)
  using trigrecsource_t = dunedaq::appfwk::DAQSource<std::unique_ptr<dataformats::TriggerRecord>>;
  std::unique_ptr<trigrecsource_t> triggerRecordInputQueue_;

  // SNBHandler
  // AAA TODO: file_path_ , file_name_ and io_size 
  // should be taken from configuration file 
  char* m_membuffer = nullptr;
  // Block size 
  const size_t m_io_size = 1024*1048576; // 1024 MB
  // Allocation size 
  const size_t m_alloc_size = 1024*1048576; // 1024 MB
  const std::string m_file_path = "/mnt/micron1/";
  const std::string m_file_name = "output_link";
  // Prepare SNB data store
  SNBHandler* m_snb_data_store_1;
       

  // Worker(s)
  std::unique_ptr<DataStore> data_writer_;
  std::unique_ptr<TriggerInhibitAgent> trigger_inhibit_agent_;
};
} // namespace dfmodules

ERS_DECLARE_ISSUE_BASE(dfmodules,
                       InvalidSNBWriterError,
                       appfwk::GeneralDAQModuleIssue,
                       "A valid SNBWriter instance is not available so it will not be possible to write data. A "
                       "likely cause for this is a skipped or missed Configure transition.",
                       ((std::string)name),
                       ERS_EMPTY)

} // namespace dunedaq

#endif // DFMODULES_PLUGINS_SNBWriter_HPP_
