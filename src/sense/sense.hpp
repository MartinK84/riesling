#pragma once

#include "io/hd5.hpp"
#include "log.hpp"
#include "op/make_grid.hpp"
#include "parse_args.hpp"
#include "trajectory.hpp"

namespace rl {
namespace SENSE {

struct Opts
{
  Opts(args::Subparser &parser);
  args::ValueFlag<std::string> type;
  args::ValueFlag<Index> volume, frame;
  args::ValueFlag<float> res, λ, fov;
  args::ValueFlag<Index> kRad, calRad, gap;
  args::ValueFlag<float> threshold;
};

//! Convenience function called from recon commands to get SENSE maps
Cx4 Choose(Opts &opts, CoreOpts &core, Trajectory const &t, std::optional<Re2> const &basis, HD5::Reader &reader);

} // namespace SENSE
} // namespace rl