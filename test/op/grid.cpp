#include "../../src/op/grid.hpp"
#include "log.hpp"
#include "tensorOps.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace rl;
using namespace Catch;

TEST_CASE("Grid Basic", "[grid]")
{
  Log::SetLevel(Log::Level::Testing);
  Threads::SetGlobalThreadCount(1);
  Index const M = GENERATE(7, 15, 16, 31, 32);
  auto const matrix = Sz3{M, M, 1};
  Re3 points(3, 3, 1);
  points.setZero();
  points(0, 0, 0) = -0.4f * M;
  points(1, 0, 0) = -0.4f * M;
  points(0, 2, 0) = 0.4f * M;
  points(1, 2, 0) = 0.4f * M;
  Trajectory const traj(points, matrix);

  float const osamp = GENERATE(2.f, 2.7f, 3.f);
  std::string const ktype = GENERATE("ES7");
  auto basis = IdBasis<float>();
  auto grid = Grid<float, 2>::Make(traj, ktype, osamp, 1, basis);
  Re3 noncart(grid->oshape);
  Re4 cart(grid->ishape);
  noncart.setConstant(1.f);
  cart = grid->adjoint(noncart);
  INFO("M " << M << " OS " << osamp << " " << ktype);
  CHECK(Norm(cart) == Approx(Norm(noncart)).margin(1e-2f));
  noncart = grid->forward(cart);
  CHECK(Norm(noncart) == Approx(Norm(cart)).margin(1e-2f));
}
