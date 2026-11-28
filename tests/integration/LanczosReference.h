#pragma once

#include <cmath>
#include <numbers>

inline double lanczos2(double x)
{
    if (std::abs(x) >= 2) return 0;
    if (x == 0) return 1;
    const double a = std::numbers::pi*x;
    return 2*std::sin(a)*std::sin(a/2)/(a*a);
}
