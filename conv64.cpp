/*
 * Let R denote the ring of integers modulo 2^64.
 *
 * Our goal here is to develop a fast and straightforward way of multiplying
 * polynomials in R[x].
 *
 */

#include<iostream>
#include<vector>

using namespace std;

/*
 * Our first step is to note that the standard Radix-2 FFT has the problem that
 * its inverse transform requires division by 2, which is not invertible in R.
 *
 * We solve this by employing a Radix-3 FFT, which handles arrays whose size is
 * a power of 3, and whose inverse transform requires division by 3.
 *
 * Now for FFT to work, the ring has to have a sufficiently powerful 3^m'th
 * root of unity, and since the unit ring of R is Z_2 x Z_{2^62}, it only has
 * roots of unity of order 2^m.
 *
 * The first step to solving this is by expanding the ring to have a 3rd root
 * of unity. This extension can be realized as the ring R[omega]/(omega^2 +
 * omega + 1), that is polynomials of the form a + b*omega, with the property
 * that omega^2 = - omega - 1. It follows that omega^3 = 1.
 *
 * We call this new ring T and define the following type for its elements.
 */

struct T {
  uint64_t a, b;

  T() : a(0), b(0) { }
  T(uint64_t x) : a(x), b(0) { }
  T(uint64_t a, uint64_t b) : a(a), b(b) { }

  //The conjugate of a + b*omega is given by mapping omega -> omega^2
  T conj() {
    return T{a - b, -b};
  }

  T operator-() {
    return T{-a, -b};
  }
};

/*
 * A couple of useful constants: `OMEGA` is a third root of unity, `OMEGA2` is
 * its square and `INV3` is the multiplicative inverse of 3.
 */

const T OMEGA = {0, 1};
const T OMEGA2 = {-1ull, -1ull};
const T INV3 = {12297829382473034411ull, 0};

/*
 * Standard operators.
 */

T operator+(const T &u, const T &v) {
  return {u.a + v.a, u.b + v.b};
}

T operator-(const T &u, const T &v) {
  return {u.a - v.a, u.b - v.b};
}

T operator*(const T &u, const T &v) {
  return {u.a*v.a - u.b*v.b, u.b*v.a + u.a*v.b - u.b*v.b};
}

void operator+=(T &u, const T &v) {
  u.a += v.a;
  u.b += v.b;
}

void operator-=(T &u, const T &v) {
  u.a -= v.a;
  u.b -= v.b;
}

void operator*=(T &u, const T &v) {
  uint64_t tmp=u.a;
  u.a=u.a*v.a - u.b*v.b;
  u.b=u.b*v.a + tmp*v.b - u.b*v.b;
}

// We pack the main algorithm in a class of its own, mainly because we will have
// to allocate some temporary working memory.
class Conv64 {
  public:

  // Returns the product of two polynomials from the ring R[x].
  vector<int64_t> multiply(const vector<int64_t> &p,
                           const vector<int64_t> &q) {
    vector<uint64_t> pp(p.size()), qq(q.size());
    for (uint64_t i = 0; i < p.size(); ++i) {
      pp[i] = p[i];
    }
    for (uint64_t i = 0; i < q.size(); ++i) {
      qq[i] = q[i];
    }
    uint64_t s = 1;
    while (s < p.size() + q.size() - 1) {
      s *= 3;
    }
    pp.resize(s);
    qq.resize(s);
    vector<int64_t> res(s);
    multiply_cyclic_raw(pp.data(), qq.data(), pp.size(), (uint64_t*)res.data());
    res.resize(p.size() + q.size() - 1);
    return res;
  }

  private:

  // Temporary space.
  T *tmp;

