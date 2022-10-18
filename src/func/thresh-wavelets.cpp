#include "thresh-wavelets.hpp"

namespace rl {

ThresholdWavelets::ThresholdWavelets(Sz4 const dims, Index const W, Index const L)
  : Prox<Cx4>()
  , pad_{LastN<3>(dims), LastN<3>(Wavelets::PaddedDimensions(dims, L)), FirstN<1>(dims)}
  , waves_{pad_.outputDimensions(), W, L}
{
}

auto ThresholdWavelets::operator()(float const λ, Eigen::TensorMap<Cx4 const>x) const -> Eigen::TensorMap<Cx4>
{
  return pad_.adjoint(waves_.adjoint(thresh_(λ, ConstMap(waves_.forward(pad_.forward(x))))));
}

} // namespace rl
