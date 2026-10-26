#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <vector>

struct TestCase {
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& testRegistry();

struct TestRegistrar {
    TestRegistrar(std::string name, std::function<void()> body) {
        testRegistry().push_back({std::move(name), std::move(body)});
    }
};

struct TestFailure {
    std::string message;
};

#define BALLS_CONCAT_INNER(a, b) a##b
#define BALLS_CONCAT(a, b) BALLS_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                                          \
    static void BALLS_CONCAT(testBody, __LINE__)();                                                              \
    static TestRegistrar BALLS_CONCAT(testRegistrar, __LINE__)(name, BALLS_CONCAT(testBody, __LINE__));          \
    static void BALLS_CONCAT(testBody, __LINE__)()

#define EXPECT(condition)                                                                                        \
    do {                                                                                                         \
        if (!(condition)) {                                                                                      \
            std::ostringstream failureStream;                                                                    \
            failureStream << __FILE__ << ":" << __LINE__ << ": EXPECT(" #condition ") failed";                   \
            throw TestFailure{failureStream.str()};                                                              \
        }                                                                                                        \
    } while (false)
