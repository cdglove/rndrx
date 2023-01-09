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
#ifndef RNDRX_ASSERT_HPP_
#define RNDRX_ASSERT_HPP_

#ifndef RNDRX_ENABLE_ASSERT
#  ifndef NDEBUG
#    define RNDRX_ENABLE_ASSERT 1
#  else
#    define RNDRX_ENABLE_ASSERT 0
#  endif
#endif

#ifdef _MSC_VER
#  define DEBUG_BREAK __debugbreak()
#else
#  include <signal.h>
#  define DEBUG_BREAK raise(SIGTRAP)
#endif

#if RNDRX_ENABLE_ASSERT
#  define RNDRX_ASSERT(x)                                                      \
    do {                                                                       \
      if(!(x)) {                                                               \
        DEBUG_BREAK;                                                           \
      }                                                                        \
    } while(false)
#else
#  define RNDRX_ASSERT(x)                                                      \
    do {                                                                       \
    } while(false)
#endif

#endif // RNDRX_ASSERT_HPP_