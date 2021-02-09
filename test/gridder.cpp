#include "../src/gridder.h"
#include "../src/log.h"
#include <catch2/catch.hpp>

TEST_CASE("Gridder with single point", "GRID-SINGLE")
{
  Log log(false);
  Info info{
      .matrix = {2, 2, 2},
      .voxel_size = {1, 1, 1},
      .read_points = 1,
      .read_gap = 0,
      .spokes_hi = 1,
      .spokes_lo = 0,
      .lo_scale = 1,
      .channels = 4,
      .volumes = 1,
  };
  float const osamp = 2.f;
  R3 traj(3, 1, 1);
  traj.setZero();

  SECTION("NN")
  {
    Kernel *kernel = new NearestNeighbour();
    Gridder gridder(info, traj, osamp, false, kernel, false, log);
    gridder.setDC(1.f);
    Cx2 rad(info.read_points, info.spokes_total());
    CHECK(rad.dimension(0) == 1);
    CHECK(rad.dimension(1) == 1);
    Cx3 cart = gridder.newGrid1();
    CHECK(cart.dimension(0) == 4);
    CHECK(cart.dimension(1) == 4);
    CHECK(cart.dimension(2) == 4);
    rad.setConstant(1.f);
    cart.setZero();
    gridder.toCartesian(rad, cart);
    CHECK(cart(2, 2, 2).real() == Approx(1.f));
    gridder.toNoncartesian(cart, rad);
    CHECK(rad(0, 0).real() == Approx(1.f));
  }

  SECTION("KB Estimate")
  {
    Kernel *kernel = new KaiserBessel(3, osamp, true);
    Gridder gridder(info, traj, osamp, true, kernel, false, log);
    Cx2 rad(info.read_points, info.spokes_total());
    CHECK(rad.dimension(0) == 1);
    CHECK(rad.dimension(1) == 1);
    Cx3 cart = gridder.newGrid1();
    CHECK(cart.dimension(0) == 4);
    CHECK(cart.dimension(1) == 4);
    CHECK(cart.dimension(2) == 4);
    rad.setConstant(1.f);
    cart.setZero();
    gridder.toCartesian(rad, cart);
    gridder.toNoncartesian(cart, rad);
    CHECK(rad(0, 0).real() == Approx(1.f));
  }

  SECTION("NN Multicoil")
  {
    Kernel *kernel = new NearestNeighbour();
    Gridder gridder(info, traj, osamp, false, kernel, false, log);
    gridder.setDC(1.f);
    Cx3 rad = info.noncartesianVolume();
    CHECK(rad.dimension(0) == info.channels);
    CHECK(rad.dimension(1) == info.read_points);
    Cx4 cart = gridder.newGrid();
    CHECK(cart.dimension(0) == info.channels);
    CHECK(cart.dimension(1) == 4);
    CHECK(cart.dimension(2) == 4);
    CHECK(cart.dimension(3) == 4);
    rad.setConstant(1.f);
    cart.setZero();
    gridder.toCartesian(rad, cart);
    CHECK(cart(0, 2, 2, 2).real() == Approx(1.f));
    CHECK(cart(1, 2, 2, 2).real() == Approx(1.f));
    CHECK(cart(2, 2, 2, 2).real() == Approx(1.f));
    CHECK(cart(3, 2, 2, 2).real() == Approx(1.f));
    gridder.toNoncartesian(cart, rad);
    CHECK(rad(0, 0, 0).real() == Approx(1.f));
    CHECK(rad(1, 0, 0).real() == Approx(1.f));
    CHECK(rad(2, 0, 0).real() == Approx(1.f));
    CHECK(rad(3, 0, 0).real() == Approx(1.f));
  }

  SECTION("KB Multicoil")
  {
    Kernel *kernel = new KaiserBessel(3, osamp, true);
    Gridder gridder(info, traj, osamp, false, kernel, false, log);
    gridder.setDC(1.f);
    Cx3 rad = info.noncartesianVolume();
    CHECK(rad.dimension(0) == info.channels);
    CHECK(rad.dimension(1) == info.read_points);
    Cx4 cart = gridder.newGrid();
    CHECK(cart.dimension(0) == info.channels);
    CHECK(cart.dimension(1) == 4);
    CHECK(cart.dimension(2) == 4);
    CHECK(cart.dimension(3) == 4);
    rad.setConstant(1.f);
    cart.setZero();
    gridder.toCartesian(rad, cart);
    CHECK(cart(0, 2, 2, 2).real() == Approx(0.32738f));
    CHECK(cart(1, 2, 2, 2).real() == Approx(0.32738f));
    CHECK(cart(2, 2, 2, 2).real() == Approx(0.32738f));
    CHECK(cart(3, 2, 2, 2).real() == Approx(0.32738f));
    gridder.toNoncartesian(cart, rad);
    CHECK(rad(0, 0, 0).real() == Approx(0.14331f).margin(1.e-5f));
    CHECK(rad(1, 0, 0).real() == Approx(0.14331f).margin(1.e-5f));
    CHECK(rad(2, 0, 0).real() == Approx(0.14331f).margin(1.e-5f));
    CHECK(rad(3, 0, 0).real() == Approx(0.14331f).margin(1.e-5f));
  }
}

