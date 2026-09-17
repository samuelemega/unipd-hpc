/*
 * reference.h - independent direct DFT and error norm for the test suites.
 * Development-only: this file is not part of the course deliverable.
 */
#ifndef REFERENCE_H
#define REFERENCE_H

#include <complex.h>
#include <math.h>
#include <stddef.h>

/*
 * Direct O(N^2) forward 2D DFT computed from the definition, independently
 * of the FFT under test. in and out are distinct ny-by-nx row-major
 * matrices.
 */
static void reference_dft2d(
    const double complex *in,
    double complex *out,
    size_t ny,
    size_t nx) {
    const double tau = 2.0 * 3.141592653589793238462643383279502884;

    for (size_t ky = 0; ky < ny; ++ky) {
        for (size_t kx = 0; kx < nx; ++kx) {
            double complex sum = 0.0;

            for (size_t y = 0; y < ny; ++y) {
                for (size_t x = 0; x < nx; ++x) {
                    double angle = -tau * ((double)(ky * y) / (double)ny +
                                           (double)(kx * x) / (double)nx);

                    sum += in[y * nx + x] * cexp(I * angle);
                }
            }

            out[ky * nx + kx] = sum;
        }
    }
}

/* Relative error ||actual - reference||_2 / max(||reference||_2, 1). */
static double relative_error(
    const double complex *actual,
    const double complex *reference,
    size_t count) {
    double difference_norm = 0.0;
    double reference_norm = 0.0;

    for (size_t k = 0; k < count; ++k) {
        double complex d = actual[k] - reference[k];

        difference_norm += creal(d * conj(d));
        reference_norm += creal(reference[k] * conj(reference[k]));
    }

    difference_norm = sqrt(difference_norm);
    reference_norm = sqrt(reference_norm);

    return difference_norm / (reference_norm > 1.0 ? reference_norm : 1.0);
}

#endif
