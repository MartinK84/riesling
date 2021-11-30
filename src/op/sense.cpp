#include "../tensorOps.h"
#include "../threads.h"
#include "sense.h"

SenseOp::SenseOp(Cx4 const &maps, Output::Dimensions const &bigSize)
  : maps_{maps}
{
  size_[0] = maps_.dimension(0);
  size_[1] = bigSize[1];
  size_[2] = maps_.dimension(1);
  size_[3] = maps_.dimension(2);
  size_[4] = maps_.dimension(3);
  full_[0] = maps_.dimension(0);
  full_[1] = bigSize[1];
  full_[2] = bigSize[2];
  full_[3] = bigSize[3];
  full_[4] = bigSize[4];
  left_[0] = right_[0] = 0;
  left_[1] = right_[1] = 0;
  left_[2] = (full_[2] - size_[2] + 1) / 2;
  left_[3] = (full_[3] - size_[3] + 1) / 2;
  left_[4] = (full_[4] - size_[4] + 1) / 2;
  right_[2] = (full_[2] - size_[2]) / 2;
  right_[3] = (full_[3] - size_[3]) / 2;
  right_[4] = (full_[4] - size_[4]) / 2;
}

Index SenseOp::channels() const
{
  return maps_.dimension(0);
}

SenseOp::InputDims SenseOp::inputDimensions() const
{
  return Sz4{size_[1], size_[2], size_[3], size_[4]};
}

SenseOp::OutputDims SenseOp::outputDimensions() const
{
  return full_;
}

void SenseOp::A(Input const &x, Output &y) const
{
  assert(x.dimension(1) == maps_.dimension(1));
  assert(x.dimension(2) == maps_.dimension(2));
  assert(x.dimension(3) == maps_.dimension(3));
  assert(y.dimension(0) == maps_.dimension(0));
  assert(y.dimension(1) == x.dimension(0));
  assert(y.dimension(2) == (maps_.dimension(1) + left_[2] + right_[2]));
  assert(y.dimension(3) == (maps_.dimension(2) + left_[3] + right_[3]));
  assert(y.dimension(4) == (maps_.dimension(3) + left_[4] + right_[4]));

  Eigen::IndexList<FixOne, int, int, int, int> resX;
  resX.set(1, x.dimension(0));
  resX.set(2, x.dimension(1));
  resX.set(3, x.dimension(2));
  resX.set(4, x.dimension(3));
  Eigen::IndexList<int, FixOne, FixOne, FixOne, FixOne> brdX;
  brdX.set(0, maps_.dimension(0));

  Eigen::IndexList<int, FixOne, int, int, int> resMaps;
  resMaps.set(0, maps_.dimension(0));
  resMaps.set(2, maps_.dimension(1));
  resMaps.set(3, maps_.dimension(2));
  resMaps.set(4, maps_.dimension(3));
  Eigen::IndexList<FixOne, int, FixOne, FixOne, FixOne> brdMaps;
  brdMaps.set(1, x.dimension(0));

  Eigen::array<std::pair<int, int>, 5> paddings;
  std::transform(
    left_.begin(), left_.end(), right_.begin(), paddings.begin(), [](Index left, Index right) {
      return std::make_pair(left, right);
    });

  y.device(Threads::GlobalDevice()) =
    (x.reshape(resX).broadcast(brdX) * maps_.reshape(resMaps).broadcast(brdMaps)).pad(paddings);
}

void SenseOp::Adj(Output const &x, Input &y) const
{
  assert(x.dimension(0) == maps_.dimension(0));
  assert(x.dimension(1) == y.dimension(0));
  assert(x.dimension(2) == (maps_.dimension(1) + left_[2] + right_[4]));
  assert(x.dimension(3) == (maps_.dimension(2) + left_[3] + right_[3]));
  assert(x.dimension(4) == (maps_.dimension(3) + left_[4] + right_[4]));
  assert(y.dimension(1) == maps_.dimension(1));
  assert(y.dimension(2) == maps_.dimension(2));
  assert(y.dimension(3) == maps_.dimension(3));

  Eigen::IndexList<int, FixOne, int, int, int> resMaps;
  resMaps.set(0, maps_.dimension(0));
  resMaps.set(2, maps_.dimension(1));
  resMaps.set(3, maps_.dimension(2));
  resMaps.set(4, maps_.dimension(3));
  Eigen::IndexList<FixOne, int, FixOne, FixOne, FixOne> brdMaps;
  brdMaps.set(1, x.dimension(1));

  y.device(Threads::GlobalDevice()) =
    ConjugateSum(x.slice(left_, size_), maps_.reshape(resMaps).broadcast(brdMaps));
}

void SenseOp::AdjA(Input const &x, Input &y) const
{
  y.device(Threads::GlobalDevice()) = x;
}
