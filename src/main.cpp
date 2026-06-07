#include "mpi_simulation.h"
#include "ppm.h"
#include "simulation.h"

#include <mpi.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

bool parse_bool(const std::string& value) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    throw std::invalid_argument("Invalid boolean value: " + value);
}

void print_usage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n"
        << "  --mode seq|mpi          Simulation mode (default: seq)\n"
        << "  --size N                Grid size NxN (default: 1000)\n"
        << "  --steps T               Number of simulation steps (default: 100000)\n"
        << "  --ants K                Number of ants (default: 1)\n"
        << "  --seed S                Random seed (default: 1)\n"
        << "  --output file.ppm       Optional PPM output path\n"
        << "  --frames-prefix prefix  Write periodic PPM frames as prefix_000001.ppm\n"
        << "  --conflict-policy name  modulo|cancel|priority (default: modulo)\n"
        << "  --gather-every G        Periodic MPI gather interval, 0 disables it (default: 0)\n"
        << "  --torus true|false      Wrap grid edges (default: true)\n"
        << "  --help                  Show this help\n";
}

bool has_help_flag(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help") {
            return true;
        }
    }
    return false;
}

Config parse_args(int argc, char** argv) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument("Missing value for " + name);
            }
            return argv[++i];
        };

        if (arg == "--help") {
            continue;
        } else if (arg == "--mode") {
            config.mode = require_value(arg);
        } else if (arg == "--size") {
            config.size = std::stoi(require_value(arg));
        } else if (arg == "--steps") {
            config.steps = std::stoll(require_value(arg));
        } else if (arg == "--ants") {
            config.ants = std::stoi(require_value(arg));
        } else if (arg == "--seed") {
            config.seed = static_cast<unsigned int>(std::stoul(require_value(arg)));
        } else if (arg == "--output") {
            config.output = require_value(arg);
        } else if (arg == "--frames-prefix") {
            config.frames_prefix = require_value(arg);
        } else if (arg == "--conflict-policy") {
            config.conflict_policy = require_value(arg);
        } else if (arg == "--gather-every") {
            config.gather_every = std::stoi(require_value(arg));
        } else if (arg == "--torus") {
            config.torus = parse_bool(require_value(arg));
        } else {
            throw std::invalid_argument("Unknown option: " + arg);
        }
    }

    if (config.mode != "seq" && config.mode != "mpi") {
        throw std::invalid_argument("--mode must be seq or mpi.");
    }
    if (config.size <= 0) {
        throw std::invalid_argument("--size must be positive.");
    }
    if (config.steps < 0) {
        throw std::invalid_argument("--steps cannot be negative.");
    }
    if (config.ants < 0) {
        throw std::invalid_argument("--ants cannot be negative.");
    }
    if (config.gather_every < 0) {
        throw std::invalid_argument("--gather-every cannot be negative.");
    }
    if (config.conflict_policy != "modulo" &&
        config.conflict_policy != "cancel" &&
        config.conflict_policy != "priority") {
        throw std::invalid_argument("--conflict-policy must be modulo, cancel, or priority.");
    }

    return config;
}

void broadcast_config(Config& config, int rank) {
    int mode_code = config.mode == "mpi" ? 1 : 0;
    int int_values[4] = {
        config.size,
        config.ants,
        config.gather_every,
        config.torus ? 1 : 0
    };
    unsigned int seed = config.seed;
    long long steps = config.steps;

    MPI_Bcast(&mode_code, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(int_values, 4, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&seed, 1, MPI_UNSIGNED, 0, MPI_COMM_WORLD);
    MPI_Bcast(&steps, 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);

    int output_length = rank == 0 ? static_cast<int>(config.output.size()) : 0;
    MPI_Bcast(&output_length, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank != 0) {
        config.output.resize(static_cast<std::size_t>(output_length));
    }
    MPI_Bcast(config.output.data(), output_length, MPI_CHAR, 0, MPI_COMM_WORLD);

    int frames_prefix_length = rank == 0 ? static_cast<int>(config.frames_prefix.size()) : 0;
    MPI_Bcast(&frames_prefix_length, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank != 0) {
        config.frames_prefix.resize(static_cast<std::size_t>(frames_prefix_length));
    }
    MPI_Bcast(config.frames_prefix.data(), frames_prefix_length, MPI_CHAR, 0, MPI_COMM_WORLD);

    int conflict_policy_length = rank == 0 ? static_cast<int>(config.conflict_policy.size()) : 0;
    MPI_Bcast(&conflict_policy_length, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank != 0) {
        config.conflict_policy.resize(static_cast<std::size_t>(conflict_policy_length));
    }
    MPI_Bcast(config.conflict_policy.data(), conflict_policy_length, MPI_CHAR, 0, MPI_COMM_WORLD);

    config.mode = mode_code == 1 ? "mpi" : "seq";
    config.size = int_values[0];
    config.ants = int_values[1];
    config.gather_every = int_values[2];
    config.torus = int_values[3] != 0;
    config.seed = seed;
    config.steps = steps;
}

} // namespace

int main(int argc, char** argv) {
    if (has_help_flag(argc, argv)) {
        print_usage(argv[0]);
        return 0;
    }

    MPI_Init(&argc, &argv);

    int rank = 0;
    int world_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    int exit_code = 0;

    try {
        Config config = parse_args(argc, argv);
        broadcast_config(config, rank);

        if (config.mode == "seq" && world_size != 1) {
            if (rank == 0) {
                std::cerr << "Sequential mode must be run with one MPI process.\n";
            }
            MPI_Finalize();
            return 1;
        }

        SimulationResult result;
        if (config.mode == "seq") {
            result = run_sequential(config);
        } else {
            result = run_mpi(config);
        }

        if (rank == 0) {
            if (!config.output.empty()) {
                write_ppm(config.output, result.size, result.grid, result.ants);
            }

            const double steps_per_second = result.elapsed_seconds > 0.0
                ? static_cast<double>(config.steps) / result.elapsed_seconds
                : 0.0;
            std::cout
                << "mode=" << config.mode
                << " size=" << config.size
                << " steps=" << config.steps
                << " ants_initial=" << config.ants
                << " ants_final=" << result.ants.size()
                << " processes=" << world_size
                << " elapsed_seconds=" << result.elapsed_seconds
                << " compute_seconds=" << result.compute_seconds
                << " communication_seconds=" << result.communication_seconds
                << " io_seconds=" << result.io_seconds
                << " steps_per_second=" << steps_per_second
                << "\n";
        }
    } catch (const std::exception& ex) {
        if (rank == 0) {
            std::cerr << "Error: " << ex.what() << "\n";
            print_usage(argv[0]);
        }
        exit_code = 1;
    }

    MPI_Finalize();
    return exit_code;
}
