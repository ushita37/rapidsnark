#include "gpu_test.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <string>
#include <cstdint>
#include <cstring>
#include <limits>
#include "fr.hpp"
#include "fq.hpp"
#include <thread>
#include <memory>
#include <vector>
#include <chrono>
#include <cassert>
#include "misc.hpp"
#include "vulkan_manager.h"

const size_t Count = 1024*1024;
const size_t IterNum = 10;

struct Element
{
    uint64_t& operator[](int i) { return val[i]; }
    const uint64_t& operator[](int i) const  { return val[i]; }

    operator FrRawElement& () { return val; }
    operator const FrRawElement& () const { return val; }

    FrRawElement val;
};

using Vector = std::vector<Element>;

std::string format(uint64_t val)
{
    std::ostringstream  oss;

    oss << "0x" << std::hex << std::setw(16) << std::setfill('0') << val;

    return oss.str();
}

std::ostream& operator<<(std::ostream& os, const FrRawElement val)
{
    os << format(val[0]) << ","
       << format(val[1]) << ","
       << format(val[2]) << ","
       << format(val[3]);

    return os;
}

void vecAdd(Element& c, const Element& a, const Element& b)
{
    c[0] = a[0] + b[0];
    c[1] = a[1] + b[1];
    c[2] = a[2] + b[2];
    c[3] = a[3] + b[3];
}
void vecMul(Element& c, const Element& a, const Element& b)
{
    c[0] = a[0] * b[0];
    c[1] = a[1] * b[1];
    c[2] = a[2] * b[2];
    c[3] = a[3] * b[3];
}

void calcSingle(Vector& c, const Vector& a, const Vector& b)
{
    const int size = c.size();

    assert(size == a.size());
    assert(size == b.size());

    for (size_t i = 0; i < size; i++) {
        for (size_t k = 0; k < IterNum; k++) {
                    vecAdd(c[i], a[i], b[i]);
//                    vecMul(c[i], a[i], b[i]);
//                    Fr_rawAdd(c[i], a[i], b[i]);
//                    Fr_rawMMul(c[i], a[i], b[i]);
        }
    }
}

void calcParallel(ThreadPool &threadPool, Vector& c, const Vector& a, const Vector& b)
{
    const int size = c.size();

    assert(size == a.size());
    assert(size == b.size());

    threadPool.parallelFor(0, size, [&] (int begin, int end, int numThread) {

        for (size_t i = begin; i < end; i++) {
            for (size_t k = 0; k < IterNum; k++) {
                        vecAdd(c[i], a[i], b[i]);
//                        vecMul(c[i], a[i], b[i]);
//                        Fr_rawAdd(c[i], a[i], b[i]);
//                        Fr_rawMMul(c[i], a[i], b[i]);
            }
        }
    });
}

void initVecInc(Vector& a, size_t initVal)
{
    for (size_t i = 0; i < a.size(); i++) {
        a[i][0] = i + initVal;
        a[i][1] = i + initVal;
        a[i][2] = i + initVal;
        a[i][3] = i + initVal;
    }
}

void initVecVal(Vector& a, size_t initVal = 0)
{
    for (size_t i = 0; i < a.size(); i++) {
        a[i][0] = initVal;
        a[i][1] = initVal;
        a[i][2] = initVal;
        a[i][3] = initVal;
    }
}

bool cmpVec(const Vector& a, const Vector& b)
{
    const std::size_t size = a.size();

    assert(a.size() == b.size());

    for (size_t i = 0; i < size; i++) {
        if (!Fr_rawIsEq(a[i], b[i])) {
            return false;
        }
    }

    return true;
}

void test_cpu(Vector& c2, const Vector& a, const Vector& b,
              char       *msg,
              long        msg_max_size)
{
    std::ostringstream  oss;
    Vector c1(Count);

    ThreadPool &threadPool = ThreadPool::defaultPool();

    oss << "threads: " << threadPool.getThreadCount() << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    calcSingle(c1, a, b);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    oss << "Single execution time: " << duration.count() << " ms" << std::endl;

    start = std::chrono::high_resolution_clock::now();

    calcParallel(threadPool, c2, a, b);

    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    oss << "Parallel execution time: " << duration.count() << " ms" << std::endl;
    oss << "CPU eq: " << std::boolalpha << cmpVec(c1, c2) << std::endl;

    strncat(msg, oss.str().c_str(), msg_max_size);
}

void test_gpu(Vector& cv, Vector& a, Vector& b,
              const char *shader_path,
              char       *msg,
              long        msg_max_size)
{
    std::ostringstream  oss;
    const size_t data_size = a.size() * sizeof(Vector::value_type);
    const uint32_t groupCount = Count/128;

    VulkanManager vkMgr(shader_path);

    auto start = std::chrono::high_resolution_clock::now();

    vkMgr.run(cv.data(), a.data(), b.data(), data_size, groupCount);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    vkMgr.debugInfo(oss);

    oss << "GPU execution time: " << duration.count() << " ms" << std::endl;

    strncat(msg, oss.str().c_str(), msg_max_size);
}

void test(
    const char *shader_path,
    char       *msg,
    long        msg_max_size)
{
    std::ostringstream oss;
    Vector a(Count);
    Vector b(Count);
    Vector rc(Count);
    Vector rv(Count);
    const size_t elementSize = sizeof(Vector::value_type);

    initVecVal(a, 1);
    initVecVal(b, 2);
    initVecVal(rc);
    initVecVal(rv);

    test_gpu(rv, a, b, shader_path, msg, msg_max_size);
    test_cpu(rc, a, b, msg, msg_max_size);

    oss << "Vulkan eq: " << std::boolalpha << cmpVec(rc, rv) << std::endl;

    oss << "ElementSize: " << elementSize << std::endl;
    oss << "VectorSize: " << Count << std::endl;

    oss << "a[0]: " << a[0] << std::endl;
    oss << "b[0]: " << b[0] << std::endl;
    oss << "r[0]: " << rv[0] << std::endl;
    oss << "r[M]: " << rv[Count-1] << std::endl;

    strncat(msg, oss.str().c_str(), msg_max_size);
}

void gpu_test(
    const char *shader_path,
    char       *msg,
    long        msg_max_size)
{
    std::string result;

    msg[0] = 0;

    try {
        test(shader_path, msg, msg_max_size);
        return;

    } catch (std::exception& e) {
        result = "Failed: ";
        result += e.what();
    }
    catch (...) {
        result += "Failed: unknown error";
    }

    strncat(msg, result.c_str(), msg_max_size);
}
