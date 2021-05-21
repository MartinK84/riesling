#include "sense.h"

#include "fft3n.h"
#include "gridder.h"
#include "io_hd5.h"
#include "sdc.h"
#include "tensorOps.h"
#include "threads.h"

Cx4 Direct(
    Info const &info,
    R3 const &traj,
    float const os,
    Kernel *const kernel,
    bool const shrink,
    std::string const &sdc,
    float const threshold,
    Cx3 const &data,
    Log &log)
{
  // Grid and heavily smooth each coil image, accumulate combined image
  float const sense_res = 12.f;
  log.info("Creating SENSE maps.");
  Gridder gridder(info, traj, os, kernel, false, log, sense_res, shrink);
  SDC::Load(sdc, info, traj, kernel, gridder, log);
  Cx4 grid = gridder.newGrid();
  R3 rss(gridder.gridDims());
  FFT3N fftN(grid, log);
  grid.setZero();
  rss.setZero();
  gridder.toCartesian(data, grid);
  fftN.reverse();
  rss.device(Threads::GlobalDevice()) = (grid * grid.conjugate()).real().sum(Sz1{0}).sqrt();
  log.info("Normalizing channel images");
  if (threshold > 0) {
    R0 max = rss.maximum();
    float const threshVal = threshold * max();
    log.info(FMT_STRING("Thresholding RSS below {} intensity"), threshVal);
    auto const start = log.now();
    B3 const thresholded = (rss > threshVal);
    grid.device(Threads::GlobalDevice()) =
        Tile(thresholded, info.channels)
            .select(grid / Tile(rss, info.channels).cast<Cx>(), grid.constant(0.f));
    log.debug("SENSE Thresholding: {}", log.toNow(start));
  } else {
    grid.device(Threads::GlobalDevice()) = grid / Tile(rss, info.channels).cast<Cx>();
  }
  log.info("Finished SENSE maps");
  return grid;
}

Cx4 SENSE(
    std::string const &method,
    Info const &info,
    R3 const &traj,
    float const os,
    Kernel *const kernel,
    bool const shrink,
    std::string const &sdc,
    float const threshold,
    Cx3 const &data,
    Log &log)
{
  if (method == "direct") {
    return Direct(info, traj, os, kernel, shrink, sdc, threshold, data, log);
  } else {
    log.info("Loading SENSE data from {}", method);
    HD5::Reader reader(method, log);
    Info const &cal_info = reader.info();
    R3 const cal_traj = reader.readTrajectory();
    Cx3 cal_data = cal_info.noncartesianVolume();
    reader.readNoncartesian(0, cal_data);
    return Direct(cal_info, cal_traj, os, kernel, shrink, "pipe", threshold, cal_data, log);
  }
}