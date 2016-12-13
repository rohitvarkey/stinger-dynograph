#pragma once

#include <dynograph_util.h>

extern "C" {
#include <stinger_core/stinger.h>
}

struct StingerGraph
{
    stinger_t * S;

    StingerGraph(int64_t nv);
    ~StingerGraph();

    // Prevent copy
    StingerGraph(const StingerGraph& other)            = delete;
    StingerGraph& operator=(const StingerGraph& other) = delete;

    void insert(const DynoGraph::Batch& batch);
    void deleteOlderThan(int64_t threshold);
    void printSize();
};