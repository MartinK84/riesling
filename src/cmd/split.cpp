#include "types.h"

#include "io/io.h"
#include "log.h"
#include "parse_args.h"
#include "tensorOps.h"

int main_split(args::Subparser &parser)
{
  args::Positional<std::string> iname(parser, "FILE", "HD5 file to recon");
  args::ValueFlag<std::string> oname(parser, "OUTPUT", "Override output name", {'o', "out"});
  args::ValueFlag<Index> lores(parser, "N", "Extract first N spokes as lo-res", {'l', "lores"}, 0);
  args::ValueFlag<Index> spoke_stride(parser, "S", "Hi-res stride", {"stride"}, 1);
  args::ValueFlag<Index> spoke_size(parser, "SZ", "Size of hi-res spokes to keep", {"size"});
  args::ValueFlag<Index> nspokes(parser, "SPOKES", "Spokes per segment", {"n", "nspokes"});
  args::ValueFlag<Index> nE(parser, "E", "Break into N echoes", {"echoes"}, 1);
  args::ValueFlag<Index> spe(parser, "S", "Spokes per echo", {"spe"}, 1);
  args::ValueFlag<float> ds(parser, "DS", "Downsample by factor", {"ds"}, 1.0);
  args::ValueFlag<Index> step(parser, "STEP", "Step size", {"s", "step"}, 0);

  ParseCommand(parser, iname);

  HD5::RieslingReader reader(iname.Get());
  auto traj = reader.trajectory();

  if (nE && spe) {
    Index const sps = spe.Get() * nE.Get();
    if (traj.info().spokes % spe != 0) {
      Log::Fail(FMT_STRING("SPE {} does not divide spokes {} cleanly"), sps, traj.info().spokes);
    }
    Index const segs = std::ceil(static_cast<float>(traj.info().spokes) / sps);
    Log::Print(
      FMT_STRING("Adding info for {} echoes with {} spokes per echo, {} per segment, {} segments"),
      nE.Get(),
      spe.Get(),
      sps,
      segs);
    I1 e(nE.Get());
    std::iota(e.data(), e.data() + nE.Get(), 0);
    I1 echoes = e.reshape(Sz2{1, nE.Get()})
                  .broadcast(Sz2{spe.Get(), 1})
                  .reshape(Sz1{sps})
                  .broadcast(Sz1{segs})
                  .slice(Sz1{0}, Sz1{traj.info().spokes});
    Info info = traj.info();
    info.echoes = nE.Get();
    traj = Trajectory(info, traj.points(), echoes);
  }

  Cx4 ks = reader.readTensor<Cx4>(HD5::Keys::Noncartesian);

  if (lores) {
    if ((lores.Get() == 0) || (std::abs(lores.Get()) > traj.info().spokes)) {
      Log::Fail(FMT_STRING("Invalid number of low-res spokes {}"), lores.Get());
    }

    // Cope with WASPI at end
    Index const lo_st = (lores.Get() < 0) ? (traj.info().spokes + lores.Get()) : 0;
    Index const hi_st = (lores.Get() < 0) ? 0 : lores.Get();

    Info lo_info = traj.info();
    lo_info.spokes = std::abs(lores.Get());
    lo_info.echoes = 1; // Don't even bother
    Log::Print(FMT_STRING("Extracting spokes {}-{} as low-res"), lo_st, lo_st + lo_info.spokes);

    // The trajectory still thinks WASPI is at the beginning. Don't use echoes
    Trajectory lo_traj(
      lo_info, traj.points().slice(Sz3{0, 0, 0}, Sz3{3, lo_info.read_points, lo_info.spokes}));
    Cx4 lo_ks = ks.slice(
      Sz4{0, 0, lo_st, 0},
      Sz4{lo_info.channels, lo_info.read_points, lo_info.spokes, lo_info.volumes});

    auto info = traj.info();
    info.spokes -= lo_info.spokes;

    traj = Trajectory(
      info,
      R3(traj.points().slice(Sz3{0, 0, lo_info.spokes}, Sz3{3, info.read_points, info.spokes})),
      I1(traj.echoes().slice(Sz1{0}, Sz1{info.spokes})));
    ks = Cx4(ks.slice(
      Sz4{0, 0, hi_st, 0}, Sz4{info.channels, info.read_points, info.spokes, info.volumes}));

    if (ds) {
      lo_traj = lo_traj.downsample(ds.Get(), lo_ks);
    }
    HD5::Writer writer(OutName(iname.Get(), oname.Get(), "lores"));
    writer.writeTrajectory(lo_traj);
    writer.writeTensor(lo_ks, HD5::Keys::Noncartesian);
  }

  if (ds) {
    traj = traj.downsample(ds.Get(), ks);
  }

  if (spoke_stride) {
    auto info = traj.info();
    ks = Cx4(ks.stride(Sz4{1, 1, spoke_stride.Get(), 1}));
    info.spokes = ks.dimension(2);
    traj = Trajectory(
      info,
      traj.points().stride(Sz3{1, 1, spoke_stride.Get()}),
      traj.echoes().stride(Sz1{spoke_stride.Get()}));
  }

  if (spoke_size) {
    auto info = traj.info();
    info.spokes = spoke_size.Get();
    ks = Cx4(
      ks.slice(Sz4{0, 0, 0, 0}, Sz4{info.channels, info.read_points, info.spokes, info.volumes}));
    traj = Trajectory(
      info,
      traj.points().slice(Sz3{0, 0, 0}, Sz3{3, info.read_points, info.spokes}),
      traj.echoes().slice(Sz1{0}, Sz1{info.spokes}));
  }

  if (nspokes) {
    auto info = traj.info();
    int const ns = nspokes.Get();
    int const spoke_step = step ? step.Get() : ns;
    int const num_full_int = static_cast<int>(info.spokes * 1.f / ns);
    int const num_int = static_cast<int>((num_full_int - 1) * ns * 1.f / spoke_step + 1);
    Log::Print(
      FMT_STRING("Interleaves: {} Spokes per interleave: {} Step: {}"), num_int, ns, spoke_step);
    int rem_spokes = info.spokes - num_full_int * ns;
    if (rem_spokes > 0) {
      Log::Print(FMT_STRING("Warning! Last interleave will have {} extra spokes."), rem_spokes);
    }

    for (int int_idx = 0; int_idx < num_int; int_idx++) {
      int const idx0 = spoke_step * int_idx;
      int const n = ns + (int_idx == (num_int - 1) ? rem_spokes : 0);
      info.spokes = n;
      HD5::Writer writer(
        OutName(iname.Get(), oname.Get(), fmt::format(FMT_STRING("hires-{:02d}"), int_idx)));
      writer.writeTrajectory(Trajectory(
        info,
        traj.points().slice(Sz3{0, 0, idx0}, Sz3{3, info.read_points, n}),
        traj.echoes().slice(Sz1{idx0}, Sz1{n})));
      writer.writeTensor(
        Cx4(ks.slice(Sz4{0, 0, idx0, 0}, Sz4{info.channels, info.read_points, n, info.volumes})),
        HD5::Keys::Noncartesian);
    }
  } else {
    HD5::Writer writer(OutName(iname.Get(), oname.Get(), "hires"));
    writer.writeTrajectory(traj);
    writer.writeTensor(ks, HD5::Keys::Noncartesian);
  }

  return EXIT_SUCCESS;
}