#pragma once

#include "log.h"
#include "tensorOps.h"
#include "threads.h"

namespace rl {

template <typename Inner>
struct ADMM
{
  using Input = typename Inner::Input;
  using Output = typename Inner::Output;

  Inner &inner;
  std::function<Input(Input const &)> const &reg;
  Index iterLimit;
  float rho = 0.1;
  float abstol = 1.e-3f;
  float reltol = 1.e-3f;

  Input run(Output const &b) const
  {
    auto dev = Threads::GlobalDevice();
    // Allocate all memory
    auto const dims = inner.op.inputDimensions();
    Input u(dims), x(dims), z(dims), zold(dims), xpu(dims);
    x.setZero();
    z.setZero();
    zold.setZero();
    u.setZero();
    xpu.setZero();

    float const sp = std::sqrt(float(Product(dims)));

    Log::Print(FMT_STRING("ADMM rho {}"), rho);
    for (Index ii = 0; ii < iterLimit; ii++) {
      x = inner.run(b, x, (z - u));
      xpu.device(dev) = x + u;
      zold = z;
      z = reg(xpu);
      u.device(dev) = xpu - z;

      float const norm_prim = Norm(x - z);
      float const norm_dual = Norm(-rho * (z - zold));

      float const eps_prim = sp * abstol + reltol * std::max(Norm(x), Norm(z));
      float const eps_dual = sp * abstol + reltol * rho * Norm(u);

      Log::Tensor(x, fmt::format("admm-x-{:02d}", ii));
      Log::Tensor(xpu, fmt::format("admm-xpu-{:02d}", ii));
      Log::Tensor(z, fmt::format("admm-z-{:02d}", ii));
      Log::Tensor(u, fmt::format("admm-u-{:02d}", ii));
      Log::Print(
        FMT_STRING("ADMM {:02d}: Primal Norm {} Primal Eps {} Dual Norm {} Dual Eps {}"),
        ii,
        norm_prim,
        eps_prim,
        norm_dual,
        eps_dual);
      if ((norm_prim < eps_prim) && (norm_dual < eps_dual)) {
        break;
      }
      float const mu = 10.f;
      if (norm_prim > mu * norm_dual) {
        Log::Print(FMT_STRING("Primal norm is outside limit {}, consider changing rho"), mu * norm_dual);
      } else if (norm_dual > mu * norm_prim) {
        Log::Print(FMT_STRING("Dual norm is outside limit {}, consider changing rho"), mu * norm_prim);
      }
    }
    return x;
  }
};

} // namespace rl