  // Returns the product of a polynomial and the monomial x^t in the ring
  // T[x]/(x^m - omega). The result is placed in `to`.
  // NOTE: t must be in the range [0,3m]
  void twiddle(T *p, uint64_t m, uint64_t t, T *to) {
    if (t == 0 || t == 3*m) {
      for (uint64_t j = 0; j < m; ++j) {
        to[j] = p[j];
      }
      return;
    }
    uint64_t tt;
    T mult = 1;
    if (t < m) {
      tt = t;
    } else if (t < 2*m) {
      tt = t - m;
      mult = OMEGA;
    } else {
      tt = t - 2*m;
      mult = OMEGA2;
    }
    for (uint64_t j=0;j<tt;j++) {
      to[j] = p[m - tt + j]*OMEGA*mult;
    }
    for (uint64_t j=tt;j<m;j++) {
      to[j] = p[j-tt]*mult;
    }
  }

  // A "Decimation In Frequency" In-Place Radix-3 FFT Routine.
  // Input: A polynomial from (T[x]/(x^m - omega))[y]/(y^r - 1).
  // Output: Its Fourier transform (w.r.t. y) in 3-reversed order.
  void fftdif(T *p, uint64_t m, uint64_t r) {
    if (r == 1) return;
    uint64_t rr = r/3;
    uint64_t pos1 = m*rr, pos2 = 2*m*rr;
    for (uint64_t i = 0; i < rr; ++i) {
      for (uint64_t j = 0; j < m; ++j) {
        tmp[j] = p[i*m + j] + p[pos1 + i*m + j] + p[pos2 + i*m + j];
        tmp[m + j] = p[i*m + j] + OMEGA*p[pos1 + i*m + j] + OMEGA2*p[pos2 + i*m + j];
        tmp[2*m + j] = p[i*m + j] + OMEGA2*p[pos1 + i*m + j] + OMEGA*p[pos2 + i*m + j];
        p[i*m + j] = tmp[j];
      }
      twiddle(tmp + m, m, 3*i*m/r, p + pos1 + i*m);
      twiddle(tmp + 2*m, m, 6*i*m/r, p + pos2 + i*m);
    }
    fftdif(p, m, rr);
    fftdif(p + pos1, m, rr);
    fftdif(p + pos2, m, rr);
  }

  // A "Decimation In Time" In-Place Radix-3 Inverse FFT Routine.
  // Input: A polynomial in (T[x]/(x^m - omega))[y]/(y^r - 1) with coefficients
  //        in 3-reversed order.
  // Output: Its inverse Fourier transform in normal order.
  void fftdit(T *p, uint64_t m, uint64_t r) {
    if (r == 1) return;
    uint64_t rr = r/3;
    uint64_t pos1 = m*rr, pos2 = 2*m*rr;
    fftdit(p, m, rr);
    fftdit(p + pos1, m, rr);
    fftdit(p + pos2, m, rr);
    for (uint64_t i = 0; i < rr; ++i) {
      twiddle(p + pos1 + i*m, m, 3*m - 3*i*m/r, tmp + m);
      twiddle(p + pos2 + i*m, m, 3*m - 6*i*m/r, tmp + 2*m);
      for(uint64_t j = 0; j < m; ++j) {
        tmp[j] = p[i*m + j];
        p[i*m + j] = tmp[j] + tmp[m + j] + tmp[2*m + j];
        p[i*m + pos1 + j] = tmp[j] + OMEGA2*tmp[m + j] + OMEGA*tmp[2*m + j];
        p[i*m + pos2 + j] = tmp[j] + OMEGA*tmp[m + j] + OMEGA2*tmp[2*m + j];
      }
    }
  }

