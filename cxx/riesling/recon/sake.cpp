#include "algo/admm.hpp"
#include "io/hd5.hpp"
#include "log.hpp"
#include "op/fft.hpp"
#include "op/recon.hpp"
#include "parse_args.hpp"
#include "precon.hpp"
#include "prox/slr.hpp"
#include "scaling.hpp"

using namespace rl;

void main_sake(args::Subparser &parser)
{
  CoreOpts   coreOpts(parser);
  GridOpts   gridOpts(parser);
  PreconOpts preOpts(parser);
  RlsqOpts   rlsqOpts(parser);

  args::ValueFlag<float> λ(parser, "L", "Regularization parameter (default 1e-1)", {"lambda"}, 1.e-1f);
  args::ValueFlag<Index> kSz(parser, "SZ", "SLR Kernel Size (default 5)", {"kernel-size"}, 5);

  ParseCommand(parser, coreOpts.iname, coreOpts.oname);

  HD5::Reader reader(coreOpts.iname.Get());
  Info const  info = reader.readInfo();
  Trajectory  traj(reader, info.voxel_size);
  auto const  basis = ReadBasis(coreOpts.basisFile.Get());
  Cx5         noncart = reader.readTensor<Cx5>();
  traj.checkDims(FirstN<3>(noncart.dimensions()));
  Index const nC = noncart.dimension(0);
  Index const nS = noncart.dimension(3);
  Index const nV = noncart.dimension(4);

  auto const A = Channels(coreOpts, gridOpts, traj, nC, nS, basis);
  auto const M = make_kspace_pre(traj, nC, basis, preOpts.type.Get(), preOpts.bias.Get());

  Sz5 const    shape = A->ishape;
  auto         fft = std::make_shared<Ops::FFTOp<5, 3>>(shape);
  auto         slr = std::make_shared<Proxs::SLR>(λ.Get(), kSz.Get(), shape);
  ADMM::DebugX debug_x = [shape](Index const ii, ADMM::Vector const &x) {
    Log::Tensor(fmt::format("admm-x-{:02d}", ii), shape, x.data());
  };
  ADMM::DebugZ debug_z = [shape](Index const ii, Index const ir, ADMM::Vector const &Fx, ADMM::Vector const &z,
                                 ADMM::Vector const &u) {
    Log::Tensor(fmt::format("admm-Fx-{:02d}-{:02d}", ir, ii), shape, Fx.data());
    Log::Tensor(fmt::format("admm-z-{:02d}-{:02d}", ir, ii), shape, z.data());
    Log::Tensor(fmt::format("admm-u-{:02d}-{:02d}", ir, ii), shape, u.data());
  };

  ADMM admm{A,
            M,
            {fft},
            {slr},
            rlsqOpts.inner_its0.Get(),
            rlsqOpts.inner_its1.Get(),
            rlsqOpts.atol.Get(),
            rlsqOpts.btol.Get(),
            rlsqOpts.ctol.Get(),
            rlsqOpts.outer_its.Get(),
            rlsqOpts.ε.Get(),
            rlsqOpts.μ.Get(),
            rlsqOpts.τ.Get(),
            debug_x,
            debug_z};

  Cx5         out(AddBack(LastN<4>(A->ishape), nV));
  float const scale = Scaling(rlsqOpts.scaling, A, M, &noncart(0, 0, 0, 0, 0));
  noncart.device(Threads::GlobalDevice()) = noncart * noncart.constant(scale);

  for (Index iv = 0; iv < nV; iv++) {
    auto const channels = admm.run(&noncart(0, 0, 0, 0, iv), rlsqOpts.ρ.Get());
    auto const channelsT = Tensorfy(channels, A->ishape);
    out.chip<4>(iv) = (channelsT * channelsT.conjugate()).sum(Sz1{0}).sqrt();
  }
  WriteOutput(coreOpts.oname.Get(), out, info, Log::Saved());
  Log::Print("Finished {}", parser.GetCommand().Name());
}