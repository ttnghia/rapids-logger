/*
 * SPDX-FileCopyrightText: Copyright (c) 2024, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rapids_logger/logger.hpp>

// TODO: Check if the below issue persists
// This issue claims to have been resolved in gcc 8, but we still seem to encounter it here.
// The code compiles and links and all tests pass, and nm shows symbols resolved as expected.
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80947
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop

#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

namespace rapids_logger {

namespace detail {
namespace {

/**
 * @brief Convert a string to a log level.
 *
 * This function is used to process env-var specifications of log levels.
 * @param env_lvl_str The string to convert.
 * @return The log level.
 */
level_enum string_to_level(std::string_view const env_lvl_str)
{
  if (env_lvl_str == "TRACE") return level_enum::trace;
  if (env_lvl_str == "DEBUG") return level_enum::debug;
  if (env_lvl_str == "INFO") return level_enum::info;
  if (env_lvl_str == "WARN") return level_enum::warn;
  if (env_lvl_str == "ERROR") return level_enum::error;
  if (env_lvl_str == "CRITICAL") return level_enum::critical;
  if (env_lvl_str == "OFF") return level_enum::off;
  std::ostringstream os{};
  os << "Invalid logging level: " << env_lvl_str;
  throw std::invalid_argument(os.str());
}

/**
 * @brief Convert a log level to an spdlog log level.
 *
 * @param lvl The log level to convert.
 * @return The spdlog log level.
 */
spdlog::level::level_enum to_spdlog_level(level_enum lvl)
{
  return static_cast<spdlog::level::level_enum>(static_cast<int32_t>(lvl));
}

/**
 * @brief Convert an spdlog log level to a log level.
 *
 * @param lvl The spdlog log level to convert.
 * @return The log level.
 */
level_enum from_spdlog_level(spdlog::level::level_enum lvl)
{
  return static_cast<level_enum>(static_cast<int32_t>(lvl));
}
}  // namespace

/**
 * @brief The sink_impl class is a wrapper around an spdlog sink.
 *
 * This class is the impl part of the PImpl for the sink.
 */
class sink_impl {
 public:
  sink_impl(std::shared_ptr<spdlog::sinks::sink> sink) : underlying{sink} {}

 private:
  std::shared_ptr<spdlog::sinks::sink> underlying;
  // The sink_vector needs to be able to pass the underlying sink to the spdlog logger.
  friend class logger::sink_vector;
};

/**
 * @brief The logger_impl class is a wrapper around an spdlog logger.
 *
 * This class is the impl part of the PImpl for the logger.
 */
class logger_impl {
 public:
  logger_impl(std::string name) : underlying{spdlog::logger{name}}
  {
    // TODO: Every consuming library will need to set its own default levels and pattern
    // underlying.set_pattern(default_pattern());
    // when creating a default logger instance instead of setting the variables
    // in CMake to generate this.
    // auto const env_logging_level =
    //  std::getenv("RAPIDS_LOGGER_DEFAULT_LOGGING_LEVEL");
    // if (env_logging_level != nullptr) { set_level(detail::string_to_level(env_logging_level));
    // } auto const env_flush_level =
    // std::getenv("@_RAPIDS_LOGGER_MACRO_PREFIX@_DEFAULT_FLUSH_LEVEL"); if (env_flush_level !=
    // nullptr) { flush_on(detail::string_to_level(env_flush_level)); }
  }

  void log(level_enum lvl, std::string const& message)
  {
    underlying.log(to_spdlog_level(lvl), message);
  }
  void set_level(level_enum log_level) { underlying.set_level(to_spdlog_level(log_level)); }
  void flush() { underlying.flush(); }
  void flush_on(level_enum log_level) { underlying.flush_on(to_spdlog_level(log_level)); }
  level_enum flush_level() const { return from_spdlog_level(underlying.flush_level()); }
  bool should_log(level_enum lvl) const { return underlying.should_log(to_spdlog_level(lvl)); }
  level_enum level() const { return from_spdlog_level(underlying.level()); }
  void set_pattern(std::string pattern) { underlying.set_pattern(pattern); }
  const std::vector<spdlog::sink_ptr>& sinks() const { return underlying.sinks(); }
  std::vector<spdlog::sink_ptr>& sinks() { return underlying.sinks(); }

 private:
  spdlog::logger underlying;  ///< The spdlog logger
};

// Default flush function
void default_flush() { std::cout << std::flush; }