  // Computes the product of two polynomials in T[x]/(x^n - omega), where n is
  // a power of 3. The result is placed in `to`.
  void mul(T *p, T *q, uint64_t n, T *to) {
    if (n <= 27) {
      // O(n^2) grade-school multiplication
      for (uint64_t i = 0; i < n; ++i) {
        to[i]=0;
      }
      for (uint64_t i = 0; i < n; ++i) {
        for (uint64_t j = 0; j < n - i; ++j) {
          to[i + j] += p[i]*q[j];
        }
        for (uint64_t j = n - i; j < n; ++j) {
          to[i + j - n] += p[i]*q[j]*OMEGA;
        }
      }
      return;
    }

    uint64_t m = 1;
    while (m*m < n) {
      m *= 3;
    }
    uint64_t r = n/m;

    T inv = 1;
    for (uint64_t i = 1; i < r; i *= 3) {
      inv *= INV3;
    }

    /**********************************************************
     * THE PRODUCT IN (T[x]/(x^m - omega))[y] / (y^r - omega) *
     **********************************************************/

    // Move to the ring (T[x]/(x^m - omega))[y]/(y^r - 1) via the map y -> x^(m/r) y
    for (uint64_t i = 0; i < r; ++i) {
      twiddle(p + m*i, m, m/r*i, to + m*i);
      twiddle(q + m*i, m, m/r*i, to + n + m*i);
    }

    // Multiply using FFT
    fftdif(to, m, r);
    fftdif(to + n, m, r);
    for (uint64_t i = 0; i < r; ++i) {
      mul(to + m*i, to + n + m*i, m, to +2*n + m*i);
    }
    fftdit(to + 2*n, m, r);
    for (uint64_t i = 0; i < n; ++i) {
      to[2*n + i] *= inv;
    }

    // Return to the ring (T[x]/(x^m - omega))[y]/(y^r - omega)
    for (uint64_t i = 0; i < r; ++i) {
      twiddle(to + 2*n + m*i, m, 3*m - m/r*i, to + n + m*i);
    }

    /************************************************************
     * THE PRODUCT IN (T[x]/(x^m - omega^2))[y] / (y^r - omega) *
     ************************************************************/

    // Use conjugation to move to the ring (T[x]/(x^m - omega))[y]/(y^r - omega^2).
    // Then move to (T[x]/(x^m - omega))[y]/(y^r - 1) via the map y -> x^(2m/r) y
    for (uint64_t i = 0; i < r; ++i) {
      for (uint64_t j = 0; j < m; ++j) {
        p[m*i + j] = p[m*i + j].conj();
        q[m*i + j] = q[m*i + j].conj();
      }
      twiddle(p + m*i, m, 2*m/r*i, to + m*i);
      twiddle(q + m*i, m, 2*m/r*i, p + m*i);
    }

    fftdif(to, m, r);
    fftdif(p, m, r);
    for (uint64_t i = 0; i < r; ++i) {
      mul(to + m*i, p + m*i, m, to + 2*n + m*i);
    }
    fftdit(to + 2*n, m, r);
    for (uint64_t i = 0; i < n; ++i) {
      to[2*n + i] *= inv;
    }

    for (uint64_t i = 0; i < r; ++i) {
      twiddle(to + 2*n + m*i, m, 3*m - 2*m/r*i, q + m*i);
    }

    /**************************************************************************
     * The product in (T[x]/(x^(2m) + x^m + 1))[y]/(y^r - omega) via CRT, and *
     * unravelling the substitution y = x^m at the same time.                 *
     **************************************************************************/

    for (uint64_t i = 0; i < n; ++i) {
      to[i] = 0;
    }
    for (uint64_t i = 0; i < r; ++i) {
      for (uint64_t j = 0; j < m; ++j) {
        to[i*m + j] += (1 - OMEGA)*to[n + i*m + j] + (1 - OMEGA2)*q[i*m + j].conj();
        if (i*m + m + j < n) {
          to[i*m + m + j] += (OMEGA2 - OMEGA)*(to[n + i*m + j] - q[i*m + j].conj());
        } else {
          to[i*m + m + j - n] += (1 - OMEGA2)*(to[n + i*m + j] - q[i*m + j].conj());
        }
      }
    }
    for (uint64_t i = 0; i < n; ++i) {
      to[i] *= INV3;
    }
  }

