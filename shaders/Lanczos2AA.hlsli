#ifndef VEYRA_LANCZOS2_AA
#define VEYRA_LANCZOS2_AA

// sinc(x) * sinc(x/2), for |x| < 2. Factoring the zeros lets neighboring
// same-sign weights share a hardware sample and avoids per-pixel sin calls.
// Maximum absolute weight error on the support is below 1e-5.
float4 Lanczos2Weight(float4 x)
{
    const float4 u = x*x;
    const float4 edge = 1.0 - 0.25*u;
    const float4 p = ((((-0.00009722712*u + 0.002066204)*u - 0.02167649)*u
                       + 0.14157045)*u - 0.55597864)*u + 0.99999049;
    return select(u < 4.0, (1.0-u)*edge*edge*p, 0.0);
}

#endif
