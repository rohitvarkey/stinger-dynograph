#include <stdint.h>

#include <hooks.h>

extern "C" {
#include <dynograph_util.h>

#include <stinger.h>
#include <bfs.h>
#include <betweenness.h>
#include <clustering.h>
#include <static_components.h>
#include <kcore.h>
#include <pagerank.h>
}

struct args
{
    const char* alg_name;
    const char* input_path;
    int64_t window_size;
    int64_t num_batches;
    int64_t num_trials;
};

struct args
get_args(int argc, char **argv)
{
    struct args args;
    if (argc != 6)
    {
        dynograph_error("Usage: alg_name input_path num_batches window_size num_trials \n");
    }

    args.alg_name = argv[1];
    args.input_path = argv[2];
    args.num_batches = atoll(argv[3]);
    args.window_size = atoll(argv[4]);
    args.num_trials = atoll(argv[5]);
    if (args.num_batches < 1 || args.window_size < 1 || args.num_trials < 1)
    {
        dynograph_error("num_batches, window_size, and num_trials must be positive");
    }

    return args;
}

// Counts the number of edges that satsify the filter
int64_t
filtered_edge_count (struct stinger * S, int64_t nv, int64_t modified_after)
{
    int64_t num_edges = 0;

    OMP ("omp parallel for reduction(+:num_edges)")
    for (int64_t v = 0; v < nv; v++) {
        STINGER_FORALL_OUT_EDGES_OF_VTX_MODIFIED_AFTER_BEGIN (S, v, modified_after) {
            num_edges += 1;
        } STINGER_FORALL_OUT_EDGES_OF_VTX_MODIFIED_AFTER_END();
    }

    return num_edges;
}

void print_graph_stats(stinger_t *S, int64_t nv, int64_t modified_after)
{
    struct stinger_fragmentation_t * stats = (stinger_fragmentation_t*)xmalloc(sizeof(struct stinger_fragmentation_t));
    stinger_fragmentation (S, nv, stats);
    printf("{\n");
    printf("\"num_vertices\"            :%ld,\n", nv);
    printf("\"num_filtered_edges\"      :%ld,\n", filtered_edge_count(S, nv, modified_after));
    printf("\"num_edges\"               :%ld,\n", stats->num_edges);
    printf("\"num_empty_edges\"         :%ld,\n", stats->num_empty_edges);
    printf("\"num_fragmented_blocks\"   :%ld,\n", stats->num_fragmented_blocks);
    printf("\"edge_blocks_in_use\"      :%ld,\n", stats->edge_blocks_in_use);
    printf("\"num_empty_blocks\"        :%ld\n" , stats->num_empty_blocks);
    printf("}\n");
    free(stats);
}

void
insert_batch(stinger_t * S, const struct dynograph_edge_batch batch, int64_t trial)
{
    const int64_t type = 0;
    const int64_t num_edges = batch.num_edges;
    const int64_t directed = batch.directed;
    hooks_region_begin(trial);
    OMP("omp parallel for")
    for (int64_t i = 0; i < num_edges; ++i)
    {
        const struct dynograph_edge* e = &batch.edges[i];
        if (directed)
        {
            stinger_insert_edge     (S, type, e->src, e->dst, e->weight, e->timestamp);
        } else { // undirected
            stinger_insert_edge_pair(S, type, e->src, e->dst, e->weight, e->timestamp);
        }
    }
    hooks_region_end(trial);
}

struct benchmark
{
    const char *name;
    int64_t data_per_vertex;
    // Number of optional args?
} benchmarks[] = {
    {"all", 4}, // Must be = max(data_per_vertex) of all other algorithms
    {"bfs", 4},
    {"bfs-do", 4},
    {"betweenness", 2},
    {"clustering", 1},
    {"components", 1},
    {"kcore", 2},
    {"pagerank", 2}
};

struct benchmark *
get_benchmark(const char *name)
{
    struct benchmark *b = NULL;
    for (int i = 0; i < sizeof(benchmarks) / sizeof(benchmarks[0]); ++i)
    {
        if (!strcmp(benchmarks[i].name, name))
        {
            b = &benchmarks[i];
            break;
        }
    }
    if (b == NULL)
    {
        dynograph_error("Benchmark '%s' does not exist!", name);
    }
    return b;
}

