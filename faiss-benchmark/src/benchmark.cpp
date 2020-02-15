#include <mutex>
#include <atomic>
#include <thread>
#include <iostream>
#include <algorithm>

#include <AutoTune.h>
#include <index_io.h>

#include "util/vecs.h"
#include "util/vector.h"
#include "util/perfmon.h"
#include "util/statistics.h"

template <typename T>
T* newZeroOutArray(size_t n) {
    T* array = new T[n];
    memset(array, 0, n * sizeof(T));
    return array;
}

void Benchmark(const faiss::Index* index, size_t count,
        const float* queries, const faiss::Index::idx_t* groundtruths,
        size_t top_n, size_t batch_size, size_t thread_count,
        float& qps, float& cpu_util, float& mem_r_bw, float& mem_w_bw,
        util::statistics::Percentile<uint32_t>& percentile_latency,
        util::statistics::Percentile<float>& percentile_rate) {
    if (batch_size == 0) {
        throw std::runtime_error("<batch_size = 0> is invalid!");
    }
    if (thread_count == 0) {
        throw std::runtime_error("<thread_count = 0> is invalid!");
    }
    size_t dim = index->d;
    uint32_t* latencies = newZeroOutArray<uint32_t>(count);
    std::unique_ptr<uint32_t> latencies_deleter(latencies);
    faiss::Index::idx_t* labels =
            newZeroOutArray<faiss::Index::idx_t>(count * top_n);
    std::unique_ptr<faiss::Index::idx_t> labels_deleter(labels);
    std::atomic<size_t> cursor(0);
    std::vector<std::thread> threads;
    util::perfmon::CPUUtilization cpu_mon(true, true);
    util::perfmon::MemoryBandwidth mem_mon;
    cpu_mon.start();
    mem_mon.start();
    uint64_t all_start_us = util::perfmon::Clock::microsecond();
    for (size_t t = 0; t < thread_count; t++) {
        threads.emplace_back([&] {
            float* distances = newZeroOutArray<float>(batch_size * top_n);
            std::unique_ptr<float> distances_deleter(distances);
            while (true) {
                size_t offset = cursor.fetch_add(batch_size);
                if (offset + batch_size > count) {
                    break;
                }
                const float* xs = queries + offset * dim;
                faiss::Index::idx_t* ls = labels + offset * top_n;
                uint64_t start_us = util::perfmon::Clock::microsecond();
                index->search(batch_size, xs, top_n, distances, ls);
                uint64_t end_us = util::perfmon::Clock::microsecond();
                uint64_t latency = end_us - start_us;
                for (size_t i = 0; i < batch_size; i++) {
                    latencies[offset + i] = (uint32_t)latency;
                }
            }
        });
    }
    for (size_t t = 0; t < thread_count; t++) {
        threads[t].join();
    }
    uint64_t all_end_us = util::perfmon::Clock::microsecond();
    cpu_util = cpu_mon.end();
    mem_mon.end(mem_r_bw, mem_w_bw);
    qps = 1000000.0f * count / (all_end_us - all_start_us);
    threads.clear();
    percentile_latency.add(latencies, count);
    latencies_deleter.reset();
    cursor = 0;
    thread_count = std::thread::hardware_concurrency();
    std::mutex mutex;
    for (size_t t = 0; t < thread_count; t++) {
        threads.emplace_back([&] {
            while (true) {
                size_t i = cursor++;
                if (i >= count) {
                    break;
                }
                size_t offset = i * top_n;
                faiss::Index::idx_t* ls = labels + offset;
                const faiss::Index::idx_t* gts = groundtruths + offset;
                std::sort(ls, ls + top_n);
                size_t igt = 0, il = 0, correct = 0;
                while (igt < top_n && il < top_n) {
                    ssize_t diff = (ssize_t)gts[igt] - (ssize_t)ls[il];
                    if (diff < 0) {
                        igt++;
                    }
                    else if (diff > 0) {
                        il++;
                    }
                    else {
                        igt++;
                        il++;
                        correct++;
                    }
                }
                float rate = (float)correct / top_n;
                mutex.lock();
                percentile_rate.add(rate);
                mutex.unlock();
            }
        });
    }
    for (size_t t = 0; t < thread_count; t++) {
        threads[t].join();
    }
}

