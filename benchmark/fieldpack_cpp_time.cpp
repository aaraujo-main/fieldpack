#include "fieldpack/fieldpack.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

volatile std::uint64_t benchmarkSink = 0;

std::uint64_t parseIterations(int argc, char* argv[]) {
    if (argc > 2) {
        throw std::invalid_argument("usage: fieldpack_cpp_time [iterations]");
    }
    if (argc == 1) {
        return 100000;
    }

    const std::string_view argument(argv[1]);
    std::uint64_t iterations = 0;
    const char* end = argument.data() + argument.size();
    const auto result = std::from_chars(argument.data(), end, iterations);
    if (result.ec != std::errc{} || result.ptr != end || iterations == 0) {
        throw std::invalid_argument("iterations must be positive integer");
    }
    return iterations;
}

template <typename Operation>
double benchmark(std::string_view label, std::uint64_t iterations, Operation&& operation) {
    const auto warmup = std::min<std::uint64_t>(iterations, 1000);
    for (std::uint64_t index = 0; index < warmup; ++index) {
        operation();
    }

    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < iterations; ++index) {
        operation();
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double microseconds =
        std::chrono::duration<double, std::micro>(elapsed).count() / iterations;
    std::cout << label << " " << microseconds << " us/op\n";
    return microseconds;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const std::uint64_t iterations = parseIterations(argc, argv);
        using fieldpack::FieldOps;
        using fieldpack::FieldPack;
        using fieldpack::FieldPackSlice;
        using fieldpack::Registry;

        const auto scalarSchema = Registry::instance().add(
            {fieldpack::makeTrivialField("int", sizeof(std::int32_t), alignof(std::int32_t)),
             fieldpack::makeTrivialField("double", sizeof(double), alignof(double)),
             fieldpack::makeField<std::string>("string")},
            true);
        FieldPack scalarPack(scalarSchema);
        auto* scalarValue = static_cast<std::int32_t*>(scalarPack.fieldAddress(0));
        *scalarValue = 17;
        *static_cast<double*>(scalarPack.fieldAddress(1)) = 2.5;
        *static_cast<std::string*>(scalarPack.fieldAddress(2)) = "benchmark";
        require(*scalarValue == 17, "scalar fixture initialization failed");

        const auto childSchema = Registry::instance().add(
            {fieldpack::makeTrivialField("int", sizeof(std::int32_t), alignof(std::int32_t)),
             fieldpack::makeTrivialField("double", sizeof(double), alignof(double))},
            true);
        const auto nestedSchema = Registry::instance().add(
            {fieldpack::makeSliceField(childSchema), fieldpack::makePackField(childSchema)}, true);
        FieldPack nestedPack(nestedSchema);
        FieldPackSlice nestedSlice(nestedPack.fieldAddress(0));
        auto* packedChild = static_cast<FieldPack*>(nestedPack.fieldAddress(1));
        auto* sliceValue = static_cast<std::int32_t*>(nestedSlice.fieldAddress(0));
        auto* packValue = static_cast<std::int32_t*>(packedChild->fieldAddress(0));
        *sliceValue = 31;
        *static_cast<double*>(nestedSlice.fieldAddress(1)) = 3.5;
        *packValue = 47;
        *static_cast<double*>(packedChild->fieldAddress(1)) = 4.5;
        require(*sliceValue == 31 && *packValue == 47, "nested fixture initialization failed");

        std::cout << "FieldPack C++ field-access benchmark\n";
        std::cout << "iterations: " << iterations << "\n";
        std::cout << "\n";

        benchmark("scalar read", iterations, [&] {
            const auto* value = static_cast<const volatile std::int32_t*>(scalarPack.fieldAddress(0));
            benchmarkSink = static_cast<std::uint64_t>(*value);
        });
        benchmark("scalar write", iterations, [&] {
            auto* value = static_cast<volatile std::int32_t*>(scalarPack.fieldAddress(0));
            *value = 19;
        });
        benchmark("nested slice read", iterations, [&] {
            FieldPackSlice slice(nestedPack.fieldAddress(0));
            const auto* value = static_cast<const volatile std::int32_t*>(slice.fieldAddress(0));
            benchmarkSink = static_cast<std::uint64_t>(*value);
        });
        benchmark("nested slice write", iterations, [&] {
            FieldPackSlice slice(nestedPack.fieldAddress(0));
            auto* value = static_cast<volatile std::int32_t*>(slice.fieldAddress(0));
            *value = 37;
        });
        benchmark("nested pack read", iterations, [&] {
            const auto* child = static_cast<const FieldPack*>(nestedPack.fieldAddress(1));
            const auto* value = static_cast<const volatile std::int32_t*>(child->fieldAddress(0));
            benchmarkSink = static_cast<std::uint64_t>(*value);
        });
        benchmark("nested pack write", iterations, [&] {
            auto* child = static_cast<FieldPack*>(nestedPack.fieldAddress(1));
            auto* value = static_cast<volatile std::int32_t*>(child->fieldAddress(0));
            *value = 53;
        });

        require(*scalarValue == 19, "scalar write benchmark failed");
        require(*sliceValue == 37, "nested slice write benchmark failed");
        require(*packValue == 53, "nested pack write benchmark failed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