void run_benchmark(const char *alg_name, stinger_t * S, int64_t num_vertices, void *alg_data, int64_t modified_after, int64_t trial)
{
    dynograph_message("Running %s...", alg_name);
    int64_t max_nv = stinger_max_nv(S);

    if (!strcmp(alg_name, "all"))
    {
        for (int i = 1; i < sizeof(benchmarks) / sizeof(benchmarks[0]); ++i)
        {
            run_benchmark(benchmarks[i].name, S, num_vertices, alg_data, modified_after, trial);
        }
    }
    else if (!strcmp(alg_name, "bfs"))
    {
        int64_t * marks = (int64_t*)alg_data + 0 * max_nv;
        int64_t * queue = (int64_t*)alg_data + 1 * max_nv;
        int64_t * Qhead = (int64_t*)alg_data + 2 * max_nv;
        int64_t * level = (int64_t*)alg_data + 3 * max_nv;
        int64_t levels;
        int64_t source_vertex = 3; // FIXME Get this from the command line
        hooks_region_begin(trial);
        levels = parallel_breadth_first_search (S, num_vertices, source_vertex, marks, queue, Qhead, level, modified_after);
        hooks_region_end(trial);
        if (levels < 5)
        {
            dynograph_message("WARNING: Breadth-first search was only %ld levels. Consider choosing a different source vertex.", levels);
        }
    }
    else if (!strcmp(alg_name, "bfs-do"))
    {
        int64_t * marks = (int64_t*)alg_data + 0 * max_nv;
        int64_t * queue = (int64_t*)alg_data + 1 * max_nv;
        int64_t * Qhead = (int64_t*)alg_data + 2 * max_nv;
        int64_t * level = (int64_t*)alg_data + 3 * max_nv;
        int64_t levels;
        int64_t source_vertex = 3; // FIXME Get this from the command line
        hooks_region_begin(trial);
        levels = direction_optimizing_parallel_breadth_first_search (S, num_vertices, source_vertex, marks, queue, Qhead, level, modified_after);
        hooks_region_end(trial);
        if (levels < 5)
        {
            dynograph_message("WARNING: Breadth-first search was only %ld levels. Consider choosing a different source vertex.", levels);
        }
    }
    else if (!strcmp(alg_name, "betweenness"))
    {
        double *bc =            (double*) alg_data + 0 * max_nv;
        int64_t *found_count =  (int64_t*)alg_data + 1 * max_nv;
        int64_t num_samples = 256; // FIXME Allow override from command line
        hooks_region_begin(trial);
        sample_search(S, num_vertices, num_samples, bc, found_count, modified_after);
        hooks_region_end(trial);
    }
    else if (!strcmp(alg_name, "clustering"))
    {
        int64_t *num_triangles = (int64_t*) alg_data + 0 * max_nv;
        hooks_region_begin(trial);
        count_all_triangles(S, num_triangles, modified_after);
        hooks_region_end(trial);
    }
    else if (!strcmp(alg_name, "components"))
    {
        int64_t *component_map = (int64_t*) alg_data + 0 * max_nv;
        hooks_region_begin(trial);
        parallel_shiloach_vishkin_components(S, num_vertices, component_map, modified_after);
        hooks_region_end(trial);
    }
    else if (!strcmp(alg_name, "kcore"))
    {
        int64_t *labels = (int64_t*) alg_data + 0 * max_nv;
        int64_t *counts = (int64_t*) alg_data + 1 * max_nv;
        int64_t k = 0;
        hooks_region_begin(trial);
        kcore_find(S, labels, counts, num_vertices, &k, modified_after);
        hooks_region_end(trial);
    }
    else if (!strcmp(alg_name, "pagerank"))
    {
        double * pagerank_scores =      (double*)alg_data + 0 * max_nv;
        double * pagerank_scores_tmp =  (double*)alg_data + 1 * max_nv;
        hooks_region_begin(trial);
        page_rank_directed(S, num_vertices, pagerank_scores, pagerank_scores_tmp, 1e-8, 0.85, 100, modified_after);
        hooks_region_end(trial);
    }
    else
    {
        dynograph_error("Algorithm %s not implemented!", alg_name);
    }
}

int main(int argc, char **argv)
{
    // Process command line arguments
    struct args args = get_args(argc, argv);
    // Load graph data in from the file in batches
    struct dynograph_dataset* dataset = dynograph_load_dataset(args.input_path, args.num_batches);
    // Look up the algorithm that will be benchmarked
    struct benchmark *b = get_benchmark(args.alg_name);

    for (int64_t trial = 0; trial < args.num_trials; trial++)
    {
        // Create the stinger data structure
        stinger_t * S = stinger_new();
        // Allocate data structures for the algorithm(s)
        uint64_t num_vertices = stinger_max_nv(S);
        void *alg_data = xcalloc(sizeof(int64_t) * b->data_per_vertex, num_vertices);

        // Run the algorithm(s) after each inserted batch
        for (int64_t i = 0; i < dataset->num_batches; ++i)
        {
            struct dynograph_edge_batch batch = dynograph_get_batch(dataset, i);
            dynograph_message("Inserting batch %i (%ld edges)", i, batch.num_edges);
            insert_batch(S, batch, trial);
            int64_t modified_after = dynograph_get_timestamp_for_window(dataset, i, args.window_size);
            num_vertices = stinger_max_active_vertex(S) + 1; // TODO faster way to get this?
            run_benchmark(b->name, S, num_vertices, alg_data, modified_after, trial);
            print_graph_stats(S, num_vertices, modified_after);
        }
        // Clean up
        free(alg_data);
        stinger_free(S);
    }

    dynograph_free_dataset(dataset);

    return 0;
}