template <typename T>
std::shared_ptr<float> PrepareQueries(util::vecs::File* file, size_t dim,
        size_t& count) {
    util::vecs::Formater<T> reader(file);
    count = 0;
    while (reader.skip()) {
        count++;
    }
    reader.reset();
    float* cursor = new float[count * dim];
    std::shared_ptr<float> queries(cursor);
    util::vector::Converter<T, float> converter;
    for (size_t i = 0; i < count; i++) {
        std::vector<T> vector = reader.read();
        if (vector.size() != dim) {
            char buf[256];
            sprintf(buf, "query vector is not %luD!", dim);
            throw std::runtime_error(buf);
        }
        converter(cursor, vector);
        cursor += dim;
    }
    return queries;
}

std::shared_ptr<float> PrepareQueries(const char* fpath, size_t dim,
        size_t& count) {
    util::vecs::SuffixWrapper query(fpath, true);
    typedef std::shared_ptr<float> (*func_t)(util::vecs::File*,
            size_t, size_t&);
    static const struct Entry {
        char type;
        func_t func;
    }
    entries[] = {
        {'b', PrepareQueries<uint8_t>},
        {'i', PrepareQueries<int32_t>},
        {'f', PrepareQueries<float>},
    };
    for (size_t i = 0; i < sizeof(entries) / sizeof(Entry); i++) {
        const Entry* entry = entries + i;
        if (query.getDataType() == entry->type) {
            return entry->func(query.getFile(), dim, count);
        }
    }
    throw std::runtime_error("unsupported format of query vectors!");
}

template <typename T>
std::shared_ptr<faiss::Index::idx_t> PrepareGroundTruths(size_t count,
        size_t top_n, util::vecs::File* gt_file) {
    faiss::Index::idx_t* cursor = new faiss::Index::idx_t[count * top_n];
    std::shared_ptr<faiss::Index::idx_t> gts(cursor);
    util::vecs::Formater<T> reader(gt_file);
    util::vector::Converter<T, faiss::Index::idx_t> converter;
    for (size_t i = 0; i < count; i++) {
        std::vector<T> gt = reader.read();
        if (gt.size() < top_n) {
            char buf[256];
            sprintf(buf, "groundtruth vector is less than %luD!", top_n);
            throw std::runtime_error(buf);
        }
        gt.resize(top_n);
        std::sort(gt.begin(), gt.end());
        converter(cursor, gt);
        cursor += top_n;
    }
    return gts;
}

std::shared_ptr<faiss::Index::idx_t> PrepareGroundTruths(size_t count,
        size_t top_n, const char* fpath) {
    util::vecs::SuffixWrapper gt(fpath, true);    
    typedef std::shared_ptr<faiss::Index::idx_t> (*func_t)(size_t, size_t,
            util::vecs::File*);
    static const struct Entry {
        char type;
        func_t func;
    }
    entries[] = {
        {'i', PrepareGroundTruths<int32_t>},
    };
    for (size_t i = 0; i < sizeof(entries) / sizeof(Entry); i++) {
        const Entry* entry = entries + i;
        if (gt.getDataType() == entry->type) {
            return entry->func(count, top_n, gt.getFile());
        }
    }
    throw std::runtime_error("unsupported format of groundtruth vectors!");
}

struct Percentage {
    std::string str;
    double value;
};

template <typename T>
void OutputValue(const char* name, T value) {
    std::cout << name << ": " << value << std::endl;
}

template <typename T>
void OutputStatistics(const char* name,
        const std::vector<Percentage>& percentages,
        util::statistics::Percentile<T>& percentile) {
    std::cout << name << ": best=" << percentile.best() << " worst=" <<
            percentile.worst() << " average=" << percentile.average();
    for (auto it = percentages.begin(); it != percentages.end(); it++) {
        std::cout << " P(" << it->str << "%)=" << percentile(it->value);
    }
    std::cout << std::endl;
}

