#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_function_should_doBlahAndBlah(void) {
}

static void test_function_should_doAlsoDoBlah(void) {
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_function_should_doBlahAndBlah);
	RUN_TEST(test_function_should_doAlsoDoBlah);
	return UNITY_END();
}
