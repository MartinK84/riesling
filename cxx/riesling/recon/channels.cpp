#include "algo/lsmr.hpp"
#include "io/hd5.hpp"
#include "log.hpp"
#include "op/recon.hpp"
#include "parse_args.hpp"
#include "precon.hpp"

using namespace rl;

void main_channels(args::Subparser &parser)
{
  CoreOpts   coreOpts(parser);
  GridOpts   gridOpts(parser);
  PreconOpts preOpts(parser);
  LsqOpts    lsqOpts(parser);

  args::ValueFlag<Eigen::Array3f, Array3fReader> ifov(parser, "FOV", "Iteration FOV (default 256,256,256)", {"ifov"},
                                                      Eigen::Array3f::Constant(256.f));

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

  auto const  A = Recon::Channels(coreOpts.ndft, gridOpts, traj, ifov.Get(), nC, nS, basis);
  auto const  M = make_kspace_pre(traj, nC, basis, gridOpts.vcc, preOpts.type.Get(), preOpts.bias.Get());
  auto debug = [&A](Index const i, LSMR::Vector const &x) {
    Log::Tensor(fmt::format("lsmr-x-{:02d}", i), A->ishape, x.data(), {"channel", "v", "x", "y", "z"});
  };
  LSMR const lsmr{A, M, lsqOpts.its.Get(), lsqOpts.atol.Get(), lsqOpts.btol.Get(), lsqOpts.ctol.Get(), debug}; 

  TOps::Pad<Cx, 5, 3> outFOV(traj.matrixForFOV(coreOpts.fov.Get()), A->ishape);
  Cx6                 out(AddBack(outFOV.ishape, nV));
  for (Index iv = 0; iv < nV; iv++) {
    auto const channels = lsmr.run(&noncart(0, 0, 0, 0, iv), lsqOpts.λ.Get());
    auto const cropped = outFOV.adjoint(Tensorfy(channels, A->ishape));
    out.chip<5>(iv) = cropped;
  }
  HD5::Writer writer(coreOpts.oname.Get());
  writer.writeTensor(HD5::Keys::Data, out.dimensions(), out.data(), HD5::Dims::Channels);
  writer.writeInfo(info);
  writer.writeString("log", Log::Saved());
  Log::Print("Finished {}", parser.GetCommand().Name());
}
