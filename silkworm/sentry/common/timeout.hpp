/*
   Copyright 2022 The Silkworm Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#pragma once

#include <chrono>
#include <stdexcept>

#include <silkworm/node/concurrency/coroutine.hpp>

#include <boost/asio/awaitable.hpp>

namespace silkworm::sentry::common {

class Timeout {
  public:
    explicit Timeout(std::chrono::milliseconds duration) : duration_(duration) {}

    Timeout(const Timeout&) = delete;
    Timeout& operator=(const Timeout&) = delete;

    boost::asio::awaitable<void> schedule() const;
    boost::asio::awaitable<void> operator()() const { return schedule(); }

    static boost::asio::awaitable<void> after(std::chrono::milliseconds duration) {
        Timeout timeout(duration);
        co_await timeout.schedule();
    }

    class ExpiredError : public std::runtime_error {
      public:
        ExpiredError() : std::runtime_error("Timeout has expired") {}
    };

  private:
    std::chrono::milliseconds duration_;
};

}  // namespace silkworm::sentry::common