void Benchmark(const char* index_fpath, const char* query_fpath,
        const char* gt_fpath, size_t top_n, const char* _percentages,
        const char* _cases) {
    FILE* file = fopen(index_fpath, "r");
    if (!file) {
        throw std::runtime_error(std::string("file '").append(index_fpath)
                .append("' doesn't exist!"));
    }
    std::shared_ptr<faiss::Index> index(faiss::read_index(file));
    fclose(file);
    size_t dim = index->d;
    size_t count;
    std::shared_ptr<float> queries = PrepareQueries(query_fpath, dim, count);
    std::shared_ptr<faiss::Index::idx_t> gts = PrepareGroundTruths(count,
            top_n, gt_fpath);
    std::vector<Percentage> percentages;
    char* dup_str = strdup(_percentages);
    char* saved_ptr;
    for (const char *item = strtok_r(dup_str, " ,", &saved_ptr);
            item; item = strtok_r(nullptr, " ,", &saved_ptr)) {
        Percentage p;
        if (sscanf(item, "%lf", &(p.value)) != 1) {
            std::string msg("unrecognizable percentage: '");
            msg.append(item).append("'!");
            free(dup_str);
            throw std::runtime_error(msg);
        }
        p.str.assign(item);
        percentages.emplace_back(p);
    }
    free(dup_str);
    struct Case {
        std::string parameters;
        size_t batch_size;
        size_t thread_count;
    };
    std::vector<Case> cases;
    dup_str = strdup(_cases);
    for (const char *item = strtok_r(dup_str, " ;", &saved_ptr);
            item; item = strtok_r(nullptr, " ;", &saved_ptr)) {
        Case c;
        const char* concurrency = strstr(item, "/");
        if (!concurrency || sscanf(concurrency, "/%lux%lu",
                &(c.batch_size), &(c.thread_count)) != 2) {
            std::string msg("unrecognizable case: '");
            msg.append(item).append("'!");
            free(dup_str);
            throw std::runtime_error(msg);
        }
        c.parameters.assign(item, concurrency - item);
        cases.emplace_back(c);
    }
    free(dup_str);
    faiss::ParameterSpace ps;
    for (auto iter = cases.begin(); iter != cases.end(); iter++) {
        float qps, cpu_util, mem_r_bw, mem_w_bw;
        util::statistics::Percentile<uint32_t> latencies(true);
        util::statistics::Percentile<float> rates(false);
        ps.set_index_parameters(index.get(), iter->parameters.data());
        Benchmark(index.get(), count, queries.get(), gts.get(), top_n,
                iter->batch_size, iter->thread_count,
                qps, cpu_util, mem_r_bw, mem_w_bw, latencies, rates);
        OutputValue("qps", qps);
        OutputValue("cpu-util", cpu_util);
        OutputValue("mem-r-bw", mem_r_bw);
        OutputValue("mem-w-bw", mem_w_bw);
        OutputStatistics("latency", percentages, latencies);
        OutputStatistics("recall", percentages, rates);
    }
}

int main(int argc, char** argv) {
    size_t top_n;
    if (argc != 7 || sscanf(argv[4], "%lu", &top_n) != 1) {
        fprintf(stderr, "%s <index> <query> <gt> <top_n> <percentages> "
                "<cases>\n"
                "Load index from <index> if it exists. Then run several "
                "cases of benchmarks. The vectors to query are from <query>,"
                " the groundtruth vectors are from <gt>. Find <top_n> nearest"
                "neighbors for each query vector. The result is consist of "
                "statistics of latency and recall rate. Besides the best, "
                "worst and average, percentiles at <percentages> will be "
                "displayed additionally. For example, if <percentages> = '50,"
                " 99, 99.9', then 50-percentile, 99-percentile and "
                "99.9-percentile of latency and recall rates will be "
                "displayed. <cases> is a semicolon-split string of serval "
                "benchmark cases, each is in format of "
                "[parameters]/<batch_size>x<thread_count> (e.g. '"
                "nprobe=32/1x4')\n",
                argv[0]);
        return 1;
    }
    const char* index_fpath = argv[1];
    const char* query_fpath = argv[2];
    const char* gt_fpath = argv[3];
    const char* percentages = argv[5];
    const char* cases = argv[6];
    try {
        Benchmark(index_fpath, query_fpath, gt_fpath, top_n, percentages,
                cases);
    }
    catch (const std::exception& e) {
        fprintf(stderr, "ERROR: %s\n", e.what());
        return 1;
    }
    return 0;
}