#include <cstdio>

#include "CppUTest/TestHarness.h"

TEST_GROUP(HelloWorld){};

TEST(HelloWorld, EmitsHelloWorld) {
  std::printf("Hello, World!\r\n");
}
