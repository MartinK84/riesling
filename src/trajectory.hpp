#pragma once

#include "info.hpp"
#include "log.h"
#include "types.h"

namespace rl {

struct Trajectory
{
  Trajectory();
  Trajectory(Info const &info, Re3 const &points);
  Trajectory(Info const &info, Re3 const &points, I1 const &frames);
  Info const &info() const;
  Re3 const &points() const;
  I1 const &frames() const;
  Re1 point(int16_t const read, int32_t const spoke) const;
  std::tuple<Trajectory, Index> downsample(float const res, Index const lores, bool const shrink) const;

private:
  void init();

  Info info_;
  Re3 points_;
  I1 frames_;
};

}