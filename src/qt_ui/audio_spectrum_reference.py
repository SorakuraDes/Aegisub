"""Independent numerical reference for the 512-point audio spectrogram (no build)."""
import cmath
import math

N = 512
RATE = 44100
BINS = N // 2 + 1


def spectrum(pcm, center):
    if not 0 <= center < len(pcm):
        return [0.0] * BINS
    samples = [pcm[j] / 32768 if 0 <= j < len(pcm) else 0.0
               for j in range(center - N // 2, center + N // 2)]
    windowed = [v * (0.5 - 0.5 * math.cos(math.tau * i / N)) for i, v in enumerate(samples)]
    # Direct DFT oracle, independent of the radix-2 ordering and butterflies.
    ref = [sum(a * cmath.exp(-1j * math.tau * i * k / N) for i, a in enumerate(windowed))
           for k in range(BINS)]
    rev = [0] * N
    frame = [0j] * N
    for i in range(N):
        rev[i] = (rev[i >> 1] >> 1) | ((i & 1) * (N // 2))
        frame[rev[i]] = windowed[i]
    span = 2
    while span <= N:
        for base in range(0, N, span):
            for j in range(span // 2):
                a, b = base + j, base + j + span // 2
                odd = frame[b] * cmath.exp(-1j * math.tau * j / span)
                frame[a], frame[b] = frame[a] + odd, frame[a] - odd
        span *= 2
    assert max(abs(a - b) for a, b in zip(frame, ref)) < 1e-8
    return [min(1., max(0., 1 + 20 * math.log10(max(1e-4, abs(v) * (2 if k in (0, N // 2) else 4) / N)) / 80))
            for k, v in enumerate(frame[:BINS])]


if __name__ == '__main__':
    pcm = [int(16384 * math.sin(math.tau * 440 * i / RATE)) for i in range(3 * N)]
    result = spectrum(pcm, N)
    strongest = max(range(BINS), key=result.__getitem__)
    assert strongest == 5 and .91 < result[strongest] < .94
    assert max(spectrum([0] * N, N // 2)) == 0
    assert max(spectrum(pcm, -1)) == max(spectrum(pcm, len(pcm))) == 0
    for center in (0, len(pcm) - 1):
        assert max(spectrum(pcm, center)) > 0
    print(f'PASS: 440 Hz -> bin {strongest} ({strongest * RATE / N:.3f} Hz), level {result[strongest]:.4f}; silence, trailing silence and boundary windows pass.')
