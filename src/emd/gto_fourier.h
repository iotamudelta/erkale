/*
 *                This source code is part of
 *
 *                     E  R  K  A  L  E
 *                             -
 *                       DFT from Hel
 *
 * Written by Susi Lehtola, 2010-2011
 * Copyright (c) 2010-2011, Susi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */



#ifndef ERKALE_FOURIER
#define ERKALE_FOURIER

#include "../global.h"
#include <armadillo>
#include <complex>
#include <vector>
#include <cstddef>

/// Helper for 1D Fourier polynomials
typedef struct {
  /// Full polynomial is of the form \f$ \sum_l c_l p_x^l \f$
  std::complex<double> c;
  /// Full polynomial is of the form \f$ \sum_l c_l p_x^l \f$
  int l;
} poly1d_t;

/// Comparison operator for sorting
bool operator<(const poly1d_t & lhs, const poly1d_t & rhs);
/// Comparison for addition
bool operator==(const poly1d_t & lhs, const poly1d_t & rhs);

/**
 *
 * \class FourierPoly_1D
 *
 * \brief Computes the 1-dimensional Fourier polynomial needed for the
 * Fourier transforms of Gaussian basis functions
 *
 *
 * The polynomial is computed using the recursion relation
 *
 * \f$ \mathcal{R}_l (p_i, \zeta) = -i p_i \mathcal{R}_{l-1} (p_i,
 * \zeta) + 2 \zeta (l-1) \mathcal{R}_{l-2} (p_i, \zeta) \f$
 *
 * with the initial values
 *
 * \f$ \mathcal{R}_0 (p_i, \zeta) = 1 \f$
 *
 * \f$ \mathcal{R}_1 (p_i, \zeta) = -ip_i \f$
 *
 * The recursion formula is given in the article
 *
 * L. C. Snyder and T. A. Weber, "The Compton profile of water:
 * Computed from an SCF–MO wavefunction in a double-zeta Gaussian
 * basis set", J. Chem. Phys. 63 (1975), pp. 113 - 114.
 *
 *
 * \author Susi Lehtola
 * \date 2011/05/10 15:32
 */

class FourierPoly_1D {
  /// 1-dimensional Fourier polynomial
  std::vector<poly1d_t> poly;

  /**
   * Helper for the constructor - use recursion formula to compute
   * polynomial *without* normalization factor, which is only added at
   * the very end. */
  FourierPoly_1D formpoly(int l, double zeta);

 public:
  /// Dummy constructor
  FourierPoly_1D();
  /// Compute polynomial, with the proper normalization factor
  FourierPoly_1D(int l, double zeta);
  /// Destructor
  ~FourierPoly_1D();

  /// Add a term in the contraction
  void addterm(const poly1d_t & term);

  /// Addition operator
  FourierPoly_1D operator+(const FourierPoly_1D & rhs) const;

  /// Get number of terms in the polynomial
  size_t getN() const;
  /// Get the i:th contraction coefficient
  std::complex<double> getc(size_t i) const;
  /// Get the exponent of p in the i:th term
  int getl(size_t i) const;

  /// Print polynomial
  void print() const;

  friend FourierPoly_1D operator*(std::complex<double> fac, const FourierPoly_1D & rhs);
};

/// Multiply the polynomial with a complex factor
FourierPoly_1D operator*(std::complex<double> fac, const FourierPoly_1D & rhs);


/* Then, the full three-dimensional transform */


/// Fourier transform of GTO is of the form \f$ c_{l,m,n} p_x^l p_y^m p_z^n exp(-z p^2) \f$
typedef struct {
  /// Expansion coefficient
  std::complex<double> c;
  /// px^l
  int l;
  /// py^m
  int m;
  /// pz^n
  int n;
  /// exp(-z*p^2)
  double z;
} trans3d_t;

/// Comparison operator for sorting
bool operator<(const trans3d_t & lhs, const trans3d_t& rhs);
/// Comparison operator for addition
bool operator==(const trans3d_t & lhs, const trans3d_t& rhs);

/**
 *
 * \class GTO_Fourier
 *
 * \brief Compute Fourier transform of Gaussian Type Orbital
 *
 * This class contains routines for computing the Fourier transform of
 * Gaussian Type Orbitals using recursion relations.
 *
 * \author Susi Lehtola
 * \date 2011/05/10 15:32
 */

class GTO_Fourier {
  /// The terms of the Fourier transformed GTO
  std::vector<trans3d_t> trans;
 public:
  /// Dummy constructor
  GTO_Fourier();
  /// Form 3d Fourier polynomial from x^l * y^m * z^n * exp(-zeta*r^2)
  GTO_Fourier(int l, int m, int n, double zeta);
  /// Destructor
  ~GTO_Fourier();

  /// Add a term in the contraction
  void addterm(const trans3d_t & term);

  /// Addition operator
  GTO_Fourier operator+(const GTO_Fourier & rhs) const;
  /// Addition operator
  GTO_Fourier & operator+=(const GTO_Fourier & rhs);

  /// Get the expansion in terms
  std::vector<trans3d_t> get() const;

  /// Print Fourier transform
  void print() const;

  // Clean out the expansion
  void clean();

  // Evaluate the expansion at p
  std::complex<double> eval(double px, double py, double pz) const;

  friend GTO_Fourier operator*(std::complex<double> fac, const GTO_Fourier & rhs);
  friend GTO_Fourier operator*(double fac, const GTO_Fourier & rhs);
};

/// Scale Fourier transform of GTO by factor fac
GTO_Fourier operator*(std::complex<double> fac, const GTO_Fourier & rhs);
/// Scale Fourier transform of GTO by factor fac
GTO_Fourier operator*(double fac, const GTO_Fourier & rhs);

class BasisSet;

/// Fourier transform basis set
std::vector< std::vector<GTO_Fourier> > fourier_expand(const BasisSet & bas, std::vector< std::vector<size_t> > & idents);

/// Evaluate EMD
double eval_emd(const BasisSet & basis, const arma::mat & P, const std::vector< std::vector<GTO_Fourier> > & fourier, const std::vector< std::vector<size_t> > & idents, double px, double py, double pz);

#endif
