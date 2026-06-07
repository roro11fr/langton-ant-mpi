#pragma once

#include "simulation.h"

#include <cstdint>
#include <string>
#include <vector>

void write_ppm(const std::string& path,
               int size,
               const std::vector<std::uint8_t>& grid,
               const std::vector<Ant>& ants);
