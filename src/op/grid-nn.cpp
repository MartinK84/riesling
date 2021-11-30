#include "grid-nn.h"

#include "../tensorOps.h"
#include "../threads.h"

#include <algorithm>
#include <cfenv>
#include <cmath>

GridNN::GridNN(
  Trajectory const &traj,
  float const os,
  bool const unsafe,
  Log &log,
  float const inRes,
  bool const shrink)
  : GridOp(traj.mapping(os, 0, inRes, shrink), unsafe, log)
{
}

GridNN::GridNN(Mapping const &mapping, bool const unsafe, Log &log)
  : GridOp(mapping, unsafe, log)
{
}

void GridNN::Adj(Cx3 const &noncart, Cx5 &cart) const
{
  assert(noncart.dimension(0) == cart.dimension(0));
  assert(cart.dimension(2) == mapping_.cartDims[0]);
  assert(cart.dimension(3) == mapping_.cartDims[1]);
  assert(cart.dimension(4) == mapping_.cartDims[2]);
  assert(mapping_.sortedIndices.size() == mapping_.cart.size());

  auto dev = Threads::GlobalDevice();
  Index const nThreads = dev.numThreads();
  std::vector<Cx5> workspace(nThreads);
  std::vector<Index> minZ(nThreads, 0L), szZ(nThreads, 0L);
  auto grid_task = [&](Index const lo, Index const hi, Index const ti) {
    // Allocate working space for this thread
    minZ[ti] = mapping_.cart[mapping_.sortedIndices[lo]].z;

    if (safe_) {
      Index const maxZ = mapping_.cart[mapping_.sortedIndices[hi - 1]].z;
      szZ[ti] = maxZ - minZ[ti] + 1;
      workspace[ti].resize(
        cart.dimension(0), cart.dimension(1), cart.dimension(2), cart.dimension(3), szZ[ti]);
      workspace[ti].setZero();
    }

    for (auto ii = lo; ii < hi; ii++) {
      log_.progress(ii, lo, hi);
      auto const si = mapping_.sortedIndices[ii];
      auto const c = mapping_.cart[si];
      auto const nc = mapping_.noncart[si];
      auto const e = std::min(mapping_.echo[si], int8_t(cart.dimension(1) - 1));
      auto const dc = mapping_.sdc[si];
      if (safe_) {
        workspace[ti].chip<4>(c.z - minZ[ti]).chip<3>(c.y).chip<2>(c.x).chip<1>(e) +=
          noncart.chip<2>(nc.spoke).chip<1>(nc.read) *
          noncart.chip<2>(nc.spoke).chip<1>(nc.read).constant(dc);
      } else {
        cart.chip<4>(c.z).chip<3>(c.y).chip<2>(c.x).chip<1>(e) +=
          noncart.chip<2>(nc.spoke).chip<1>(nc.read) *
          noncart.chip<2>(nc.spoke).chip<1>(nc.read).constant(dc);
      }
    }
  };

  auto const &start = log_.now();
  cart.setZero();
  Threads::RangeFor(grid_task, mapping_.cart.size());
  if (safe_) {
    log_.info("Combining thread workspaces...");
    for (Index ti = 0; ti < nThreads; ti++) {
      if (szZ[ti]) {
        cart
          .slice(
            Sz5{0, 0, 0, 0, minZ[ti]},
            Sz5{
              cart.dimension(0), cart.dimension(1), cart.dimension(2), cart.dimension(3), szZ[ti]})
          .device(dev) += workspace[ti];
      }
    }
  }
  log_.debug("Non-cart -> Cart: {}", log_.toNow(start));
}

void GridNN::A(Cx5 const &cart, Cx3 &noncart) const
{
  assert(noncart.dimension(0) == cart.dimension(0));
  assert(cart.dimension(2) == mapping_.cartDims[0]);
  assert(cart.dimension(3) == mapping_.cartDims[1]);
  assert(cart.dimension(4) == mapping_.cartDims[2]);

  auto grid_task = [&](Index const lo, Index const hi) {
    for (auto ii = lo; ii < hi; ii++) {
      log_.progress(ii, lo, hi);
      auto const si = mapping_.sortedIndices[ii];
      auto const c = mapping_.cart[si];
      auto const nc = mapping_.noncart[si];
      auto const e = std::min(mapping_.echo[si], int8_t(cart.dimension(1) - 1));
      noncart.chip<2>(nc.spoke).chip<1>(nc.read) =
        cart.chip<4>(c.z).chip<3>(c.y).chip<2>(c.x).chip<1>(e);
    }
  };
  auto const &start = log_.now();
  noncart.setZero();
  Threads::RangeFor(grid_task, mapping_.cart.size());
  log_.debug("Cart -> Non-cart: {}", log_.toNow(start));
}

R3 GridNN::apodization(Sz3 const sz) const
{
  R3 a(sz);
  a.setConstant(1.f);
  return a;
}
