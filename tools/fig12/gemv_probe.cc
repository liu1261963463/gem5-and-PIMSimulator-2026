#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "pim/PIMKernel.h"
#include "MultiChannelMemorySystem.h"

using namespace DRAMSim;

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

int main(int argc, char **argv)
{
    try {
        if (argc != 4) throw std::runtime_error("usage: gemv_probe M K output.json");
        const unsigned m = std::stoul(argv[1]), k = std::stoul(argv[2]);
        if (!m || !k || m > 8192 || k > 8192)
            throw std::runtime_error("dimensions must be in [1,8192]");
        auto mem = std::make_shared<MultiChannelMemorySystem>(
            "ini/HBM2_samsung_2M_16B_x64.ini", "system_hbm.ini", ".", "", 4096);
        unsigned channels = 0, ranks = 0, banks = 0, blocks = 0;
        float tck = 0;
        mem->getIniUint("NUM_CHANS", &channels);
        mem->getIniUint("NUM_RANKS", &ranks);
        mem->getIniUint("NUM_BANKS", &banks);
        mem->getIniUint("NUM_PIM_BLOCKS", &blocks);
        mem->getIniFloat("tCK", &tck);
        if (channels != 16 || ranks != 1 || banks != 16 || blocks != 8 ||
            tck != 1.0f || PIMConfiguration::getPIMPrecision() != FP16)
            throw std::runtime_error("probe requires the original 16-channel HBM2 FP16 configuration");

        PIMKernel kernel(mem, channels, ranks);
        // Existing preloadGemv traverses complete GRF tiles: pad, never read past vectors.
        const unsigned outputTile = 8 * channels * ranks * blocks;
        const unsigned mp = (m + outputTile - 1) / outputTile * outputTile;
        const unsigned kp = (k + 255) / 256 * 256;
        NumpyBurstType weight, input;
        weight.shape = {mp, kp}; weight.loadTobShape(16);
        input.shape = {1, kp}; input.loadTobShape(16);
        weight.bData.resize(size_t(mp) * kp / 16);
        input.bData.resize(kp / 16);
        std::vector<int> x(kp, 0), expected(mp, 0);
        for (unsigned c = 0; c < k; ++c) {
            x[c] = (mix(c + 123u) & 1) ? 1 : -1;
            input.bData[c / 16].fp16Data_[c % 16] = convertF2H(float(x[c]));
        }
        for (unsigned r = 0; r < m; ++r)
            for (unsigned c = 0; c < k; ++c) {
                const int w = (mix(r * 8192u + c + 456u) & 1) ? 1 : -1;
                weight.bData[size_t(r) * kp / 16 + c / 16].fp16Data_[c % 16] =
                    convertF2H(float(w));
                expected[r] += w * x[c];
            }

        const uint64_t c0 = kernel.getCycle();
        kernel.preloadGemv(&weight);
        kernel.runPIM();
        const uint64_t c1 = kernel.getCycle();
        kernel.executeGemv(&weight, &input, false);
        kernel.runPIM();
        const uint64_t c2 = kernel.getCycle();
        // NaN poison makes missing readbacks fail, even for padded zero rows.
        std::vector<BurstType> result(mp);
        for (auto &b : result)
            for (auto &v : b.fp16Data_) v = convertF2H(NAN);
        const unsigned endCol = kernel.getResultColGemv(kp / 16, mp);
        kernel.readResult(result.data(), pimBankType::ODD_BANK, mp, 0, 0, endCol);
        kernel.runPIM();
        const uint64_t c3 = kernel.getCycle();
        unsigned errors = 0;
        for (unsigned r = 0; r < mp; ++r) {
            float sum = 0;
            for (auto v : result[r].fp16Data_) sum += convertH2F(v);
            if (!std::isfinite(sum) || sum != expected[r]) {
                if (errors < 4)
                    std::cerr << "mismatch row=" << r << " expected=" << expected[r]
                              << " got=" << sum << '\n';
                ++errors;
            }
        }
        if (errors || mem->hasPendingTransactions() || c2 <= c1 || c3 <= c2)
            throw std::runtime_error("GEMV correctness/drain/timing check failed; no result JSON written");
        std::ofstream out(argv[3]);
        if (!out) throw std::runtime_error("cannot open output JSON");
        out << "{\n  \"backend\": \"HBM2_diagnostic_not_paper_DDR5\",\n"
            << "  \"M\": " << m << ", \"K\": " << k
            << ", \"M_padded\": " << mp << ", \"K_padded\": " << kp << ",\n"
            << "  \"tCK_ns\": " << tck << ", \"correct\": true,\n"
            << "  \"weight_preload_cycles\": " << c1-c0 << ",\n"
            << "  \"kernel_including_upload_cycles\": " << c2-c1 << ",\n"
            << "  \"result_read_cycles\": " << c3-c2 << ",\n"
            << "  \"host_reduction_timed\": false,\n"
            << "  \"paper_without_overheads_available\": false,\n"
            << "  \"paper_with_overheads_available\": false\n}\n";
        out.close();
        if (!out) throw std::runtime_error("failed writing JSON");
        std::cout << "PASS GEMV M=" << m << " K=" << k << " padded=" << mp << 'x' << kp
                  << " preload=" << c1-c0 << " kernel_with_upload=" << c2-c1
                  << " readback=" << c3-c2 << " cycles\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << '\n'; return 1;
    }
}
