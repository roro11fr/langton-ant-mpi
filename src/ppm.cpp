#include "ppm.h"

#include <fstream>
#include <stdexcept>
#include <vector>

void write_ppm(const std::string& path,
               int size,
               const std::vector<std::uint8_t>& grid,
               const std::vector<Ant>& ants) {
    if (path.empty()) {
        return;
    }
    if (size <= 0 || grid.size() != static_cast<std::size_t>(size) * size) {
        throw std::invalid_argument("Invalid grid passed to write_ppm.");
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 3, 255);

    for (int row = 0; row < size; ++row) {
        for (int col = 0; col < size; ++col) {
            const std::size_t cell = static_cast<std::size_t>(row) * size + col;
            const std::size_t pixel = cell * 3;
            if (grid[cell] != 0) {
                pixels[pixel] = 0;
                pixels[pixel + 1] = 0;
                pixels[pixel + 2] = 0;
            }
        }
    }

    for (const Ant& ant : ants) {
        if (ant.row < 0 || ant.row >= size || ant.col < 0 || ant.col >= size) {
            continue;
        }
        const std::size_t pixel = (static_cast<std::size_t>(ant.row) * size + ant.col) * 3;
        pixels[pixel] = 220;
        pixels[pixel + 1] = 20;
        pixels[pixel + 2] = 20;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Could not open output file: " + path);
    }

    out << "P6\n" << size << " " << size << "\n255\n";
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
}
