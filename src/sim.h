/*
    author: Suhas Vittal
    date:   2025 August 18
*/

#ifndef SIM_h
#define SIM_h

#include <cstdint>
#include <unordered_map>
#include <vector>

struct INSTRUCTION;

enum class GATE { H, X, Z, CX, S, SDG, T, TDG, RX, RY, RZ, CCX }

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class SIM
{
public:
    uint64_t s_stall_cycles_caused_by_magic_states{0};
    uint64_t s_stall_cycles_caused_by_memory{0};
    uint64_t s_logical_inst{0};
    uint64_t s_total_inst{0};
    uint64_t s_cycles{0};
private:
    constexpr static size_t MAGIC_STATE_X{0},
                            MAGIC_STATE_Y{0};
    constexpr static int64_t FREE_TILE{-1};
    /*
        Tracks pending instructions for each qubit. An instruction can
        only be executed if it is at the head of the window for all its
        arguments.
    */
    std::unordered_map<int64_t, std::vector<INSTRUCTION*>> inst_window_map_;

    /*
        Contains the location of the (logical) qubits on the device.
        Each logical qubit is assumed to take up one "tile" of space.

        Contains `atlas_width_ * atlas_height_` tiles.
    */
    std::vector<int64_t> atlas_;
    const size_t atlas_width_;
    const size_t atlas_height_;
public:
    void tick();

private:
    void do_gate(GATE gate, std::vector<int64_t> qubits);
    bool has_adjacent_free_tile(size_t x, size_t y) const;
    bool path_exists_between_points(size_t x1, size_t y1, size_t x2, size_t y2) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif // SIM_h