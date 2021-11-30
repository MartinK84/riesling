#include "io.h"
#include "parse_args.h"
#include "tensorOps.h"
#include "threads.h"
#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <scn/scn.h>

namespace {
std::unordered_map<int, Log::Level> levelMap{
  {0, Log::Level::None}, {1, Log::Level::Info}, {2, Log::Level::Debug}, {3, Log::Level::Images}};
}

void Vector3fReader::operator()(
  std::string const &name, std::string const &value, Eigen::Vector3f &v)
{
  float x, y, z;
  auto result = scn::scan(value, "{},{},{}", x, y, z);
  if (!result) {
    Log::Fail("Could not read vector for {} from value {} because {}", name, value, result.error());
  }
  v.x() = x;
  v.y() = y;
  v.z() = z;
}

void VectorReader::operator()(
  std::string const &name, std::string const &input, std::vector<float> &values)
{
  float val;
  auto result = scn::scan(input, "{}", val);
  if (result) {
    values.push_back(val);
  } else {
    Log::Fail("Could not read argument for {}", name);
  }
  while ((result = scn::scan(result.range(), ",{}", val))) {
    values.push_back(val);
  }
}

args::Group global_group("GLOBAL OPTIONS");
args::HelpFlag help(global_group, "HELP", "Show this help message", {'h', "help"});
args::Flag verbose(global_group, "VERBOSE", "Talk more", {'v', "verbose"});
args::MapFlag<int, Log::Level> verbosity(
  global_group,
  "VERBOSITY",
  "Talk even more (values 0-3, see documentation)",
  {"verbosity"},
  levelMap);
args::ValueFlag<Index> nthreads(global_group, "THREADS", "Limit number of threads", {"nthreads"});

Log ParseCommand(args::Subparser &parser, args::Positional<std::string> &iname)
{
  parser.Parse();
  Log::Level const level =
    verbosity ? verbosity.Get() : (verbose ? Log::Level::Info : Log::Level::None);

  Log log(level);
  log.info(FMT_STRING("Starting: {}"), parser.GetCommand().Name());
  if (!iname) {
    throw args::Error("No input file specified");
  }
  if (nthreads) {
    log.info("Using {} threads", nthreads.Get());
    Threads::SetGlobalThreadCount(nthreads.Get());
  }
  return log;
}

Log ParseCommand(args::Subparser &parser)
{
  parser.Parse();
  Log::Level const level =
    verbosity ? verbosity.Get() : (verbose ? Log::Level::Info : Log::Level::None);

  Log log(level);
  if (nthreads) {
    Threads::SetGlobalThreadCount(nthreads.Get());
  }
  log.info(FMT_STRING("Starting operation: {}"), parser.GetCommand().Name());
  return log;
}

std::string OutName(
  std::string const &iName,
  std::string const &oName,
  std::string const &suffix,
  std::string const &extension)
{
  return fmt::format(
    FMT_STRING("{}-{}.{}"),
    oName.empty() ? std::filesystem::path(iName).filename().replace_extension().string() : oName,
    suffix,
    extension);
}
