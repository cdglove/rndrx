// Copyright (c) 2022 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef RNDRX_LOG_HPP_
#define RNDRX_LOG_HPP_

#include "rndrx/noncopyable.hpp"
#include <iostream>

namespace rndrx {
enum class LogLevel {
  None,
  Error,
  Warn,
  Info,
  Trace,
  NumLogLevels,
};

LogLevel constexpr g_LogLevel = LogLevel::Trace;
struct LogState : noncopyable {
  explicit LogState(std::ostream& os)
      : os_(os) {
  }

  bool is_done() const {
    return done_;
  }

  void done() {
    done_ = true;
    os_ << std::endl;
  }

  std::ostream& os_;
  bool done_ = false;
};

} // namespace rndrx

#define LOG(level)                                                             \
  for(rndrx::LogState log_state(std::cerr);                                    \
      rndrx::g_LogLevel >= rndrx::LogLevel::level && !log_state.is_done();     \
      log_state.done())                                                        \
  std::cerr

#endif // RNDRX_LOG_HPP_