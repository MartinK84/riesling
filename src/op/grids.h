#pragma once

#include "../kernel.h"
#include "../trajectory.h"
#include "grid-base.hpp"

#include <memory>

std::unique_ptr<GridBase> make_grid(Kernel const *k, Mapping const &m, bool const fg);
std::unique_ptr<GridBase>
make_grid_basis(Kernel const *k, Mapping const &m, R2 const &b, bool const fg);