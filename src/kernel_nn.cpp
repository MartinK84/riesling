#include "kernel_nn.h"

NearestNeighbour::NearestNeighbour(long const w)
    : w_{w}
{
}

long NearestNeighbour::radius() const
{
  return w_ / 2; // Can safely grid to edge with this
}

Sz3 NearestNeighbour::start() const
{
  return Sz3{-w_ / 2, -w_ / 2, -w_ / 2};
};

Sz3 NearestNeighbour::size() const
{
  return Sz3{w_, w_, w_};
};

Cx3 NearestNeighbour::kspace(Point3 const &offset) const
{
  Cx3 w(w_, w_, w_);
  w.setZero();
  w(w_ / 2, w_ / 2, w_ / 2) = 1.f;
  return w;
}

Cx3 NearestNeighbour::image(Point3 const &offset, Dims3 const &) const
{
  Cx3 w(w_, w_, w_);
  w.setConstant(1.f / sqrt(w_ * w_ * w_)); // Parseval's theorem
  return w;
}

Cx4 NearestNeighbour::sensitivity(Point3 const &x, Cx4 const &s) const
{
  // Null op, return the sensitivity kernels
  return s;
}

ApodizeFunction NearestNeighbour::apodization(Dims3 const &dims) const
{
  ApodizeFunction nullOp = [](Cx3 &, bool const) {};
  return nullOp;
}