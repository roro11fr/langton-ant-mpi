#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class Direction : int {
    North = 0,
    East = 1,
    South = 2,
    West = 3
};

struct Ant {
    int id = 0;
    int row = 0;
    int col = 0;
    Direction direction = Direction::North;
};

struct Config {
    std::string mode = "seq";
    int size = 1000;
    long long steps = 100000;
    int ants = 1;
    unsigned int seed = 1;
    std::string output;
    std::string frames_prefix;
    std::string conflict_policy = "modulo";
    int gather_every = 0;
    bool torus = true;
};

struct SimulationResult {
    int size = 0;
    std::vector<std::uint8_t> grid;
    std::vector<Ant> ants;
    double elapsed_seconds = 0.0;
    double compute_seconds = 0.0;
    double communication_seconds = 0.0;
    double io_seconds = 0.0;
};

struct LocalPartition {
    int start_row = 0;
    int row_count = 0;
};

Direction turn_right(Direction direction);
Direction turn_left(Direction direction);

std::vector<Ant> generate_initial_ants(int size, int ant_count, unsigned int seed);
LocalPartition partition_rows(int size, int rank, int world_size);
int owner_rank_for_row(int global_row, int size, int world_size);

SimulationResult run_sequential(const Config& config);
