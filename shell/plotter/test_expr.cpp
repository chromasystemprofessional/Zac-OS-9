/* Plotter's expressions (expr.cpp). */
#include <cmath>
#include <cstdio>

#include "expr.h"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static double at(const char *text, double x) {
	Expression e;
	return e.parse(text) ? e(x) : NAN;
}

int main() {
	CHECK(at("2x+1", 3) == 7);
	CHECK(at("x^2 - 4x + 4", 2) == 0);
	CHECK(at("-x^2", 3) == -9);
	CHECK(at("2^3^2", 0) == 512);
	CHECK(std::fabs(at("sin(pi/2)", 0) - 1) < 1e-12);
	CHECK(std::fabs(at("2sin x", M_PI / 2) - 2) < 1e-12);
	CHECK(std::fabs(at("3(x+1)", 1) - 6) < 1e-12);
	CHECK(std::fabs(at("sqrt(abs(x))", -9) - 3) < 1e-12);
	CHECK(std::fabs(at("ln(e)", 0) - 1) < 1e-12 && std::fabs(at("log(100)", 0) - 2) < 1e-12);
	CHECK(std::isnan(at("sqrt(x)", -1)));
	Expression bad;
	QString why;
	CHECK(!bad.parse("2x+", &why) && why.contains("missing"));
	CHECK(!bad.parse("sin(x", &why) && why.contains(")"));
	CHECK(!bad.parse("2#x", &why) && why.contains("#"));
	std::puts("ok: Plotter reads expressions");
	return 0;
}
