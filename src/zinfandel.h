#pragma once

#include "info.h"
#include "log.h"
#include "sense.h"

/*!
 * ZTE Infilling From Autocallibration NeighbourhooD ELements
 */
void zinfandel(
    long const gap_sz,
    long const n_src,
    long const n_spoke,
    long const n_read,
    float const lambda,
    R3 const &traj,
    Cx3 &ks,
    Log &log);

/*!
 *  Helper functions exposed for testing
 */
Cx2 GrabSources(
    Cx3 const &ks,
    float const scale,
    long const n_src,
    long const s_read,
    long const n_read,
    std::vector<long> const &spokes);

Cx2 GrabTargets(
    Cx3 const &ks,
    float const scale,
    long const s_read,
    long const n_read,
    std::vector<long> const &spokes);
