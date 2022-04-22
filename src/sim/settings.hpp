#pragma once

#include "types.h"

namespace rl {

struct Settings
{
  Index sps, gps;
  float alpha, TR, Tramp, Tssi, TI, Trec, TE, bval;
};

} // namespace rl