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
#ifndef RNDRX_FRAMEGRAPH_HPP_
#define RNDRX_FRAMEGRAPH_HPP_
#pragma once

#include "rndrx/config.hpp"
#include <memory>

namespace rndrx {

class FrameGraphDescription;
class FrameGraph {
 public:
  static std::unique_ptr<FrameGraph> create(FrameGraphDescription const& desc);

  //RNDRX_MAYBE_VIRTUAL void render();
};



} // namespace rndrx

#endif // RNDRX_FRAMEGRAPH_HPP_