#include "nufft.hpp"

#include "loop.hpp"
#include "rank.hpp"

namespace rl {

template <size_t NDim>
NUFFTOp<NDim>::NUFFTOp(
  std::unique_ptr<GridBase<Cx, NDim>> gridder, Sz<NDim> const matrix, std::optional<SDC::Functor> sdc, bool toeplitz)
  : Parent("NUFFTOp", Concatenate(FirstN<2>(gridder->inputDimensions()), matrix), gridder->outputDimensions())
  , gridder_{std::move(gridder)}
  , fft_{gridder_->input()}
  , pad_{gridder_->input(), matrix, LastN<NDim>(gridder_->inputDimensions()), FirstN<2>(gridder_->inputDimensions())}
  , apo_{pad_.inputDimensions(), gridder_.get()}
  , sdc_{sdc}
{
  Log::Print<Log::Level::High>(
    "NUFFT Input Dims {} Output Dims {} Grid Dims {}", inputDimensions(), outputDimensions(), gridder_->inputDimensions());
  if (toeplitz) {
    Log::Print("Calculating Töplitz embedding");
    tf_.resize(inputDimensions());
    tf_.setConstant(1.f);
    if (sdc_) {
      Output temp = (*sdc)(ConstMap(forward(tf_)));
      tf_ = adjoint(temp);
    } else {
      tf_ = adjoint(forward(tf_));
    }
  }
}

template <size_t NDim>
auto NUFFTOp<NDim>::forward(InputMap x) const -> OutputMap
{
  auto const time = this->startForward(x);
  auto result = gridder_->forward(fft_.forward(pad_.forward(apo_.forward(x))));
  this->finishForward(result, time);
  return result;
}

template <size_t NDim>
auto NUFFTOp<NDim>::adjoint(OutputMap y) const -> InputMap
{
  auto const time = this->startAdjoint(y);
  auto result = apo_.adjoint(pad_.adjoint(fft_.adjoint(gridder_->adjoint(sdc_ ? (*sdc_)(ConstMap(y)) : y))));
  this->finishAdjoint(result, time);
  return result;
}

template <size_t NDim>
auto NUFFTOp<NDim>::adjfwd(InputMap x) const -> InputMap
{
  auto const start = Log::Now();
  Input result(inputDimensions());
  if (tf_.size() == 0) {
    result.device(Threads::GlobalDevice()) = adjoint(forward(x));
  } else {
    auto temp = fft_.forward(pad_.forward(x));
    temp *= tf_;
    result.device(Threads::GlobalDevice()) = pad_.adjoint(fft_.adjoint(temp));
  }
  LOG_DEBUG("Finished NUFFT adjoint*forward. Norm {}->{}. Time {}", Norm(x), Norm(result), Log::ToNow(start));
  return result;
}

template <size_t NDim>
auto NUFFTOp<NDim>::fft() const -> FFTOp<NDim + 2, NDim> const &
{
  return fft_;
};

template struct NUFFTOp<2>;
template struct NUFFTOp<3>;

std::unique_ptr<Operator<Cx, 5, 4>> make_nufft(
  Trajectory const &traj,
  std::string const &ktype,
  float const osamp,
  Index const nC,
  Sz3 const matrix,
  std::optional<SDC::Functor> sdc,
  std::optional<Re2> basis,
  bool const toeplitz)
{
  if (traj.nDims() == 2) {
    Log::Print<Log::Level::Debug>("Creating 2D Multi-slice NUFFT");
    auto grid = make_grid<Cx, 2>(traj, ktype, osamp * (toeplitz ? 2.f : 1.f), nC, basis);
    NUFFTOp<2> nufft2(std::move(grid), FirstN<2>(matrix), sdc, toeplitz);
    return std::make_unique<LoopOp<NUFFTOp<2>>>(nufft2, traj.info().matrix[2]);
  } else {
    Log::Print<Log::Level::Debug>("Creating full 3D NUFFT");
    auto grid = make_grid<Cx, 3>(traj, ktype, osamp * (toeplitz ? 2.f : 1.f), nC, basis);
    return std::make_unique<IncreaseOutputRank<NUFFTOp<3>>>(
      std::make_unique<NUFFTOp<3>>(std::move(grid), matrix, sdc, toeplitz));
  }
}

} // namespace rl
