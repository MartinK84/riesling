#pragma once

#include "common.hpp"
#include "log.h"
#include "tensorOps.h"
#include "threads.h"
#include "types.h"

namespace {
auto SymOrtho(float const a, float const b)
{
  if (b == 0.f) {
    return std::make_tuple(std::copysign(1.f, a), 0.f, std::abs(a));
  } else if (a == 0.f) {
    return std::make_tuple(0.f, std::copysign(1.f, b), std::abs(b));
  } else if (std::abs(b) > std::abs(a)) {
    auto const τ = a / b;
    float s = std::copysign(1.f, b) / std::sqrt(1.f + τ * τ);
    return std::make_tuple(s, s * τ, b / s);
  } else {
    auto const τ = b / a;
    float c = std::copysign(1.f, a) / std::sqrt(1.f + τ * τ);
    return std::make_tuple(c, c * τ, a / c);
  }
}

} // namespace

/* Based on https://github.com/PythonOptimizers/pykrylov/blob/master/pykrylov/lls/lsmr.py
 */

/*
 * LSMR with arbitrary regularization, i.e. Solve (A'A + λI)x = A'b + c with warm start
 */
template <typename Op, typename Precond>
typename Op::Input lsmr(
  Index const &max_its,
  Op &op,
  typename Op::Output const &b,
  typename Op::Input const &x0,
  float const λ,
  typename Op::Input const &xr,
  Precond const *M = nullptr, // Left preconditioner
  float const atol = 1.e-6f,
  float const btol = 1.e-6f,
  float const ctol = 1.e-6f,
  bool const debug = false)
{
  auto dev = Threads::GlobalDevice();
  // Allocate all memory
  using TI = typename Op::Input;
  using TO = typename Op::Output;
  auto const inDims = op.inputDimensions();
  auto const outDims = op.outputDimensions();

  CheckDimsEqual(x0.dimensions(), inDims);
  CheckDimsEqual(xr.dimensions(), inDims);
  CheckDimsEqual(b.dimensions(), outDims);

  // Workspace variables
  TO Mu(outDims), u(outDims);
  TI v(inDims), h(inDims), h̅(inDims), x(inDims), ur(inDims);

  float scale = Norm(b);

  x.device(dev) = x0 / x.constant(scale);
  Mu.device(dev) = (b / b.constant(scale)) - op.A(x);
  u.device(dev) = M ? M->apply(Mu) : Mu;
  ur.device(dev) = xr * xr.constant(sqrt(λ) / scale) - x * x.constant(sqrt(λ));

  float β = sqrt(std::real(Dot(Mu, u)) + std::real(Dot(ur, ur)));
  Mu.device(dev) = Mu / Mu.constant(β);
  u.device(dev) = u / u.constant(β);
  ur.device(dev) = ur / ur.constant(β);

  v.device(dev) = op.Adj(u) + sqrt(λ) * ur;
  float α = Norm(v);
  v.device(dev) = v / v.constant(α);
  h.device(dev) = v;
  h̅.setZero();

  // Initialize transformation variables. There are a lot
  float ζ̅ = α * β;
  float α̅ = α;
  float ρ = 1;
  float ρ̅ = 1;
  float c̅ = 1;
  float s̅ = 0;

  // Initialize variables for ||r||
  float β̈ = β;
  float β̇ = 0;
  float ρ̇old = 1;
  float τ̃old = 0;
  float θ̃ = 0;
  float ζ = 0;

  // Initialize variables for estimation of ||A|| and cond(A)
  float normA2 = α * α;
  float maxρ̅ = 0;
  float minρ̅ = std::numeric_limits<float>::max();
  float const normb = β;

  if (debug) {
    Log::Image(u, "lsmr-u-init");
    Log::Image(v, "lsmr-v-init");
    Log::Image(x, "lsmr-x-init");
    Log::Image(ur, "lsmr-ur-init");
  }

  Log::Print(
    FMT_STRING(
      "Starting regularized LSMR: scale {} λ {} Atol {} btol {} ctol {}, initial residual {}"),
    scale,
    λ,
    atol,
    btol,
    ctol,
    normb);

  for (Index ii = 0; ii < max_its; ii++) {
    // Bidiagonalization step
    Mu.device(dev) = op.A(v) - α * Mu;
    u.device(dev) = M ? M->apply(Mu) : Mu;
    ur.device(dev) = ((sqrt(λ) * v) - (α * ur));
    β = sqrt(std::real(Dot(Mu, u)) + std::real(Dot(ur, ur)));
    Mu.device(dev) = Mu / Mu.constant(β);
    u.device(dev) = u / u.constant(β);
    ur.device(dev) = ur / ur.constant(β);

    v.device(dev) = op.Adj(u) + (sqrt(λ) * ur) - (β * v);
    α = sqrt(std::real(Dot(v, v)));
    v.device(dev) = v / v.constant(α);

    // Construct rotation
    float ρold = ρ;
    float c, s;
    std::tie(c, s, ρ) = SymOrtho(α̅, β);
    float θnew = s * α;
    α̅ = c * α;

    // Use a plane rotation (Qbar_i) to turn R_i^T to R_i^bar
    float ρ̅old = ρ̅;
    float ζold = ζ;
    float θ̅ = s̅ * ρ;
    float ρtemp = c̅ * ρ;
    std::tie(c̅, s̅, ρ̅) = SymOrtho(c̅ * ρ, θnew);
    ζ = c̅ * ζ̅;
    ζ̅ = -s̅ * ζ̅;

    // Update h, h̅h, x.
    h̅.device(dev) = h - (θ̅ * ρ / (ρold * ρ̅old)) * h̅;
    x.device(dev) = x + (ζ / (ρ * ρ̅)) * h̅;
    h.device(dev) = v - (θnew / ρ) * h;

    if (debug) {
      Log::Image(u, fmt::format(FMT_STRING("lsmr-u-{:02d}"), ii));
      Log::Image(v, fmt::format(FMT_STRING("lsmr-v-{:02d}"), ii));
      Log::Image(x, fmt::format(FMT_STRING("lsmr-x-{:02d}"), ii));
      Log::Image(h̅, fmt::format(FMT_STRING("lsmr-hbar-{:02d}"), ii));
      Log::Image(h, fmt::format(FMT_STRING("lsmr-h-{:02d}"), ii));
      Log::Image(ur, fmt::format(FMT_STRING("lsmr-ur-{:02d}"), ii));
    }
    // Estimate of ||r||.
    // Apply rotation P{k-1}.
    float const β̂ = c * β̈;
    β̈ = -s * β̈;

    // Apply rotation Qtilde_{k-1}.
    // β̇ = β̇_{k-1} here.

    float const θ̃old = θ̃;
    auto [c̃old, s̃old, ρ̃old] = SymOrtho(ρ̇old, θ̅);
    θ̃ = s̃old * ρ̅;
    ρ̇old = c̃old * ρ̅;
    β̇ = -s̃old * β̇ + c̃old * β̂;

    // β̇   = β̇_k here.
    // ρ̇old = ρ̇_k  here.

    τ̃old = (ζold - θ̃old * τ̃old) / ρ̃old;
    float const τ̇ = (ζ - θ̃ * τ̃old) / ρ̇old;
    float const normr = sqrt(pow(β̇ - τ̇, 2.f) + β̈ * β̈);

    // Estimate ||A||.
    normA2 += β * β;
    float const normA = sqrt(normA2);
    normA2 += α * α;

    // Estimate cond(A).
    maxρ̅ = std::max(maxρ̅, ρ̅old);
    if (ii > 1) {
      minρ̅ = std::min(minρ̅, ρ̅old);
    }
    float const condA = std::max(maxρ̅, ρtemp) / std::min(minρ̅, ρtemp);

    Log::Print(
      FMT_STRING("LSMR {}: Residual {} Estimate cond(A) {} α {} β {}"), ii, normr, condA, α, β);

    // Convergence tests - go in pairs which check large/small values then the user tolerance
    float const normar = abs(ζ̅);
    float const normx = Norm(x);

    if (1.f + (1.f / condA) <= 1.f) {
      Log::Print(FMT_STRING("Cond(A_) is very large"));
      break;
    }
    if ((1.f / condA) <= ctol) {
      Log::Print(FMT_STRING("Cond(A_) has exceeded limit"));
      break;
    }

    if (1.f + (normar / (normA * normr)) <= 1.f) {
      Log::Print(FMT_STRING("Least-squares solution reached machine precision"));
      break;
    }
    if ((normar / (normA * normr)) <= atol) {
      Log::Print(FMT_STRING("Least-squares = {} < atol = {}"), normar / (normA * normr), atol);
      break;
    }

    if (normr <= (btol * normb + atol * normA * normx)) {
      Log::Print(FMT_STRING("Ax - b <= atol, btol"));
      break;
    }
    if ((1.f + normr / (normb + normA * normx)) <= 1.f) {
      Log::Print(FMT_STRING("Ax - b reached machine precision"));
      break;
    }
  }
  return x * x.constant(scale);
}

