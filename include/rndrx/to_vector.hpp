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
#ifndef RNDRX_TOVECTOR_HPP_
#define RNDRX_TOVECTOR_HPP_
#pragma once

#include <ranges>
#include <vector>

namespace rndrx {
struct to_vector_t {};
constexpr to_vector_t to_vector;

template <typename Range>
auto operator|(Range&& rng, to_vector_t) {
  return std::vector<std::ranges::range_value_t<Range>>(rng.begin(), rng.end());
}
} // namespace rndrx

#endif // RNDRX_TOVECTOR_HPP_