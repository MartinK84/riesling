#include "io/hd5.hpp"
#include "log.hpp"
#include "parse_args.hpp"
#include "tensorOps.hpp"
#include "types.hpp"

using namespace rl;

int main_noisify(args::Subparser &parser)
{
  args::Positional<std::string> iname(parser, "I", "Input file name");

  args::ValueFlag<std::string> oname(parser, "O", "Output file name", {"out", 'o'});
  args::ValueFlag<float> σ(parser, "S", "Noise standard deviation", {"std"}, 1.f);
  args::ValueFlag<std::string> dset(parser, "D", "Dataset to add noise to", {"dset"}, HD5::Keys::Noncartesian);

  ParseCommand(parser, iname);

  HD5::Reader reader(iname.Get());
  Cx5 ks = reader.readTensor<Cx5>(dset.Get());

  Cx5 noise(ks.dimensions());
  noise.setRandom<Eigen::internal::NormalRandomGenerator<std::complex<float>>>();
  ks += noise * noise.constant(σ.Get());

  HD5::Writer writer(OutName(iname.Get(), oname.Get(), "noisy"));
  Trajectory(reader).write(writer);
  writer.writeTensor(ks, dset.Get());

  return EXIT_SUCCESS;
}