/*
 * LSMR with Tikhonov regularization / damping. i.e. Solve (A'A + λI)x = A'b
 */
template <typename Op, typename Precond>
typename Op::Input lsmr_damp(
  Index const &max_its,
  Op &op,
  typename Op::Output const &b,
  Precond const *M = nullptr, // Left preconditioner
  float const atol = 1.e-6f,
  float const btol = 1.e-6f,
  float const ctol = 1.e-6f,
  float const λ = 0.f)
{
  float const scale = Norm(b);

  auto dev = Threads::GlobalDevice();
  // Allocate all memory
  using TI = typename Op::Input;
  using TO = typename Op::Output;
  auto const inDims = op.inputDimensions();
  auto const outDims = op.outputDimensions();

  TO Mu(outDims);
  TO u(outDims);
  Mu.device(dev) = b / b.constant(scale);
  u.device(dev) = M ? M->apply(Mu) : Mu;
  float β = sqrt(std::real(Dot(u, Mu)));
  Mu.device(dev) = Mu / Mu.constant(β);
  u.device(dev) = u / u.constant(β);

  TI v(inDims);
  v.device(dev) = op.Adj(u);
  float α = Norm(v);
  v.device(dev) = v / v.constant(α);

  TI h(inDims), h̅(inDims), x(inDims);
  h.device(dev) = v;
  h̅.setZero();
  x.setZero();

  // Initialize transformation variables. There are a lot
  float ζ̅ = α * β;
  float α̅ = α;
  float ρ = 1;
  float ρ̅ = 1;
  float c̅ = 1;
  float s̅ = 0;

  // Initialize variables for ||r||
  float β̈ = β;
  float β̇ = 0;
  float ρ̇old = 1;
  float τ̃old = 0;
  float θ̃ = 0;
  float ζ = 0;
  float d = 0;

  // Initialize variables for estimation of ||A|| and cond(A)
  float normA2 = α * α;
  float maxρ̅ = 0;
  float minρ̅ = std::numeric_limits<float>::max();
  float const normb = β;

  Log::Print(
    FMT_STRING("Starting LSMR: scale {} λ {} Atol {} btol {} ctol {} initial residual {}"),
    scale,
    λ,
    atol,
    btol,
    ctol,
    normb);

  for (Index ii = 0; ii < max_its; ii++) {
    // Bidiagonalization step
    Mu.device(dev) = op.A(v) - α * Mu;
    u.device(dev) = M ? M->apply(Mu) : Mu;
    β = sqrt(std::real(Dot(Mu, u)));
    Mu.device(dev) = Mu / Mu.constant(β);
    u.device(dev) = u / u.constant(β);

    v.device(dev) = op.Adj(u) - β * v;
    α = Norm(v);
    v.device(dev) = v / v.constant(α);

    // Construct rotations
    float ch, sh, αh;
    std::tie(ch, sh, αh) = SymOrtho(α̅, λ);

    float ρold = ρ;
    float c, s;
    std::tie(c, s, ρ) = SymOrtho(αh, β);
    float θnew = s * α;
    α̅ = c * α;

    // Use a plane rotation (Qbar_i) to turn R_i^T to R_i^bar
    float ρ̅old = ρ̅;
    float ζold = ζ;
    float θ̅ = s̅ * ρ;
    float ρtemp = c̅ * ρ;
    std::tie(c̅, s̅, ρ̅) = SymOrtho(c̅ * ρ, θnew);
    ζ = c̅ * ζ̅;
    ζ̅ = -s̅ * ζ̅;

    // Update h, h̅, x.
    h̅.device(dev) = h - (θ̅ * ρ / (ρold * ρ̅old)) * h̅;
    x.device(dev) = x + (ζ / (ρ * ρ̅)) * h̅;
    h.device(dev) = v - (θnew / ρ) * h;

    Log::Image(v, fmt::format(FMT_STRING("lsmr-v-{:02d}"), ii));
    Log::Image(x, fmt::format(FMT_STRING("lsmr-x-{:02d}"), ii));
    Log::Image(h, fmt::format(FMT_STRING("lsmr-hbar-{:02d}"), ii));
    Log::Image(h, fmt::format(FMT_STRING("lsmr-h-{:02d}"), ii));

    // Estimate of ||r||.
    // Apply rotation P{k-1}.
    float const β̂ = ch * β̈;
    float const βcheck = -sh * β̈;
    β̈ = -s * β̈;

    // Apply rotation Qtilde_{k-1}.
    // β̇ = β̇_{k-1} here.

    float const θ̃old = θ̃;
    auto [c̃old, s̃old, ρ̃old] = SymOrtho(ρ̇old, θ̅);
    θ̃ = s̃old * ρ̅;
    ρ̇old = c̃old * ρ̅;
    β̇ = -s̃old * β̇ + c̃old * β̂;

    // β̇   = β̇_k here.
    // ρ̇old = ρ̇_k  here.
    τ̃old = (ζold - θ̃old * τ̃old) / ρ̃old;
    float const τ̇ = (ζ - θ̃ * τ̃old) / ρ̇old;
    d = d + βcheck * βcheck;
    float const normr = sqrt(d + pow(β̇ - τ̇, 2.f) + β̈ * β̈);

    // Estimate ||A||.
    normA2 += β * β;
    float const normA = sqrt(normA2);
    normA2 += α * α;

    // Estimate cond(A).
    maxρ̅ = std::max(maxρ̅, ρ̅old);
    if (ii > 1) {
      minρ̅ = std::min(minρ̅, ρ̅old);
    }
    float const condA = std::max(maxρ̅, ρtemp) / std::min(minρ̅, ρtemp);

    Log::Print(
      FMT_STRING("LSMR {}: Residual {} Estimate cond(A) {} α {} β {}"), ii, normr, condA, α, β);

    // Convergence tests - go in pairs which check large/small values then the user tolerance
    float const normar = abs(ζ̅);
    float const normx = Norm(x);

    if (1.f + (1.f / condA) <= 1.f) {
      Log::Print(FMT_STRING("Cond(A_) is very large"));
      break;
    }
    if ((1.f / condA) <= ctol) {
      Log::Print(FMT_STRING("Cond(A_) has exceeded limit"));
      break;
    }

    if (1.f + (normar / (normA * normr)) <= 1.f) {
      Log::Print(FMT_STRING("Least-squares solution reached machine precision"));
      break;
    }
    if ((normar / (normA * normr)) <= atol) {
      Log::Print(FMT_STRING("Least-squares < atol"));
      break;
    }

    if (normr <= (btol * normb + atol * normA * normx)) {
      Log::Print(FMT_STRING("Ax - b <= atol, btol"));
      break;
    }
    if ((1.f + normr / (normb + normA * normx)) <= 1.f) {
      Log::Print(FMT_STRING("Ax - b reached machine precision"));
      break;
    }
  }
  return x * x.constant(scale);
}
