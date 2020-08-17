#include <Kokkos_Core.hpp>

#include "knn.hpp"
#include "stats.hpp"
#include "xmap.hpp"

namespace edm
{

void _xmap(CrossMap &result, const Dataset &ds, const LUT &lut,
           const Targets &targets, int E, int tau, int Tp)
{
    const auto distances = lut.distances;
    const auto indices = lut.indices;

    size_t scratch_size = ScratchTimeSeries::shmem_size(ds.extent(0));

    Kokkos::parallel_for(
        "EDM::xmap::lookup",
        Kokkos::TeamPolicy<>(targets.size(), Kokkos::AUTO)
            .set_scratch_size(0, Kokkos::PerTeam(scratch_size)),
        KOKKOS_LAMBDA(const Kokkos::TeamPolicy<>::member_type &member) {
            int tj = targets(member.league_rank());

            ScratchTimeSeries scratch(member.team_scratch(0), ds.extent(0));

            Kokkos::parallel_for(Kokkos::TeamThreadRange(member, ds.extent(0)),
                                 [=](int i) { scratch(i) = ds(i, tj); });

            member.team_barrier();

            CorrcoefState state;

            Kokkos::parallel_reduce(
                Kokkos::TeamThreadRange(member, distances.extent(0)),
                [=](int i, CorrcoefState &upd) {
                    float pred = 0.0f;

                    Kokkos::parallel_reduce(
                        Kokkos::ThreadVectorRange(member, E + 1),
                        [=](int &e, float &p) {
                            p += scratch(indices(i, e)) * distances(i, e);
                        },
                        pred);

                    float actual = scratch((E - 1) * tau + Tp + i);

                    upd += CorrcoefState(pred, actual);
                },
                Kokkos::Sum<CorrcoefState>(state));

            Kokkos::single(Kokkos::PerTeam(member),
                           [=]() { result(tj) = state.rho(); });
        });
}

// Group time series by their optimal embedding dimensions
void group_ts(std::vector<Targets> &groups, const std::vector<int> &edims,
              int E_max)
{
    std::vector<std::vector<int>> h_groups(E_max);

    groups.resize(E_max);

    for (int i = 0; i < edims.size(); i++) {
        h_groups[edims[i] - 1].push_back(i);
    }

    for (int E = 1; E <= E_max; E++) {
        Targets targets("targets", h_groups[E - 1].size());

        auto mirror = Kokkos::create_mirror_view(targets);

        for (int j = 0; j < h_groups[E - 1].size(); j++) {
            mirror(j) = h_groups[E - 1][j];
        }

        Kokkos::deep_copy(targets, mirror);
        groups[E - 1] = targets;
    }
}

void xmap(CrossMap &result, const Dataset &ds, const TimeSeries &library,
          const std::vector<Targets> &groups, std::vector<LUT> &luts,
          LUT &tmp_lut, int E_max, int tau, int Tp)
{
    Kokkos::Profiling::pushRegion("EDM::xmap");

    // Compute kNN tables for all E
    for (int E = 1; E <= E_max; E++) {
        knn(library, library, luts[E - 1], tmp_lut, E, tau, Tp, E + 1);
        normalize_lut(luts[E - 1]);
    }

    // Perform lookups
    for (int E = 1; E <= E_max; E++) {
        if (!groups[E - 1].size()) {
            continue;
        }

        _xmap(result, ds, luts[E - 1], groups[E - 1], E, tau, Tp);
    }

    Kokkos::Profiling::popRegion();
}

} // namespace edm
