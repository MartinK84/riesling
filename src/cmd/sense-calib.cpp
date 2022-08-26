#include "types.h"

#include "io/hd5.hpp"
#include "log.h"
#include "op/gridBase.hpp"
#include "parse_args.h"
#include "sdc.h"
#include "sense.h"

using namespace rl;

int main_sense_calib(args::Subparser &parser)
{
  CoreOpts core(parser);
  SDC::Opts sdcOpts(parser);
  args::ValueFlag<Index> volume(parser, "V", "SENSE calibration volume", {"sense-vol"}, -1);
  args::ValueFlag<Index> frame(parser, "F", "SENSE calibration frame", {"sense-frame"}, 0);
  args::ValueFlag<float> res(parser, "R", "SENSE calibration res (12 mm)", {"sense-res"}, 12.f);
  args::ValueFlag<float> λ(parser, "L", "SENSE regularization", {"sense-lambda"}, 0.f);
  args::ValueFlag<float> fov(parser, "FOV", "FoV in mm (default 256 mm)", {"fov"}, 256.f);

  ParseCommand(parser, core.iname);

  HD5::RieslingReader reader(core.iname.Get());
  auto const traj = reader.trajectory();
  auto const &info = traj.info();
  auto const basis = ReadBasis(core.basisFile);
  auto gridder = make_grid<Cx>(traj, core.ktype.Get(), core.osamp.Get(), info.channels, basis);
  auto const sdc = SDC::Choose(sdcOpts, traj, core.osamp.Get());
  Cx3 const data = sdc->adjoint(reader.noncartesian(ValOrLast(volume.Get(), info.volumes)));
  Cx4 sense = SENSE::SelfCalibration(info, gridder.get(), fov.Get(), res.Get(), λ.Get(), frame.Get(), data);

  auto const fname = OutName(core.iname.Get(), core.oname.Get(), "sense", "h5");
  HD5::Writer writer(fname);
  writer.writeTensor(sense, "sense");

  return EXIT_SUCCESS;
}