// Oriented display UV <-> oriented source UV. Keep in sync with PreviewView.
float PresentationRemapWidth(float u, float ratio) {
    const float x = 2 * u - 1;
    const float a = min(abs(x), 1.0);
    return .5 + .5 * x * ratio * rsqrt(1 + (ratio * ratio - 1) * a * a);
}
