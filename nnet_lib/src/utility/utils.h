#ifndef _UTILITY_UTILS_H_
#define _UTILITY_UTILS_H_

#include <array>
#include <string>
#include <vector>

#include "core/datatypes.h"
#include "gem5/m5ops.h"

namespace smaug {

// TODO: Allow these to take rvalue references.
template <typename T>
int product(std::vector<T> array) {
    int prod = 1;
    for (auto val : array)
        prod *= val;
    return prod;
}

template <typename T>
std::vector<T> sum(std::vector<T> array0, std::vector<T> array1) {
    assert(array0.size() == array1.size());
    std::vector<T> sum(array0.size());
    for (int i = 0; i < array0.size(); i++)
      sum[i] = array0[i] + array1[i];
    return sum;
}

template <typename T>
void variadicToVector(std::vector<T>& vector, T elem) {
    vector.push_back(elem);
}

template <typename T, typename... Args>
void variadicToVector(std::vector<T>& vector, T e, Args... elems) {
    vector.push_back(e);
    variadicToVector(vector, elems...);
}

template <typename T, typename... Args>
std::array<T, sizeof...(Args) + 1> variadicToArray(T i, Args... elems) {
    return {{ i, elems... }};
}

void* malloc_aligned(size_t size, bool zeroOut = false);

// Return the difference between @value and the next multiple of @alignment.
int calc_padding(int value, unsigned alignment);

std::string dataLayoutToStr(DataLayout layout);

#ifndef TRACE_MODE
#define M5_SWITCH_CPU()                                                        \
    if (runningInSimulation) {                                                 \
        m5_switch_cpu();                                                       \
    }
#define M5_DUMP_STATS()                                                        \
    if (runningInSimulation) {                                                 \
        m5_dump_stats(0, 0);                                                   \
    }
#define M5_DUMP_RESET_STATS()                                                  \
    if (runningInSimulation) {                                                 \
        m5_dump_reset_stats(0, 0);                                             \
    }
#else
#define M5_SWITCH_CPU()
#define M5_DUMP_STATS()
#define M5_DUMP_RESET_STATS()
#endif

}  // namespace smaug

#endif
