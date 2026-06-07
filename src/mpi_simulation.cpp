#include "mpi_simulation.h"
#include "ppm.h"

#include <mpi.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {

struct PackedAnt {
    int id;
    int row;
    int col;
    int direction;
};

struct CellConflict {
    int count = 0;
    int min_ant_id = 0;
};

long long cell_key(int global_row, int col, int size) {
    return static_cast<long long>(global_row) * size + col;
}

void wrap_or_kill(Ant& ant, int size, bool torus, bool& alive) {
    if (torus) {
        if (ant.row < 0) {
            ant.row = size - 1;
        } else if (ant.row >= size) {
            ant.row = 0;
        }

        if (ant.col < 0) {
            ant.col = size - 1;
        } else if (ant.col >= size) {
            ant.col = 0;
        }
    } else if (ant.row < 0 || ant.row >= size || ant.col < 0 || ant.col >= size) {
        alive = false;
    }
}

void advance_ant(Ant& ant, int size, bool torus, bool& alive) {
    switch (ant.direction) {
        case Direction::North:
            --ant.row;
            break;
        case Direction::East:
            ++ant.col;
            break;
        case Direction::South:
            ++ant.row;
            break;
        case Direction::West:
            --ant.col;
            break;
    }
    wrap_or_kill(ant, size, torus, alive);
}

std::vector<PackedAnt> pack_ants(const std::vector<Ant>& ants) {
    std::vector<PackedAnt> packed;
    packed.reserve(ants.size());
    for (const Ant& ant : ants) {
        packed.push_back({ant.id, ant.row, ant.col, static_cast<int>(ant.direction)});
    }
    return packed;
}

std::vector<Ant> unpack_ants(const std::vector<PackedAnt>& packed) {
    std::vector<Ant> ants;
    ants.reserve(packed.size());
    for (const PackedAnt& ant : packed) {
        ants.push_back({ant.id, ant.row, ant.col, static_cast<Direction>(ant.direction)});
    }
    return ants;
}

