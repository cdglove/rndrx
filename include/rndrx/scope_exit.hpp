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
#ifndef RNDRX_SCOPEEXIT_HPP_
#define RNDRX_SCOPEEXIT_HPP_
#pragma once

#include <utility>
#include "rndrx/noncopyable.hpp"

namespace rndrx {
template <typename Func>
class ScopeExit : noncopyable {
 public:
  ScopeExit(Func&& func)
      : func_(std::forward<Func>(func)) {
  }
  ~ScopeExit() {
    func_();
  }

 private:
  Func func_;
};

template <typename Func>
[[nodiscard]] auto on_scope_exit(Func&& f) {
  return ScopeExit<Func>(std::forward<Func>(f));
}

}
#endif // RNDRX_SCOPEEXIT_HPP_