  // Computes the product of two polynomials from the ring R[x]/(x^n - 1), where
  // n must be a power of three. The result is placed in target which must have
  // space for n elements.
  void multiply_cyclic_raw(uint64_t *p, uint64_t *q, uint64_t n,
                                       uint64_t *target) {
    // If n = 3^k, let m = 3^(floor(k/2)) and r = 3^(ceil(k/2))
    uint64_t m = 1;
    while (m*m <= n) {
      m *= 3;
    }
    m /= 3;
    uint64_t r = n/m;

    // Compute 3^(-r)
    T inv = 1;
    for (uint64_t i = 1; i < r; i *= 3) {
      inv *= INV3;
    }

    // Allocate some working memory, the layout is as follows:
    // pp: length n
    // qq: length n
    // to: length n + 3*m
    // tmp: length 3*m
    T *buf = new T[3*n + 6*m];
    T *pp = buf;
    T *qq = buf + n;
    T *to = buf + 2*n;
    tmp = buf + 3*n + 3*m;

    for (uint64_t i = 0; i < n; ++i) {
      pp[i] = p[i];
      qq[i] = q[i];
    }

    // By setting y = x^m, we may write our polynomials in the form
    //   (p_0 + p_1 x + ... + p_{m-1} x^{m-1})
    // + (p_m + ... + p_{2m-1} x^{m-1}) y
    // + ...
    // + (p_{(r-1)m} + ... + p_{rm - 1} x^{m-1}) y^r
    //
    // In this way we can view p and q as elements of the ring S[y]/(y^r - 1),
    // where S = R[x]/(x^m - omega), and since r <= 3m, we know that x^{3m/r} is
    // an rth root of unity. We can therefore use FFT to calculate the product
    // in S[y]/(y^r - 1).
    fftdif(pp, m, r);
    fftdif(qq, m, r);
    for (uint64_t i = 0; i < r; ++i) {
      mul(pp + i*m, qq + i*m, m, to + i*m);
    }
    fftdit(to, m, r);
    for (uint64_t i = 0; i<n; ++i) {
      pp[i] = to[i]*inv;
    }
    
    // Now, the product in (T[x]/(x^m - omega^2))[y](y^r - 1) is simply the
    // conjugate of the product in (T[x]/(x^m - omega))[y]/(y^r - 1), because
    // there is no omega-component in the data.
    //
    // By the Chinese Remainder Theorem we can obtain the product in the
    // ring (T[x]/(x^(2m) + x^m + x))[y]/(y^r - 1), and then set y=x^m to get
    // the result.
    for (uint64_t i = 0; i < n; ++i) to[i] = 0;
    for (uint64_t i = 0; i < r; ++i) {
      for (uint64_t j = 0; j < m; ++j) {
        to[i*m + j] += (1 - OMEGA)*pp[i*m + j] + (1 - OMEGA2)*pp[i*m + j].conj();
        if (i*m + m + j < n) {
          to[i*m + m + j] += (OMEGA2 - OMEGA)*(pp[i*m + j] - pp[i*m + j].conj());
        } else {
          to[i*m + m + j - n] += (OMEGA2 - OMEGA) * (pp[i*m + j] - pp[i*m + j].conj());
        }
      }
    }
    for (uint64_t i = 0; i < n; ++i) {
      target[i] = (to[i]*INV3).a;
    }

    delete[] buf;
  }
};

int main(void) {
  Conv64 c;
  vector<int64_t> in1(500000), in2(500000);
  for (int64_t i = 0; i < 500000; ++i) {
    in1[i] = i % 2;
    in2[i] = (i + 1) % 2;
  }

  vector<int64_t> res = c.multiply(in1, in2);

  for (int64_t i = 0; i < res.size(); ++i) {
    cout << res[i] << ' ';
  }
  cout << '\n';

  return 0;
}

