#pragma once
typedef float float_t;
/* Declarations only: native host tests use the real CRT math implementation. */
double floor(double value);
double round(double value);
double fabs(double value);
#define isfinite(value) __builtin_isfinite(value)
