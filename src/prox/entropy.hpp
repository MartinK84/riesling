#pragma once

#include "prox.hpp"

namespace rl {

template<typename Tensor>
struct Entropy final : Prox<Tensor> {
    Entropy(float const λ);
    auto operator()(float const α, Eigen::TensorMap<Tensor const>) const -> Tensor;
private:
    float λ_;
};

struct NMREntropy final : Prox<Cx4> {
    NMREntropy(float const λ);
    auto operator()(float const α, Eigen::TensorMap<Cx4 const>) const -> Cx4;
private:
    float λ_;
};

} // namespace rl
