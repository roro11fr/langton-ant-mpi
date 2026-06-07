#include "simulation.h"
#include "ppm.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

struct CellConflict {
    int count = 0;
    int min_ant_id = 0;
};

long long cell_key(int row, int col, int size) {
    return static_cast<long long>(row) * size + col;
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

} // namespace

Direction turn_right(Direction direction) {
    return static_cast<Direction>((static_cast<int>(direction) + 1) % 4);
}

Direction turn_left(Direction direction) {
    return static_cast<Direction>((static_cast<int>(direction) + 3) % 4);
}

std::vector<Ant> generate_initial_ants(int size, int ant_count, unsigned int seed) {
    if (size <= 0) {
        throw std::invalid_argument("Grid size must be positive.");
    }
    if (ant_count < 0) {
        throw std::invalid_argument("Ant count cannot be negative.");
    }

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> coord_dist(0, size - 1);
    std::uniform_int_distribution<int> dir_dist(0, 3);

    std::vector<Ant> ants;
    ants.reserve(static_cast<std::size_t>(ant_count));

    for (int i = 0; i < ant_count; ++i) {
        ants.push_back({
            i,
            coord_dist(rng),
            coord_dist(rng),
            static_cast<Direction>(dir_dist(rng))
        });
    }

    return ants;
}

LocalPartition partition_rows(int size, int rank, int world_size) {
    const int base = size / world_size;
    const int remainder = size % world_size;
    const int row_count = base + (rank < remainder ? 1 : 0);
    const int start_row = rank * base + std::min(rank, remainder);
    return {start_row, row_count};
}

int owner_rank_for_row(int global_row, int size, int world_size) {
    if (global_row < 0 || global_row >= size) {
        return -1;
    }

    const int base = size / world_size;
    const int remainder = size % world_size;
    const int large_rows = base + 1;

    if (remainder > 0 && global_row < remainder * large_rows) {
        return global_row / large_rows;
    }

    if (base == 0) {
        return -1;
    }

    return remainder + (global_row - remainder * large_rows) / base;
}

SimulationResult run_sequential(const Config& config) {
    if (config.size <= 0) {
        throw std::invalid_argument("--size must be positive.");
    }
    if (config.steps < 0) {
        throw std::invalid_argument("--steps cannot be negative.");
    }

    SimulationResult result;
    result.size = config.size;
    result.grid.assign(static_cast<std::size_t>(config.size) * config.size, 0);
    result.ants = generate_initial_ants(config.size, config.ants, config.seed);

    const auto started = std::chrono::steady_clock::now();

    for (long long step = 0; step < config.steps; ++step) {
        std::unordered_map<long long, CellConflict> flips;
        flips.reserve(result.ants.size() * 2 + 1);

        std::vector<Ant> next_ants;
        next_ants.reserve(result.ants.size());

        for (Ant ant : result.ants) {
            const std::size_t index = static_cast<std::size_t>(ant.row) * config.size + ant.col;
            const bool black = result.grid[index] != 0;
            ant.direction = black ? turn_left(ant.direction) : turn_right(ant.direction);
            register_flip(flips, cell_key(ant.row, ant.col, config.size), ant.id);

            bool alive = true;
            advance_ant(ant, config.size, config.torus, alive);
            if (alive) {
                next_ants.push_back(ant);
            }
        }

        for (const auto& [key, conflict] : flips) {
            if (should_flip_cell(conflict, config.conflict_policy)) {
                result.grid[static_cast<std::size_t>(key)] ^= 1;
            }
        }

        result.ants = std::move(next_ants);

        if (!config.frames_prefix.empty() &&
            config.gather_every > 0 &&
            (step + 1) % config.gather_every == 0) {
            const auto io_started = std::chrono::steady_clock::now();
            write_ppm(frame_path(config.frames_prefix, step + 1), result.size, result.grid, result.ants);
            const auto io_finished = std::chrono::steady_clock::now();
            result.io_seconds += std::chrono::duration<double>(io_finished - io_started).count();
        }
    }

    const auto finished = std::chrono::steady_clock::now();
    result.elapsed_seconds = std::chrono::duration<double>(finished - started).count();
    result.compute_seconds = result.elapsed_seconds - result.io_seconds;
    return result;
}
