#pragma once

#include "Pch.h"

namespace YAEngine
{
  template<typename T>
  T Random(T rangeFrom, T rangeTo)
  {
    static std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<T> distr(rangeFrom, rangeTo);
    return distr(generator);
  }

  template<typename T>
  T Random(T rangeFrom)
  {
    return Random(rangeFrom, std::numeric_limits<T>::max());
  }

  template<typename T>
  T Random()
  {
    return Random(static_cast<T>(0), std::numeric_limits<T>::max());
  }
}
