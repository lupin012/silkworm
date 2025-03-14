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

#include <random>

#include "singleton.hpp"

namespace silkworm {

class RandomNumber {
    std::mt19937_64 generator_;                      // the 64-bit Mersenne Twister 19937 generator
    std::uniform_int_distribution<uint64_t> distr_;  // a uniform distribution

  public:
    RandomNumber() {
        std::random_device rd;
        generator_.seed(rd());  // init generator_ with a random seed
    }

    RandomNumber(uint64_t a, uint64_t b) : distr_(a, b) {
        std::random_device rd;
        generator_.seed(rd());  // init generator_ with a random seed
    }

    uint64_t generate_one() { return distr_(generator_); }
};

#define RANDOM_NUMBER default_instantiating::Singleton<RandomNumber>::instance()

}  // namespace silkworm