void exchange_migrating_ants(int up_rank,
                             int down_rank,
                             const std::vector<PackedAnt>& outgoing_up,
                             const std::vector<PackedAnt>& outgoing_down,
                             std::vector<Ant>& local_ants) {
    constexpr int down_tag = 200;
    constexpr int up_tag = 201;

    MPI_Request send_requests[2] = {MPI_REQUEST_NULL, MPI_REQUEST_NULL};
    MPI_Isend(outgoing_down.data(),
              static_cast<int>(outgoing_down.size() * sizeof(PackedAnt)),
              MPI_BYTE,
              down_rank,
              down_tag,
              MPI_COMM_WORLD,
              &send_requests[0]);
    MPI_Isend(outgoing_up.data(),
              static_cast<int>(outgoing_up.size() * sizeof(PackedAnt)),
              MPI_BYTE,
              up_rank,
              up_tag,
              MPI_COMM_WORLD,
              &send_requests[1]);

    auto probe_and_receive = [&](int source, int tag) {
        if (source == MPI_PROC_NULL) {
            return;
        }

        MPI_Status status;
        MPI_Probe(source, tag, MPI_COMM_WORLD, &status);

        int byte_count = 0;
        MPI_Get_count(&status, MPI_BYTE, &byte_count);
        if (byte_count % static_cast<int>(sizeof(PackedAnt)) != 0) {
            throw std::runtime_error("Invalid ant migration payload size.");
        }

        std::vector<PackedAnt> incoming(static_cast<std::size_t>(byte_count / static_cast<int>(sizeof(PackedAnt))));
        MPI_Recv(incoming.data(), byte_count, MPI_BYTE, source, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        for (const PackedAnt& ant : incoming) {
            local_ants.push_back({ant.id, ant.row, ant.col, static_cast<Direction>(ant.direction)});
        }
    };

    probe_and_receive(up_rank, down_tag);
    probe_and_receive(down_rank, up_tag);
    MPI_Waitall(2, send_requests, MPI_STATUSES_IGNORE);
}

void exchange_ghost_rows(const std::vector<std::uint8_t>& local_grid,
                         int row_count,
                         int size,
                         int up_rank,
                         int down_rank,
                         std::vector<std::uint8_t>& top_ghost,
                         std::vector<std::uint8_t>& bottom_ghost) {
    top_ghost.assign(static_cast<std::size_t>(size), 0);
    bottom_ghost.assign(static_cast<std::size_t>(size), 0);

    const std::uint8_t* first_row = row_count > 0 ? local_grid.data() : top_ghost.data();
    const std::uint8_t* last_row = row_count > 0
        ? local_grid.data() + static_cast<std::size_t>(row_count - 1) * size
        : bottom_ghost.data();

    MPI_Sendrecv(last_row,
                 size,
                 MPI_UNSIGNED_CHAR,
                 down_rank,
                 300,
                 top_ghost.data(),
                 size,
                 MPI_UNSIGNED_CHAR,
                 up_rank,
                 300,
                 MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);

    MPI_Sendrecv(first_row,
                 size,
                 MPI_UNSIGNED_CHAR,
                 up_rank,
                 301,
                 bottom_ghost.data(),
                 size,
                 MPI_UNSIGNED_CHAR,
                 down_rank,
                 301,
                 MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
}

std::vector<std::uint8_t> scatter_initial_grid(const LocalPartition& part,
                                               int size,
                                               int rank,
                                               int world_size) {
    std::vector<int> counts(static_cast<std::size_t>(world_size));
    std::vector<int> displacements(static_cast<std::size_t>(world_size));

    for (int r = 0; r < world_size; ++r) {
        const LocalPartition p = partition_rows(size, r, world_size);
        counts[static_cast<std::size_t>(r)] = p.row_count * size;
        displacements[static_cast<std::size_t>(r)] = p.start_row * size;
    }

    std::vector<std::uint8_t> global_grid;
    if (rank == 0) {
        global_grid.assign(static_cast<std::size_t>(size) * size, 0);
    }

    std::vector<std::uint8_t> local_grid(static_cast<std::size_t>(part.row_count) * size);
    MPI_Scatterv(rank == 0 ? global_grid.data() : nullptr,
                 counts.data(),
                 displacements.data(),
                 MPI_UNSIGNED_CHAR,
                 local_grid.data(),
                 part.row_count * size,
                 MPI_UNSIGNED_CHAR,
                 0,
                 MPI_COMM_WORLD);

    return local_grid;
}

std::vector<std::uint8_t> gather_grid(const std::vector<std::uint8_t>& local_grid,
                                      const LocalPartition& part,
                                      int size,
                                      int rank,
                                      int world_size) {
    std::vector<int> counts(static_cast<std::size_t>(world_size));
    std::vector<int> displacements(static_cast<std::size_t>(world_size));

    for (int r = 0; r < world_size; ++r) {
        const LocalPartition p = partition_rows(size, r, world_size);
        counts[static_cast<std::size_t>(r)] = p.row_count * size;
        displacements[static_cast<std::size_t>(r)] = p.start_row * size;
    }

    std::vector<std::uint8_t> global_grid;
    if (rank == 0) {
        global_grid.resize(static_cast<std::size_t>(size) * size);
    }

    MPI_Gatherv(local_grid.data(),
                part.row_count * size,
                MPI_UNSIGNED_CHAR,
                rank == 0 ? global_grid.data() : nullptr,
                counts.data(),
                displacements.data(),
                MPI_UNSIGNED_CHAR,
                0,
                MPI_COMM_WORLD);

    return global_grid;
}

std::vector<Ant> gather_ants(const std::vector<Ant>& local_ants, int rank, int world_size) {
    std::vector<PackedAnt> packed = pack_ants(local_ants);
    const int local_count = static_cast<int>(packed.size());

    std::vector<int> counts(static_cast<std::size_t>(world_size));
    MPI_Gather(&local_count, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    std::vector<int> byte_counts(static_cast<std::size_t>(world_size));
    std::vector<int> byte_displacements(static_cast<std::size_t>(world_size));
    int total_count = 0;

    if (rank == 0) {
        for (int r = 0; r < world_size; ++r) {
            byte_counts[static_cast<std::size_t>(r)] = counts[static_cast<std::size_t>(r)] * static_cast<int>(sizeof(PackedAnt));
            byte_displacements[static_cast<std::size_t>(r)] = total_count * static_cast<int>(sizeof(PackedAnt));
            total_count += counts[static_cast<std::size_t>(r)];
        }
    }

    std::vector<PackedAnt> all_packed(static_cast<std::size_t>(rank == 0 ? total_count : 0));
    MPI_Gatherv(packed.data(),
                local_count * static_cast<int>(sizeof(PackedAnt)),
                MPI_BYTE,
                rank == 0 ? all_packed.data() : nullptr,
                byte_counts.data(),
                byte_displacements.data(),
                MPI_BYTE,
                0,
                MPI_COMM_WORLD);

    if (rank == 0) {
        return unpack_ants(all_packed);
    }
    return {};
}

std::string frame_path(const std::string& prefix, long long step) {
    std::ostringstream out;
    out << prefix << "_" << std::setw(6) << std::setfill('0') << step << ".ppm";
    return out.str();
}

void register_flip(std::unordered_map<long long, CellConflict>& flips, long long key, int ant_id) {
    auto [it, inserted] = flips.emplace(key, CellConflict{0, ant_id});
    ++it->second.count;
    if (inserted || ant_id < it->second.min_ant_id) {
        it->second.min_ant_id = ant_id;
    }
}

bool should_flip_cell(const CellConflict& conflict, const std::string& policy) {
    if (policy == "modulo") {
        return (conflict.count % 2) != 0;
    }
    if (policy == "cancel") {
        return conflict.count == 1;
    }
    if (policy == "priority") {
        return conflict.count > 0;
    }
    throw std::invalid_argument("--conflict-policy must be modulo, cancel, or priority.");
}

bool boundary_destination_is_black(const Ant& ant,
                                   const LocalPartition& part,
                                   const std::vector<std::uint8_t>& top_ghost,
                                   const std::vector<std::uint8_t>& bottom_ghost,
                                   int size) {
    const int local_row = ant.row - part.start_row;
    if (local_row == 0 && ant.direction == Direction::North) {
        return top_ghost[static_cast<std::size_t>(ant.col)] != 0;
    }
    if (local_row == part.row_count - 1 && ant.direction == Direction::South) {
        return bottom_ghost[static_cast<std::size_t>(ant.col)] != 0;
    }
    (void)size;
    return false;
}

} // namespace

SimulationResult run_mpi(const Config& config) {
    int rank = 0;
    int world_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (config.size <= 0) {
        throw std::invalid_argument("--size must be positive.");
    }
    if (config.steps < 0) {
        throw std::invalid_argument("--steps cannot be negative.");
    }
    if (world_size > config.size) {
        throw std::invalid_argument("The number of MPI processes cannot exceed --size.");
    }

    const LocalPartition part = partition_rows(config.size, rank, world_size);
    std::vector<std::uint8_t> local_grid = scatter_initial_grid(part, config.size, rank, world_size);
    std::vector<std::uint8_t> top_ghost(static_cast<std::size_t>(config.size), 0);
    std::vector<std::uint8_t> bottom_ghost(static_cast<std::size_t>(config.size), 0);

    std::vector<Ant> local_ants;
    if (rank == 0) {
        const std::vector<Ant> all_ants = generate_initial_ants(config.size, config.ants, config.seed);
        for (const Ant& ant : all_ants) {
            const int owner = owner_rank_for_row(ant.row, config.size, world_size);
            if (owner == 0) {
                local_ants.push_back(ant);
            }
        }
        for (int target = 1; target < world_size; ++target) {
            std::vector<Ant> target_ants;
            for (const Ant& ant : all_ants) {
                if (owner_rank_for_row(ant.row, config.size, world_size) == target) {
                    target_ants.push_back(ant);
                }
            }
            std::vector<PackedAnt> packed = pack_ants(target_ants);
            int count = static_cast<int>(packed.size());
            MPI_Send(&count, 1, MPI_INT, target, 10, MPI_COMM_WORLD);
            MPI_Send(packed.data(), count * static_cast<int>(sizeof(PackedAnt)), MPI_BYTE, target, 11, MPI_COMM_WORLD);
        }
    } else {
        int count = 0;
        MPI_Recv(&count, 1, MPI_INT, 0, 10, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        std::vector<PackedAnt> packed(static_cast<std::size_t>(count));
        MPI_Recv(packed.data(), count * static_cast<int>(sizeof(PackedAnt)), MPI_BYTE, 0, 11, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        local_ants = unpack_ants(packed);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    const double started = MPI_Wtime();
    double compute_seconds = 0.0;
    double communication_seconds = 0.0;
    double io_seconds = 0.0;

    for (long long step = 0; step < config.steps; ++step) {
        const int up_rank = (rank == 0) ? (config.torus ? world_size - 1 : MPI_PROC_NULL) : rank - 1;
        const int down_rank = (rank == world_size - 1) ? (config.torus ? 0 : MPI_PROC_NULL) : rank + 1;

        const double ghost_started = MPI_Wtime();
        exchange_ghost_rows(local_grid,
                            part.row_count,
                            config.size,
                            up_rank,
                            down_rank,
                            top_ghost,
                            bottom_ghost);
        communication_seconds += MPI_Wtime() - ghost_started;

        const double compute_started = MPI_Wtime();
        std::unordered_map<long long, CellConflict> flips;
        flips.reserve(local_ants.size() * 2 + 1);

        std::vector<Ant> next_local;
        next_local.reserve(local_ants.size());
        std::vector<Ant> outgoing_up;
        std::vector<Ant> outgoing_down;

        for (Ant ant : local_ants) {
            const int local_row = ant.row - part.start_row;
            if (local_row < 0 || local_row >= part.row_count) {
                continue;
            }

            const std::size_t index = static_cast<std::size_t>(local_row) * config.size + ant.col;
            const bool black = local_grid[index] != 0;
            ant.direction = black ? turn_left(ant.direction) : turn_right(ant.direction);
            register_flip(flips, cell_key(ant.row, ant.col, config.size), ant.id);

            const bool crosses_partition_boundary =
                (local_row == 0 && ant.direction == Direction::North) ||
                (local_row == part.row_count - 1 && ant.direction == Direction::South);
            if (crosses_partition_boundary) {
                const bool destination_black = boundary_destination_is_black(ant, part, top_ghost, bottom_ghost, config.size);
                (void)destination_black;
            }

            bool alive = true;
            advance_ant(ant, config.size, config.torus, alive);
            if (!alive) {
                continue;
            }

            const int owner = owner_rank_for_row(ant.row, config.size, world_size);
            if (owner == rank) {
                next_local.push_back(ant);
            } else if (owner >= 0) {
                if (config.torus && rank == 0 && owner == world_size - 1) {
                    outgoing_up.push_back(ant);
                } else if (config.torus && rank == world_size - 1 && owner == 0) {
                    outgoing_down.push_back(ant);
                } else if (owner < rank) {
                    outgoing_up.push_back(ant);
                } else {
                    outgoing_down.push_back(ant);
                }
            }
        }

        for (const auto& [key, conflict] : flips) {
            if (should_flip_cell(conflict, config.conflict_policy)) {
                const int global_row = static_cast<int>(key / config.size);
                const int col = static_cast<int>(key % config.size);
                const int local_row = global_row - part.start_row;
                if (local_row >= 0 && local_row < part.row_count) {
                    local_grid[static_cast<std::size_t>(local_row) * config.size + col] ^= 1;
                }
            }
        }

        local_ants = std::move(next_local);
        compute_seconds += MPI_Wtime() - compute_started;

        const double migration_started = MPI_Wtime();
        exchange_migrating_ants(up_rank,
                                down_rank,
                                pack_ants(outgoing_up),
                                pack_ants(outgoing_down),
                                local_ants);
        communication_seconds += MPI_Wtime() - migration_started;

        if (config.gather_every > 0 && (step + 1) % config.gather_every == 0) {
            const double io_started = MPI_Wtime();
            std::vector<std::uint8_t> frame_grid = gather_grid(local_grid, part, config.size, rank, world_size);
            std::vector<Ant> frame_ants = gather_ants(local_ants, rank, world_size);
            if (rank == 0 && !config.frames_prefix.empty()) {
                write_ppm(frame_path(config.frames_prefix, step + 1), config.size, frame_grid, frame_ants);
            }
            io_seconds += MPI_Wtime() - io_started;
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    SimulationResult result;
    result.size = config.size;
    const double final_io_started = MPI_Wtime();
    result.grid = gather_grid(local_grid, part, config.size, rank, world_size);
    result.ants = gather_ants(local_ants, rank, world_size);
    io_seconds += MPI_Wtime() - final_io_started;

    double max_compute = 0.0;
    double max_communication = 0.0;
    double max_io = 0.0;
    MPI_Reduce(&compute_seconds, &max_compute, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&communication_seconds, &max_communication, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&io_seconds, &max_io, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    const double finished = MPI_Wtime();
    result.elapsed_seconds = finished - started;
    result.compute_seconds = max_compute;
    result.communication_seconds = max_communication;
    result.io_seconds = max_io;
    return result;
}
