#include "parse_args.hpp"
#include "basis/basis.hpp"
#include "io/hd5.hpp"
#include "log.hpp"
#include "tensorOps.hpp"
#include "threads.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <scn/scan.h>

using namespace rl;

namespace {
std::unordered_map<int, Log::Level> levelMap{
  {0, Log::Level::None}, {1, Log::Level::Ephemeral}, {2, Log::Level::Standard}, {3, Log::Level::Debug}};
}

void Array2fReader::operator()(std::string const &name, std::string const &value, Eigen::Array2f &v)
{
  if (auto result = scn::scan<float, float>(value, "{},{}")) {
    v[0] = std::get<0>(result->values());
    v[1] = std::get<1>(result->values());
  } else {
    Log::Fail("Could not read vector for {} from value {}", name, value);
  }
}

void Array3fReader::operator()(std::string const &name, std::string const &value, Eigen::Array3f &v)
{
  if (auto result = scn::scan<float, float, float>(value, "{},{},{}")) {
    v[0] = std::get<0>(result->values());
    v[1] = std::get<1>(result->values());
    v[2] = std::get<2>(result->values());
  } else {
    Log::Fail("Could not read vector for {} from value {}", name, value);
  }
}

void Vector3fReader::operator()(std::string const &name, std::string const &value, Eigen::Vector3f &v)
{
  if (auto result = scn::scan<float, float, float>(value, "{},{},{}")) {
    v[0] = std::get<0>(result->values());
    v[1] = std::get<1>(result->values());
    v[2] = std::get<2>(result->values());
  } else {
    Log::Fail("Could not read vector for {} from value {}", name, value);
  }
}

template <typename T>
void VectorReader<T>::operator()(std::string const &name, std::string const &input, std::vector<T> &values)
{
  auto result = scn::scan<T>(input, "{}");
  if (result) {
    // Values will have been default initialized. Reset
    values.clear();
    values.push_back(result->value());
    while ((result = scn::scan<T>(result->range(), ",{}"))) {
      values.push_back(result->value());
    }
  } else {
    Log::Fail("Could not read argument for {}", name);
  }
}

template struct VectorReader<float>;
template struct VectorReader<Index>;

template <int N>
void SzReader<N>::operator()(std::string const &name, std::string const &value, Sz<N> &sz)
{
  Index ind = 0;
  if (auto result = scn::scan<Index>(value, "{}")) {
    sz[ind] = result->value();
    for (ind = 1; ind < N; ind++) {
      result = scn::scan<Index>(result->range(), ",{}");
      if (!result) { Log::Fail("Could not read {} from '{}'", name, value); }
      sz[ind] = result->value();
    }
  } else {
    Log::Fail("Could not read {} from '{}'", name, value);
  }
}

template struct SzReader<2>;
template struct SzReader<3>;
template struct SzReader<4>;

CoreOpts::CoreOpts(args::Subparser &parser)
  : iname(parser, "F", "Input HD5 file")
  , oname(parser, "O", "Override output name", {'o', "out"})
  , basisFile(parser, "B", "Read basis from file", {"basis", 'b'})
  , scaling(parser, "S", "Data scaling (otsu/bart/number)", {"scale"}, "otsu")
  , fov(parser, "FOV", "Final FoV in mm (x,y,z)", {"fov"}, Eigen::Array3f::Zero())
  , ndft(parser, "D", "Use NDFT instead of NUFFT", {"ndft"})
  , residImage(parser, "R", "Write residuals in image space", {"resid-image"})
  , residKSpace(parser, "R", "Write residuals in k-space", {"resid-kspace"})
  , keepTrajectory(parser, "", "Keep the trajectory in the output file", {"keep"})
{
}

args::Group    global_group("GLOBAL OPTIONS");
args::HelpFlag help(global_group, "H", "Show this help message", {'h', "help"});
args::MapFlag<int, Log::Level>
                             verbosity(global_group, "V", "Log level 0-3", {'v', "verbosity"}, levelMap, Log::Level::Standard);
args::ValueFlag<std::string> debug(global_group, "F", "Write debug images to file", {"debug"});
args::ValueFlag<Index>       nthreads(global_group, "N", "Limit number of threads", {"nthreads"});

void SetLogging(std::string const &name)
{
  if (verbosity) {
    Log::SetLevel(verbosity.Get());
  } else if (char *const env_p = std::getenv("RL_VERBOSITY")) {
    Log::SetLevel(levelMap.at(std::atoi(env_p)));
  }

  Log::Print("Welcome to RIESLING");
  Log::Print("Command: {}", name);

  if (debug) { Log::SetDebugFile(debug.Get()); }
}

void SetThreadCount()
{
  if (nthreads) {
    Threads::SetGlobalThreadCount(nthreads.Get());
  } else if (char *const env_p = std::getenv("RL_THREADS")) {
    Threads::SetGlobalThreadCount(std::atoi(env_p));
  }
  Log::Print("Using {} threads", Threads::GlobalThreadCount());
}

void ParseCommand(args::Subparser &parser)
{
  parser.Parse();
  SetLogging(parser.GetCommand().Name());
  SetThreadCount();
}

void ParseCommand(args::Subparser &parser, args::Positional<std::string> &iname)
{
  ParseCommand(parser);
  if (!iname) { throw args::Error("No input file specified"); }
}

void ParseCommand(args::Subparser &parser, args::Positional<std::string> &iname, args::Positional<std::string> &oname)
{
  ParseCommand(parser);
  if (!iname) { throw args::Error("No input file specified"); }
  if (!oname) { throw args::Error("No output file specified"); }
}

std::string OutName(std::string const &iName, std::string const &oName, std::string const &suffix, std::string const &extension)
{
  return fmt::format("{}{}.{}", oName.empty() ? std::filesystem::path(iName).filename().replace_extension().string() : oName,
                     suffix.empty() ? "" : fmt::format("-{}", suffix), extension);
}

void WriteOutput(CoreOpts                           &opts,
                 rl::Cx5 const                      &img,
                 std::string const                  &suffix,
                 rl::Trajectory const               &traj,
                 std::string const                  &log,
                 rl::Cx5 const                      &residImage,
                 rl::Cx5 const                      &residKSpace,
                 std::map<std::string, float> const &meta)
{
  auto const  fname = OutName(opts.iname.Get(), opts.oname.Get(), suffix, "h5");
  HD5::Writer writer(fname);
  writer.writeTensor(HD5::Keys::Image, img.dimensions(), img.data(), HD5::Dims::Image);
  writer.writeMeta(meta);
  writer.writeInfo(traj.info());
  if (opts.keepTrajectory) {
    writer.writeTensor(HD5::Keys::Trajectory, traj.points().dimensions(), traj.points().data(), HD5::Dims::Trajectory);
  }
  writer.writeString("log", log);
  if (opts.residImage) { writer.writeTensor(HD5::Keys::ResidualImage, residImage.dimensions(), residImage.data()); }
  if (opts.residKSpace) { writer.writeTensor(HD5::Keys::ResidualKSpace, residKSpace.dimensions(), residKSpace.data()); }
  Log::Print("Wrote output file {}", fname);
}
