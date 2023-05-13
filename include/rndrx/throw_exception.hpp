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
#ifndef RNDRX_THROWEXCEPTION_HPP_
#define RNDRX_THROWEXCEPTION_HPP_
#pragma once

#include <stdexcept>

namespace rndrx {
template <typename Ex>
[[noreturn]] void throw_exception(Ex e) {
  throw e;
}

[[noreturn]] inline void throw_runtime_error(char const* format) {
  throw_exception(std::runtime_error(format));
}

#define RNDRX_THROW_RUNTIME_ERROR() \
  for(std::stringstream msg;;rndrx::throw_runtime_error(msg.str().c_str())) \
    msg

} // namespace rndrx

#endif // RNDRX_THROWEXCEPTION_HPP_