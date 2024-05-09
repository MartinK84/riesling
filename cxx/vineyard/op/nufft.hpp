#pragma once

#include "op/tensorop.hpp"

#include "apodize.hpp"
#include "fft/fft.hpp"
#include "grid.hpp"
#include "pad.hpp"

namespace rl {

template <int NDim>
struct NUFFTOp final : TensorOperator<Cx, NDim + 2, 3>
{
  OP_INHERIT(Cx, NDim + 2, 3)
  NUFFTOp(std::shared_ptr<Grid<Cx, NDim>>        gridder,
          Sz<NDim> const                         matrix,
          Index const                            batches = 1);
  OP_DECLARE()

  std::shared_ptr<Grid<Cx, NDim>> gridder;
  InTensor mutable workspace;
  std::shared_ptr<FFT::FFT<NDim + 2, NDim>> fft;

  PadOp<Cx, NDim + 2, NDim>              pad;
  ApodizeOp<Cx, NDim>                    apo;
  Index const                            batches;
};

std::shared_ptr<TensorOperator<Cx, 5, 4>> make_nufft(Trajectory const                      &traj,
                                                     GridOpts                              &opts,
                                                     Index const                            nC,
                                                     Sz3 const                              matrix,
                                                     Basis<Cx> const                       &basis = IdBasis());

std::shared_ptr<TensorOperator<Cx, 5, 4>> make_nufft(Trajectory const                      &traj,
                                                     std::string const                     &ktype,
                                                     float const                            osamp,
                                                     Index const                            nC,
                                                     Sz3 const                              matrix,
                                                     Basis<Cx> const                       &basis = IdBasis(),
                                                     Index const                            bSz = 32,
                                                     Index const                            sSz = 16384);

} // namespace rl
