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
#ifndef RNDRX_NONCOPYABLE_HPP_
#define RNDRX_NONCOPYABLE_HPP_

namespace rndrx {
struct noncopyable {
  noncopyable() = default;
  noncopyable(noncopyable const&) = delete;
  noncopyable& operator=(noncopyable const&) = delete;
  noncopyable(noncopyable&&) = default;
  noncopyable& operator=(noncopyable&&) = default;
};
} // namespace rndrx

#define RNDRX_DEFAULT_MOVABLE(T)                                               \
  ~T() = default;                                                              \
  T(T&&) = default;                                                            \
  T& operator=(T&&) = default;

#endif // RNDRX_NONCOPYABLE_HPP_