TEST_CASE("Gridder with single spoke", "GRID-SPOKE")
{
  Log log(false);
  Info info{
      .matrix = {4, 4, 4},
      .voxel_size = {1, 1, 1},
      .read_points = 4,
      .read_gap = 0,
      .spokes_hi = 1,
      .spokes_lo = 0,
      .lo_scale = 1,
      .channels = 1,
      .volumes = 1,
  };
  float const osamp = 2.f;
  R3 traj(3, info.read_points, info.spokes_total());
  traj.setZero();
  traj(0, 1, 0) = 1.f / 3.f;
  traj(0, 2, 0) = 2.f / 3.f;
  traj(0, 3, 0) = 1.f;

  SECTION("NN")
  {
    Kernel *kernel = new NearestNeighbour();
    Gridder gridder(info, traj, osamp, false, kernel, false, log);
    gridder.setDC(1.f);
    Cx3 cart = gridder.newGrid1();
    CHECK(cart.dimension(0) == 8);
    CHECK(cart.dimension(1) == 8);
    CHECK(cart.dimension(2) == 8);
    Cx2 rad(info.read_points, info.spokes_total());
    rad.setConstant(1.f);
    cart.setZero();
    gridder.toCartesian(rad, cart);
    CHECK(cart(4, 4, 4).real() == Approx(1.f));
    CHECK(cart(5, 4, 4).real() == Approx(1.f));
    CHECK(cart(6, 4, 4).real() == Approx(1.f));
    CHECK(cart(7, 4, 4).real() == Approx(1.f));
    CHECK(cart(4, 5, 5).real() == Approx(0.f));
    CHECK(cart(4, 5, 4).real() == Approx(0.f));
    CHECK(cart(4, 4, 5).real() == Approx(0.f));
    gridder.toNoncartesian(cart, rad);
    CHECK(rad(0, 0).real() == Approx(1.f));
  }

  SECTION("KB Estimate")
  {
    Kernel *kernel = new KaiserBessel(3, osamp, false);
    Gridder gridder(info, traj, osamp, true, kernel, false, log);
    Cx3 cart = gridder.newGrid1();
    CHECK(cart.dimension(0) == 8);
    CHECK(cart.dimension(1) == 8);
    CHECK(cart.dimension(2) == 8);
    Cx2 rad(info.read_points, info.spokes_total());
    rad.setConstant(1.f);
    cart.setZero();
    gridder.toCartesian(rad, cart);
    gridder.toNoncartesian(cart, rad);
    CHECK(rad(0, 0).real() == Approx(1.f).margin(1.e-1f));
  }
}
