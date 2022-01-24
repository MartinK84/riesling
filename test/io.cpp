#include "../src/io.h"
#include "../src/log.h"
#include "../src/tensorOps.h"
#include "../src/traj_spirals.h"

#include <filesystem>

#include <catch2/catch.hpp>

void Dummy(std::filesystem::path const &fname)
{
  HD5::RieslingReader reader(fname);
}

TEST_CASE("io", "[io]")
{
  Index const M = 4;
  float const os = 2.f;
  Info const info{
    .type = Info::Type::ThreeD,
    .matrix = Eigen::Array3l::Constant(M),
    .channels = 1,
    .read_points = Index(os * M / 2),
    .spokes = Index(M * M),
    .volumes = 2,
    .echoes = 1,
    .tr = 1.f,
    .voxel_size = Eigen::Array3f::Constant(1.f),
    .origin = Eigen::Array3f::Constant(0.f),
    .direction = Eigen::Matrix3f::Identity()};
  auto const points = ArchimedeanSpiral(info.read_points, info.spokes);
  Trajectory const traj(info, points);
  Cx4 refData(info.channels, info.read_points, info.spokes, info.volumes);
  refData.setConstant(1.f);

  SECTION("Basic")
  {
    std::filesystem::path const fname("test.h5");
    { // Use destructor to ensure it is written
      HD5::Writer writer(fname);
      writer.writeTrajectory(traj);
      writer.writeTensor(refData, HD5::Keys::Noncartesian);
    }
    CHECK(std::filesystem::exists(fname));

    HD5::RieslingReader reader(fname);
    auto const checkInfo = reader.trajectory().info();
    CHECK(checkInfo.channels == info.channels);
    CHECK(checkInfo.read_points == info.read_points);
    CHECK(checkInfo.spokes == info.spokes);
    CHECK(checkInfo.volumes == info.volumes);

    auto const checkData = reader.noncartesian(0);
    CHECK(Norm(checkData - refData) == Approx(0.f).margin(1.e-9));

    std::filesystem::remove(fname);
  }

  SECTION("Failures")
  {
    std::filesystem::path const fname("test-failures.h5");

    { // Use destructor to ensure it is written
      HD5::Writer writer(fname);
      writer.writeTrajectory(traj);
      writer.writeTensor(
        Cx4(refData.reshape(Sz4{info.volumes, info.read_points, info.spokes, info.channels})),
        HD5::Keys::Noncartesian);
    }
    CHECK(std::filesystem::exists(fname));
    CHECK_THROWS_AS(Dummy(fname), Log::Failure);
    std::filesystem::remove(fname);
  }
}