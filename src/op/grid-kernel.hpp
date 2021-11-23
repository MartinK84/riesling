#pragma once

#include "../cropper.h"
#include "../kernel.h"
#include "../trajectory.h"

#include "grid.h"

template <typename Kernel>
struct Grid final : GridOp
{
  Grid(
    Trajectory const &traj,
    float const os,
    bool const unsafe,
    Log &log,
    float const inRes = -1.f,
    bool const shrink = false)
    : GridOp(traj.mapping(os, (Kernel::InPlane / 2), inRes, shrink), unsafe, log)
    , kernel_{os}
  {
  }

  Grid(Mapping const &mapping, bool const unsafe, Log &log)
    : GridOp(mapping, unsafe, log)
    , kernel_{mapping_.osamp}
  {
  }

  void A(Input const &cart, Output &noncart) const
  {
    assert(noncart.dimension(0) == cart.dimension(0));
    assert(cart.dimension(1) == mapping_.cartDims[0]);
    assert(cart.dimension(2) == mapping_.cartDims[1]);
    assert(cart.dimension(3) == mapping_.cartDims[2]);

    long const nchan = cart.dimension(0);
    Eigen::IndexList<int, FixIn, FixIn, FixThrough> szC;
    szC.set(0, nchan);

    auto grid_task = [&](long const lo, long const hi) {
      Eigen::IndexList<FixZero, int, int, int> stC;
      for (auto ii = lo; ii < hi; ii++) {
        log_.progress(ii, lo, hi);
        auto const si = mapping_.sortedIndices[ii];
        auto const c = mapping_.cart[si];
        auto const n = mapping_.noncart[si];
        auto const k = kernel_(mapping_.offset[si]);
        stC.set(1, c.x - (Kernel::InPlane / 2));
        stC.set(2, c.y - (Kernel::InPlane / 2));
        stC.set(3, c.z - (Kernel::ThroughPlane / 2));
        noncart.chip(n.spoke, 2).chip(n.read, 1) = cart.slice(stC, szC).contract(
          k.template cast<Cx>(),
          Eigen::IndexPairList<
            Eigen::type2indexpair<1, 0>,
            Eigen::type2indexpair<2, 1>,
            Eigen::type2indexpair<3, 2>>());
      }
    };
    auto const &start = log_.now();
    noncart.setZero();
    Threads::RangeFor(grid_task, mapping_.cart.size());
    log_.debug("Cart -> Non-cart: {}", log_.toNow(start));
  }

  void Adj(Output const &noncart, Input &cart) const
  {
    assert(noncart.dimension(0) == cart.dimension(0));
    assert(cart.dimension(1) == mapping_.cartDims[0]);
    assert(cart.dimension(2) == mapping_.cartDims[1]);
    assert(cart.dimension(3) == mapping_.cartDims[2]);
    assert(mapping_.sortedIndices.size() == mapping_.cart.size());

    long const nchan = cart.dimension(0);
    Eigen::IndexList<int, FixOne, FixOne, FixOne> rshNC;
    constexpr Eigen::IndexList<FixOne, FixIn, FixIn, FixThrough> brdNC;
    rshNC.set(0, nchan);

    constexpr Eigen::IndexList<FixOne, FixIn, FixIn, FixThrough> rshK;
    Eigen::IndexList<int, FixOne, FixOne, FixOne> brdK;
    brdK.set(0, nchan);

    Eigen::IndexList<int, FixIn, FixIn, FixThrough> szC;
    szC.set(0, nchan);

    auto dev = Threads::GlobalDevice();
    long const nThreads = dev.numThreads();
    std::vector<Cx4> workspace(nThreads);
    std::vector<long> minZ(nThreads, 0L), szZ(nThreads, 0L);
    auto grid_task = [&](long const lo, long const hi, long const ti) {
      // Allocate working space for this thread
      Eigen::IndexList<FixZero, int, int, int> stC;
      minZ[ti] = mapping_.cart[mapping_.sortedIndices[lo]].z - ((Kernel::ThroughPlane - 1) / 2);

      if (safe_) {
        long const maxZ =
          mapping_.cart[mapping_.sortedIndices[hi - 1]].z + (Kernel::ThroughPlane / 2);
        szZ[ti] = maxZ - minZ[ti] + 1;
        workspace[ti].resize(cart.dimension(0), cart.dimension(1), cart.dimension(2), szZ[ti]);
        workspace[ti].setZero();
      }

      for (auto ii = lo; ii < hi; ii++) {
        log_.progress(ii, lo, hi);
        auto const si = mapping_.sortedIndices[ii];
        auto const c = mapping_.cart[si];
        auto const n = mapping_.noncart[si];
        auto const nc = noncart.chip(n.spoke, 2).chip(n.read, 1);
        auto const k = kernel_(mapping_.offset[si]).template cast<Cx>();
        auto const nck = (nc * nc.constant(mapping_.sdc[si])).reshape(rshNC).broadcast(brdNC) *
                         k.reshape(rshK).broadcast(brdK);
        stC.set(1, c.x - (Kernel::InPlane / 2));
        stC.set(2, c.y - (Kernel::InPlane / 2));
        if (safe_) {
          stC.set(3, c.z - (Kernel::ThroughPlane / 2) - minZ[ti]);
          workspace[ti].slice(stC, szC) += nck;
        } else {
          stC.set(3, c.z - (Kernel::ThroughPlane / 2));
          cart.slice(stC, szC) += nck;
        }
      }
    };

    auto const start = log_.now();
    cart.setZero();
    Threads::RangeFor(grid_task, mapping_.cart.size());
    log_.debug("Non-cart -> Cart: {}", log_.toNow(start));
    if (safe_) {
      log_.info("Combining thread workspaces...");
      auto const start2 = log_.now();
      for (long ti = 0; ti < nThreads; ti++) {
        if (szZ[ti]) {
          cart
            .slice(
              Sz4{0, 0, 0, minZ[ti]},
              Sz4{cart.dimension(0), cart.dimension(1), cart.dimension(2), szZ[ti]})
            .device(dev) += workspace[ti];
        }
      }
      log_.debug("Combining took: {}", log_.toNow(start2));
    }
  }

  R3 apodization(Sz3 const sz) const
  {
    auto gridSz = this->gridDims();
    Cx3 temp(gridSz);
    FFT::ThreeD fft(temp, log_);
    temp.setZero();
    auto const k = kernel_(Point3{0, 0, 0});
    Crop3(temp, k.dimensions()) = k.template cast<Cx>();
    fft.reverse(temp);
    R3 a = Crop3(R3(temp.real()), sz);
    float const scale =
      sqrt(std::accumulate(gridSz.cbegin(), gridSz.cend(), 1, std::multiplies<long>()));
    log_.info(
      FMT_STRING("Apodization size {} scale factor: {}"), fmt::join(a.dimensions(), ","), scale);
    a.device(Threads::GlobalDevice()) = a * a.constant(scale);
    return a;
  }

private:
  using FixIn = Eigen::type2index<Kernel::InPlane>;
  using FixThrough = Eigen::type2index<Kernel::ThroughPlane>;

  Kernel kernel_;
};
