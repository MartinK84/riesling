#include "io_hd5.h"
#include "io_nifti.h"
#include "log.h"
#include "parse_args.h"
#include "slab_correct.h"
#include "threads.h"
#include "types.h"
#include "zinfandel.h"
#include <filesystem>

int main_zinfandel(args::Subparser &parser)
{
  args::Positional<std::string> fname(parser, "INPUT FILE", "Input radial k-space to fill");
  args::ValueFlag<std::string> oname(
      parser, "OUTPUT NAME", "Name of output .h5 file", {"out", 'o'});
  args::ValueFlag<long> volume(parser, "VOLUME", "Only recon this volume", {"vol"}, -1);
  args::ValueFlag<long> gap(
      parser, "DEAD-TIME GAP", "Set gap value (default use header value)", {'g', "gap"}, -1);
  args::ValueFlag<long> src(
      parser, "SOURCES", "Number of ZINFANDEL sources (default 4)", {"src"}, 4);
  args::ValueFlag<long> spokes(
      parser, "CAL SPOKES", "Number of spokes to use for calibration (default 5)", {"spokes"}, 5);
  args::ValueFlag<long> read(
      parser, "CAL READ", "Read calibration size (default all)", {"read"}, 0);
  args::ValueFlag<float> l1(
      parser, "LAMBDA", "Tikhonov regularization (default 0)", {"lambda"}, 0.f);
  args::ValueFlag<float> pw(
      parser, "PULSE WIDTH", "Pulse-width for slab profile correction", {"pw"}, 0.f);
  args::ValueFlag<float> rbw(
      parser, "BANDWIDTH", "Read-out bandwidth for slab profile correction (kHz)", {"rbw"}, 0.f);
  args::Flag twostep(parser, "TWOSTEP", "Use two step method", {"two", '2'});
  Log log = ParseCommand(parser, fname);

  HD5Reader reader(fname.Get(), log);
  auto info = reader.info();
  long const gap_sz = gap ? gap.Get() : info.read_gap;
  auto const traj = reader.readTrajectory();

  auto out_info = info;
  out_info.read_gap = 0;
  if (volume) {
    out_info.volumes = 1;
  }

  HD5Writer writer(OutName(fname, oname, "zinfandel", "h5"), log);
  writer.writeInfo(out_info);
  writer.writeTrajectory(traj);
  writer.writeMeta(reader.readMeta());

  Cx3 rad_ks = info.noncartesianVolume();
  for (auto const &iv : WhichVolumes(volume.Get(), info.volumes)) {
    reader.readData(iv, rad_ks);
    zinfandel(
        gap_sz, src.Get(), spokes.Get(), read.Get(), twostep ? l1.Get() : 0.f, traj, rad_ks, log);
    if (twostep) {
      zinfandel2(gap_sz, src.Get(), read.Get(), l1.Get(), traj, rad_ks, log);
    }
    if (pw && rbw) {
      slab_correct(out_info, pw.Get(), rbw.Get(), rad_ks, log);
    }
    writer.writeData(volume ? 0 : iv, rad_ks);
  }
  log.info("Finished");
  return EXIT_SUCCESS;
}
