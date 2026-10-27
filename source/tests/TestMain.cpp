#include "TestFramework.h"

#include <cstdio>
#include <exception>

std::vector<TestCase>& testRegistry() {
    static std::vector<TestCase> registry;
    return registry;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int failures = 0;
    for (const TestCase& test : testRegistry()) {
        try {
            test.body();
            std::printf("[pass] %s\n", test.name.c_str());
        } catch (const TestFailure& failure) {
            ++failures;
            std::printf("[FAIL] %s\n       %s\n", test.name.c_str(), failure.message.c_str());
        } catch (const std::exception& error) {
            ++failures;
            std::printf("[FAIL] %s\n       exception: %s\n", test.name.c_str(), error.what());
        }
    }
    std::printf("%zu tests, %d failed\n", testRegistry().size(), failures);
    return failures == 0 ? 0 : 1;
}
