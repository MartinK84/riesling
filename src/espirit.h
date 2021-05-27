#pragma once

#include "gridder.h"
#include "log.h"

Cx4 ESPIRIT(
    Gridder const &gridder, Cx3 const &data, long const calSz, long const kernelSz, Log &log);