/**
 * @brief A sink that calls a callback function with log messages.
 *
 * We do not currently use spdlog's callback sink because it does not support
 * flushing. We could contribute that function to the upstream callback_sink_mt
 * to simplify this code.
 */
template <class Mutex>
class callback_sink : public spdlog::sinks::base_sink<Mutex> {
 public:
  explicit callback_sink(log_callback_t callback, flush_callback_t flush = nullptr)
    : _callback{callback}, _flush{flush ? flush : default_flush}
  {
  }

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override
  {
    spdlog::memory_buf_t formatted;
    spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
    std::string msg_string = std::string(formatted.data(), formatted.size());

    if (_callback) {
      _callback(static_cast<int>(msg.level), msg_string.c_str());
    } else {
      std::cout << msg_string;
    }
  }

  void flush_() override { _flush(); }

  log_callback_t _callback;
  void (*_flush)();
};

}  // namespace detail

// Sink vector functions
void logger::sink_vector::push_back(sink_ptr const& sink)
{
  sinks_.push_back(sink);
  parent.impl->sinks().push_back(sink->impl->underlying);
}
void logger::sink_vector::push_back(sink_ptr&& sink)
{
  sinks_.push_back(sink);
  parent.impl->sinks().push_back(sink->impl->underlying);
}
void logger::sink_vector::pop_back()
{
  sinks_.pop_back();
  parent.impl->sinks().pop_back();
}
void logger::sink_vector::clear()
{
  sinks_.clear();
  parent.impl->sinks().clear();
}

// Sink methods
sink::sink(std::unique_ptr<detail::sink_impl> impl) : impl{std::move(impl)} {}

sink::~sink() = default;

basic_file_sink_mt::basic_file_sink_mt(std::string const& filename, bool truncate)
  : sink{std::make_unique<detail::sink_impl>(
      std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, truncate))}
{
}

ostream_sink_mt::ostream_sink_mt(std::ostream& stream, bool force_flush)
  : sink{std::make_unique<detail::sink_impl>(
      std::make_shared<spdlog::sinks::ostream_sink_mt>(stream, force_flush))}
{
}

null_sink_mt::null_sink_mt()
  : sink{std::make_unique<detail::sink_impl>(std::make_shared<spdlog::sinks::null_sink_mt>())}
{
}

stderr_sink_mt::stderr_sink_mt()
  : sink{std::make_unique<detail::sink_impl>(std::make_shared<spdlog::sinks::stderr_sink_mt>())}
{
}

callback_sink_mt::callback_sink_mt(const log_callback_t& callback, const flush_callback_t& flush)
  : sink{std::make_unique<detail::sink_impl>(
      std::make_shared<detail::callback_sink<std::mutex>>(callback, flush))}
{
}

// Logger methods
logger::logger(std::string name, std::string filename)
  : impl{std::make_unique<detail::logger_impl>(name)}, sinks_{*this}
{
  sinks_.push_back(std::make_shared<basic_file_sink_mt>(filename, true));
}

logger::logger(std::string name, std::ostream& stream)
  : impl{std::make_unique<detail::logger_impl>(name)}, sinks_{*this}
{
  sinks_.push_back(std::make_shared<ostream_sink_mt>(stream));
}

logger::logger(std::string name, std::vector<sink_ptr> sinks)
  : impl{std::make_unique<detail::logger_impl>(name)}, sinks_{*this}
{
  for (auto const& s : sinks) {
    sinks_.push_back(s);
  }
}

logger::~logger()              = default;
logger::logger(logger&& other) = default;
logger& logger::operator=(logger&& other)
{
  impl = std::move(other.impl);
  sinks_.clear();
  for (auto const& s : other.sinks_) {
    sinks_.push_back(s);
  }
  return *this;
}

void logger::log(level_enum lvl, std::string const& message) { impl->log(lvl, message); }
void logger::set_level(level_enum log_level) { impl->set_level(log_level); }
void logger::flush() { impl->flush(); }
void logger::flush_on(level_enum log_level) { impl->flush_on(log_level); }
level_enum logger::flush_level() const { return impl->flush_level(); }
bool logger::should_log(level_enum lvl) const { return impl->should_log(lvl); }
level_enum logger::level() const { return impl->level(); }
void logger::set_pattern(std::string pattern) { impl->set_pattern(pattern); }
const logger::sink_vector& logger::sinks() const { return sinks_; }
logger::sink_vector& logger::sinks() { return sinks_; }

}  // namespace rapids_logger
