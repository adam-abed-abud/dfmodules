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
  char* membuffer_ = nullptr;
  int m_num_producers = 2;
  size_t window_size_ = 5568;
  size_t io_size_ = 1048576;//1048576;//4194304;//2097152;//1048576;//194952;//1048576;
  size_t m_alloc_size = 1048576; //2097152; //10485760; //10 MB
  size_t frame_window_ = 10;
  std::string file_path_ = "output";
  

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
