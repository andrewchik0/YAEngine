#pragma once

#include <random>
#include <limits>

namespace YAEngine
{
  template<typename T>
  T random(T rangeFrom, T rangeTo)
  {
    std::random_device randDev;
    std::mt19937 generator(randDev());
    std::uniform_int_distribution<T> distr(rangeFrom, rangeTo);
    return distr(generator);
  }

  template<typename T>
  T random(T rangeFrom)
  {
    return random(rangeFrom, std::numeric_limits<T>::max());
  }

  template<typename T>
  T random()
  {
    return random(static_cast<T>(0), std::numeric_limits<T>::max());
  }
}
