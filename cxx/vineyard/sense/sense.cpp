#include "sense/sense.hpp"

#include "algo/lsmr.hpp"
#include "fft.hpp"
#include "filter.hpp"
#include "io/hd5.hpp"
#include "op/fft.hpp"
#include "op/recon.hpp"
#include "op/sense.hpp"
#include "pad.hpp"
#include "precon.hpp"
#include "tensors.hpp"
#include "threads.hpp"

namespace rl {
namespace SENSE {

Opts::Opts(args::Subparser &parser)
  : type(parser, "T", "SENSE type (auto/espirit/file.h5)", {"sense", 's'}, "auto")
  , volume(parser, "V", "SENSE calibration volume (first)", {"sense-vol"}, 0)
  , kWidth(parser, "K", "SENSE kernel width (7)", {"sense-width"}, 7)
  , res(parser, "R", "SENSE calibration res (12 mm)", {"sense-res"}, Eigen::Array3f::Constant(12.f))
  , fov(parser, "SENSE-FOV", "SENSE FOV (default header FOV)", {"sense-fov"}, Eigen::Array3f::Zero())
  , λ(parser, "L", "SENSE regularization", {"sense-lambda"}, 0.f)
/*, kRad(parser, "K", "ESPIRIT kernel size (4)", {"espirit-k"}, 3)
, calRad(parser, "C", "ESPIRIT calibration region (8)", {"espirit-cal"}, 6)
, gap(parser, "G", "ESPIRIT gap (0)", {"espirit-gap"}, 0)
, threshold(parser, "T", "ESPIRIT retention threshold (0.015)", {"espirit-thresh"}, 0.015f) */
{
}

auto LoresChannels(Opts &opts, GridOpts &gridOpts, Trajectory const &inTraj, Cx5 const &noncart, Basis<Cx> const &basis) -> Cx5
{
  auto const nC = noncart.dimension(0);
  auto const nT = noncart.dimension(2);
  auto const nS = noncart.dimension(3);
  auto const nV = noncart.dimension(4);
  if (opts.volume.Get() >= nV) {
    Log::Fail("Specified SENSE volume {} is greater than number of volumes in data {}", opts.volume.Get(), nV);
  }

  Cx4 const ncVol = noncart.chip<4>(opts.volume.Get());
  auto [traj, lores] = inTraj.downsample(ncVol, opts.res.Get(), 0, false, false);
  auto const A = Recon::Channels(false, gridOpts, traj, opts.fov.Get(), nC, nS, basis);
  auto const M = make_kspace_pre(traj, nC, basis, gridOpts.vcc);
  LSMR const lsmr{A, M, 4};

  // auto const maxCoord = Maximum(NoNaNs(traj.points()).abs());
  // NoncartesianTukey(maxCoord * 0.75, maxCoord, 0.f, traj.points(), lores);
  Cx5 const channels(Tensorfy(lsmr.run(lores.data()), A->ishape));

  Sz3 const shape = traj.matrixForFOV(opts.fov.Get());
  for (Index ii = 0; ii < 3; ii++) {
    if (shape[ii] > channels.dimension(ii + 2)) {
      Log::Fail("Requested SENSE FOV {} could not be satisfied with FOV {} and oversampling {}", opts.fov.Get().transpose(),
                traj.FOV().transpose(), gridOpts.osamp.Get());
    }
  }

  Cx5 const cropped = Crop(channels, AddFront(shape, channels.dimension(0), channels.dimension(1)));

  return cropped;
}

auto LoresKernels(Opts &opts, GridOpts &gridOpts, Trajectory const &inTraj, Cx5 const &noncart, Basis<Cx> const &basis) -> Cx5
{
  auto const nC = noncart.dimension(0);
  auto const nT = noncart.dimension(2);
  auto const nS = noncart.dimension(3);
  auto const nV = noncart.dimension(4);
  if (opts.volume.Get() >= nV) {
    Log::Fail("Specified SENSE volume {} is greater than number of volumes in data {}", opts.volume.Get(), nV);
  }

  Sz3 kSz;
  kSz.fill(opts.kWidth.Get());
  Cx4 const ncVol = noncart.chip<4>(opts.volume.Get());
  auto const [traj, lores] = inTraj.downsample(ncVol, kSz, 0, true, true);
  auto const A = TOps::Grid<Cx, 3>::Make(traj, gridOpts.ktype.Get(), gridOpts.osamp.Get(), nC, basis);
  auto const M = make_kspace_pre(traj, nC, basis, false);
  LSMR const lsmr{A, M, 4};
  Cx5 const  channels(Tensorfy(lsmr.run(lores.data()), A->ishape));
  return channels;
}

void TikhonovDivision(Cx5 &channels, Cx4 const &ref, float const λ)
{
  Sz5 const shape = channels.dimensions();
  Log::Debug("Normalizing SENSE. Dimensions {} λ {}", shape, λ);
  Cx5 normalized(shape);
  channels.device(Threads::GlobalDevice()) =
    channels / (ref + ref.constant(λ)).reshape(AddFront(LastN<4>(shape), 1)).broadcast(Sz5{shape[0], 1, 1, 1, 1});
}

auto SobolevWeights(Sz3 const kW, Index const l) -> Re3
{
  Re3 W(kW);
  for (Index ik = 0; ik < kW[2]; ik++) {
    float const kk = ik - (kW[2] / 2);
    for (Index ij = 0; ij < kW[1]; ij++) {
      float const kj = ij - (kW[1] / 2);
      for (Index ii = 0; ii < kW[0]; ii++) {
        float const ki = ii - (kW[0] / 2);
        float const k2 = (ki * ki + kj * kj + kk * kk);
        W(ii, ij, ik) = std::pow(1.f + k2, l / 2);
      }
    }
  }
  return W;
}

auto Nonsense(Cx5 &channels, Cx4 const &ref, Index const kW) -> Cx5
{
  Sz5 const cshape = channels.dimensions();
  if (LastN<4>(cshape) != ref.dimensions()) {
    Log::Fail("SENSE dimensions don't match channels {} reference {}", cshape, ref.dimensions());
  }
  if (cshape[2] < (2 * kW) || cshape[3] < (2 * kW) || cshape[4] < (2 * kW)) {
    Log::Fail("SENSE matrix {} insufficient to satisfy kernel size {}", LastN<3>(cshape), kW);
  }
  Sz5 const kshape{cshape[0], cshape[1], kW, kW, kW};

  // Set up operators
  auto p = std::make_shared<TOps::Pad<Cx, 5>>(kshape, cshape);
  auto f = std::make_shared<TOps::FFT<5, 3>>(cshape, true);
  auto fp = std::make_shared<Ops::Multiply<Cx>>(f, p);
  // auto fp_inv = fp->inverse();
  auto n = std::make_shared<TOps::NonSENSE>(ref, cshape[0]);
  auto A = std::make_shared<Ops::Multiply<Cx>>(n, fp);
  // auto A = std::make_shared<Ops::Multiply<Cx>>(fp_inv, nfp);

  // Smoothness penalthy (Sobolev Norm, Nonlinear Inversion Paper Uecker 2008)
  Cx3 const  sw = SobolevWeights(Sz3{kW, kW, kW}, 16).cast<Cx>();
  auto const swv = CollapseToArray(sw);
  auto       W = std::make_shared<Ops::DiagRep<Cx>>(kshape[0] * kshape[1], swv);
  auto       λ = std::make_shared<Ops::DiagScale<Cx>>(W->rows(), 1.f);
  auto       reg = std::make_shared<Ops::Multiply<Cx>>(λ, W);
  auto       Aʹ = std::make_shared<Ops::VStack<Cx>>(A, reg);

  // Preconditioner
  auto I = std::make_shared<Ops::Identity<Cx>>(A->rows());
  auto M = W;
  auto Mʹ = std::make_shared<Ops::DStack<Cx>>(I, M);

  Ops::Op<Cx>::CMap cmap(channels.data(), A->rows());
  // auto b = fp_inv->forward(cmap);
  Ops::Op<Cx>::Vector bʹ(Aʹ->rows());
  bʹ.head(A->rows()) = cmap;
  bʹ.tail(reg->rows()).setZero();

  Log::Tensor("W", sw.dimensions(), sw.data(), {"x", "y", "z"});
  Log::Tensor("ref", ref.dimensions(), ref.data(), {"v", "x", "y", "z"});
  Log::Tensor("channels", cshape, channels.data(), HD5::Dims::SENSE);
  // Log::Tensor("b", kshape, b.data(), HD5::Dims::SENSE);

  auto debug = [&](Index const i, LSMR::Vector const &x) {
    Log::Tensor(fmt::format("x-{:02d}", i), kshape, x.data(), HD5::Dims::SENSE);
    auto const temp = fp->forward(x);
    auto const temp2 = Tensorfy(temp, cshape);
    Log::Tensor(fmt::format("ximg-{:02d}", i), temp2.dimensions(), temp2.data(), HD5::Dims::SENSE);
  };
  LSMR lsmr{Aʹ, Mʹ};
  lsmr.iterLimit = 8;
  lsmr.debug = debug;
  auto const x = lsmr.run(bʹ.data(), 0.f);
  // auto const kernels = Tensorfy(x, kshape);
  Log::Print("Finished run");
  auto const temp = fp->forward(x);
  Cx5        maps = Tensorfy(temp, cshape);
  return maps;
}

auto Choose(Opts &opts, GridOpts &nufft, Trajectory const &traj, Cx5 const &noncart) -> Cx5
{
  if (opts.type.Get() == "auto") {
    Log::Print("SENSE Self-Calibration");
    auto c = LoresChannels(opts, nufft, traj, noncart);
    TikhonovDivision(c, ConjugateSum(c, c).sqrt(), opts.λ.Get());
    return c;
  } else if (opts.type.Get() == "espirit") {
    Log::Fail("Not supported right now");
    // auto channels = LoresChannels(opts, core, traj, noncart);
    // auto fft = FFT::Make<5, 3>(channels.dimensions());
    // fft->reverse(channels);
    // return ESPIRIT(channels, shape, opts.kRad.Get(), opts.calRad.Get(), opts.gap.Get(), opts.threshold.Get());
  } else {
    HD5::Reader senseReader(opts.type.Get());
    return senseReader.readTensor<Cx5>(HD5::Keys::Data);
  }
}

} // namespace SENSE
} // namespace rl
