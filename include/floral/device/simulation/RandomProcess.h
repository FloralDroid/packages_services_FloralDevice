/*
 * Copyright 2026 FloralDroid
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <random>

namespace floral::device::simulation {

uint64_t GenerateSessionSeed();
uint64_t DeriveSeed(uint64_t master_seed, uint64_t stream_id);

class RandomStream final {
  public:
    explicit RandomStream(uint64_t seed);

    float Normal(float standard_deviation);
    float Uniform(float minimum, float maximum);

  private:
    std::mt19937_64 engine_;
    std::normal_distribution<float> normal_{0.0f, 1.0f};
};

// A first-order Gauss-Markov process produces continuous drift without making
// the result depend on how often a caller happens to read it.
class GaussMarkovProcess final {
  public:
    GaussMarkovProcess(uint64_t seed, float correlation_seconds, float standard_deviation,
                       float initial_value = 0.0f);

    float Advance(float elapsed_seconds, float mean = 0.0f);
    float value() const { return value_; }

  private:
    RandomStream random_;
    const float correlation_seconds_;
    const float standard_deviation_;
    float value_;
};

}  // namespace floral::device::simulation
