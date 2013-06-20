#include "unitary.h"
#include "timer.h"
#include "mathf.h"
#include <cfloat>

extern "C" {
#include <gsl/gsl_poly.h>
#include <gsl/gsl_errno.h>
}

Unitary::Unitary(int qv, double thr, bool max, bool ver) {
  q=qv;
  eps=thr;
  verbose=ver;

  /// Maximize or minimize?
  if(max)
    sign=1;
  else
    sign=-1;

  // Defaults - use 4 points for fit of just derivative
  npoly_df=4; 
  // and 3 points for fit of both function and derivative
  npoly_fdf=3;
}

Unitary::~Unitary() {
}

void Unitary::check_unitary(const arma::cx_mat & W) const {
  arma::cx_mat prod=arma::trans(W)*W;
  for(size_t i=0;i<prod.n_cols;i++)
    prod(i,i)-=1.0;
  double norm=rms_cnorm(prod);
  if(norm>=sqrt(DBL_EPSILON))
    throw std::runtime_error("Matrix is not unitary!\n");
}

arma::cx_mat Unitary::get_rotation(double step) const {
  // Imaginary unit
  std::complex<double> imagI(0,1.0);

  return Hvec*arma::diagmat(arma::exp(sign*step*imagI*Hval))*arma::trans(Hvec);
}

void Unitary::set_poly(int ndf, int nfdf) {
  npoly_df=ndf;
  npoly_fdf=nfdf;
}

bool Unitary::converged(const arma::cx_mat & W) {
  /// Dummy default function, just check norm of gradient
  (void) W;
  return false;
}

double Unitary::optimize(arma::cx_mat & W, enum unitmethod met, enum unitacc acc, size_t maxiter) {
  // Old gradient
  arma::cx_mat oldG;
  G.zeros(W.n_cols,W.n_cols);

  if(W.n_cols<2) {
    // No optimization is necessary.
    W.eye();
    J=cost_func(W);
    return 0.0;
  }

  // Check matrix
  check_unitary(W);

  // Iteration number
  size_t k=0;
  J=0;

  while(true) {
    // Increase iteration number
    k++;
    
    Timer t;
  
    // Store old value
    oldJ=J;

    // Compute the cost function and the euclidean derivative, Abrudan 2009 table 3 step 2
    arma::cx_mat Gammak;
    cost_func_der(W,J,Gammak);

    // Riemannian gradient, Abrudan 2009 table 3 step 2
    oldG=G;
    G=Gammak*arma::trans(W) - W*arma::trans(Gammak);
    
    // Print progress
    if(verbose)
      print_progress(k);

    // H matrix
    if(k==1) {
      // First iteration; initialize with gradient
      H=G;

    } else {

      double gamma=0.0;
      if(acc==SDSA) {
	// Steepest descent / steepest ascent
	gamma=0.0;
      } else if(acc==CGPR) {
	// Compute Polak-Ribière coefficient
	gamma=bracket(G - oldG, G) / bracket(oldG, oldG);
      } else if(acc==CGFR) {
	// Fletcher-Reeves
	gamma=bracket(G, G) / bracket(oldG, oldG);
      } else
	throw std::runtime_error("Unsupported update.\n");
      
      H=G+gamma*H;
      
      // Check that update is OK
      if(bracket(G,H)<0.0) {
	H=G;
	printf("CG search direction reset.\n");
      }
    }
    
    // Check for convergence.
    if(bracket(G,G)<eps || converged(W)) {
      
      if(verbose) {
	fprintf(stderr," %10.3f\n",t.get());
	fflush(stderr);
	
	printf(" %s\nConverged.\n",t.elapsed().c_str());
	fflush(stdout);
	
	// Print classification
	classify(W);
      }
      
      break;
    } else if(k==maxiter) {
      if(verbose) {
	fprintf(stderr," %10.3f\n",t.get());
	fflush(stderr);

	printf(" %s\nNot converged.\n",t.elapsed().c_str());
	fflush(stdout);
      }

      break;
    }

    // Imaginary unit
    std::complex<double> imagI(0,1.0);

    // Diagonalize -iH to find eigenvalues purely imaginary
    // eigenvalues iw_i of H; Abrudan 2009 table 3 step 1.
    bool diagok=arma::eig_sym(Hval,Hvec,-imagI*H);
    if(!diagok) {
      ERROR_INFO();
      throw std::runtime_error("Unitary optimization: error diagonalizing H.\n");
    }

    // Find maximal eigenvalue
    double wmax=max(abs(Hval));
    if(wmax==0.0) {
      continue;
    }

    // Compute maximal step size.
    // Order of the cost function in the coefficients of W.
    Tmu=2.0*M_PI/(q*wmax);

    // Find optimal step size
    double step;
    if(met==POLY_DF) {
      step=polynomial_step_df(W);
      //      fprintf(stderr,"Polynomial_df  step %e\n",step);
    } else if(met==POLY_FDF) {
      step=polynomial_step_fdf(W);
      //      fprintf(stderr,"Polynomial_fdf step %e\n",step);
    } else if(met==ARMIJO) {
      step=armijo_step(W);
      //      fprintf(stderr,"Armijo         step %e\n",step);
    } else throw std::runtime_error("Method not implemented.\n");

    // Check step size
    if(step<0.0) throw std::runtime_error("Negative step size!\n");
    if(step==DBL_MAX) throw std::runtime_error("Could not find step size!\n");

    // Take step
    if(step!=0.0) {
      W=get_rotation(step)*W;
    }

    if(verbose) {
      fprintf(stderr," %10.3f\n",t.get());
      fflush(stderr);

      printf(" %s\n",t.elapsed().c_str());
      fflush(stdout);
    }
  }

  return J;
}

void Unitary::print_progress(size_t k) const {
  printf("\t%4i\t% e\t% e\t%e ",(int) k,J,J-oldJ,bracket(G,G));
  fflush(stdout);
}

void Unitary::classify(const arma::cx_mat & W) const {
  // Classify matrix
  double real=rms_norm(arma::real(W));
  double imag=rms_norm(arma::imag(W));
  
  printf("Transformation matrix is");
  if(imag<sqrt(DBL_EPSILON)*real)
    printf(" real");
  else if(real<sqrt(DBL_EPSILON)*imag)
    printf(" imaginary");
  else
    printf(" complex");
  
  printf(", re norm %e, im norm %e\n",real,imag);
}

double Unitary::polynomial_step_df(const arma::cx_mat & W) {
  // Step size
  const double deltaTmu=Tmu/(npoly_df-1);
  
  // Evaluate the first-order derivative of the cost function at the expansion points
  arma::vec mu(npoly_df);
  arma::vec Jprime(npoly_df);
  for(int i=0;i<npoly_df;i++) {
    // Mu in the point is
    mu(i)=i*deltaTmu;

    // Trial matrix is
    arma::cx_mat Wtr=get_rotation(mu(i))*W;
    // Compute derivative matrix
    arma::cx_mat der=cost_der(Wtr);
    // so the derivative wrt the step is
    Jprime(i)=sign*2.0*std::real(arma::trace(der*arma::trans(Wtr)*arma::trans(H)));
  }

  // Sanity check - is derivative of the right sign?
  if(sign*Jprime[0]<0.0) {
    ERROR_INFO();
    throw std::runtime_error("Derivative is of the wrong sign!\n");
  }

  // Fit derivative to polynomial of order p: J'(mu) = a0 + a1*mu + ... + ap*mu^p
  arma::vec coeff=fit_polynomial_df(mu,Jprime);

  // Find out zeros of the polynomial
  arma::vec roots=solve_roots(coeff);
  // and return the smallest positive one
  return smallest_positive(roots);
}

double Unitary::polynomial_step_fdf(const arma::cx_mat & W) {
  // Step size
  const double deltaTmu=Tmu/(npoly_fdf-1);
  
  // Evaluate the first-order derivative of the cost function at the expansion points
  arma::vec mu(npoly_fdf);
  arma::vec f(npoly_fdf);
  arma::vec fp(npoly_fdf);
  for(int i=0;i<npoly_fdf;i++) {
    // Value of mu is
    mu(i)=i*deltaTmu;

    // Trial matrix is
    arma::cx_mat Wtr=get_rotation(mu(i))*W;
    arma::cx_mat der;
    
    if(i==0) {
      f[i]=J;
      der=G[G.size()-1];
    } else
      cost_func_der(Wtr*W,f[i],der);
    
    // Compute the derivative
    fp[i]=sign*2.0*std::real(arma::trace(der*arma::trans(Wtr)*arma::trans(H)));
  }

  // Fit function to polynomial of order p
  //  J(mu)  = a_0 + a_1*mu + ... + a_(p-1)*mu^(p-1)
  // and its derivative to the function
  //  J'(mu) = a_1 + 2*a_2*mu + ... + (p-1)*a_(p-1)*mu^(p-2).
  arma::vec ader=fit_polynomial_fdf(mu,f,fp);

  // Find out zeros of the polynomial
  arma::vec roots=solve_roots(ader);
  // and return the smallest positive one
  return smallest_positive(roots);
}

double Unitary::armijo_step(const arma::cx_mat & W) {
  // Start with half of maximum.
  double step=Tmu/2.0;

  // Initial rotation matrix
  arma::cx_mat R=get_rotation(step);

  // Evaluate function at R2
  double J2=cost_func(R*R*W);

  if(sign==-1) {
    // Minimization.

    // First condition: f(W) - f(R^2 W) >= mu*<G,H>
    while(J-J2 >= step*bracket(G,H)) {
      // Increase step size.
      step*=2.0;
      R=get_rotation(step);

      // and re-evaluate J2
      J2=cost_func(R*R*W);
    }

    // Evaluate function at R
    double J1=cost_func(R*W);

    // Second condition: f(W) - f(R W) <= mu/2*<G,H>
    while(J-J1 < step/2.0*bracket(G,H)) {
      // Decrease step size.
      step/=2.0;
      R=get_rotation(step);

      // and re-evaluate J1
      J1=cost_func(R*W);
    }

  } else if(sign==1) {
    // Maximization

    // First condition: f(W) - f(R^2 W) >= mu*<G,H>
    while(J-J2 <= -step*bracket(G,H)) {
      // Increase step size.
      step*=2.0;
      R=get_rotation(step);

      // and re-evaluate J2
      J2=cost_func(R*R*W);
    }

    // Evaluate function at R
    double J1=cost_func(R*W);

    // Second condition: f(W) - f(R W) <= mu/2*<G,H>
    while(J-J1 > -step/2.0*bracket(G,H)) {
      // Decrease step size.
      step/=2.0;
      R=get_rotation(step);

      // and re-evaluate J1
      J1=cost_func(R*W);
    }
  } else
    throw std::runtime_error("Invalid optimization direction!\n");

  return step;
}

double bracket(const arma::cx_mat & X, const arma::cx_mat & Y) {
  return 0.5*std::real(arma::trace(arma::trans(X)*Y));
}

arma::cx_vec solve_roots_cplx(const arma::vec & a) {
  // Find roots of a_0 + a_1*mu + ... + a_(p-1)*mu^(p-1) = 0.
  // Coefficient of highest order term must be nonzero.
  size_t r=a.size();
  while(a(r-1)==0.0)
    r--;
  
  // GSL routine workspace
  gsl_poly_complex_workspace *w=gsl_poly_complex_workspace_alloc(r);
  
  // Collect coefficients
  double ad[r];
  for(size_t i=0;i<r;i++)
    ad[i]=a(i);
  
  // Return values
  double z[2*(r-1)];
  int gslok=gsl_poly_complex_solve(ad,r,w,z);
  
  if(gslok!=GSL_SUCCESS) {
    ERROR_INFO();
    fprintf(stderr,"Solution of polynomial root failed, error: \"%s\"\n",gsl_strerror(gslok));
    throw std::runtime_error("Error solving polynomial.\n");
  }
  
  // Get roots
  arma::cx_vec roots(r-1);
  for(size_t i=0;i<r-1;i++) {
    roots(i).real()=z[2*i];
    roots(i).imag()=z[2*i+1];
  }

  // Sort the roots (Armadillo's sort is broken at least in 3.900)
  roots=arma::sort(roots);

  printf("\n");
  a.print("Coefficients");
  roots.print("Roots");

  return roots;
}

arma::vec solve_roots(const arma::vec & a) {
  // Solve the roots
  arma::cx_vec croots=solve_roots_cplx(a);

  // Collect real roots
  size_t nreal=0;
  for(size_t i=0;i<croots.n_elem;i++)
    if(fabs(std::imag(croots[i]))<10*DBL_EPSILON)
      nreal++;

  // Real roots
  arma::vec roots(nreal);
  size_t ir=0;
  for(size_t i=0;i<croots.n_elem;i++)
    if(fabs(std::imag(croots(i)))<10*DBL_EPSILON)
      roots(ir++)=std::real(croots(i));

  // Sort roots
  roots=arma::sort(roots);
  
  roots.print("Real roots");

  return roots;
}

double smallest_positive(const arma::vec & a) {
  double step=0.0;
  for(size_t i=0;i<a.size();i++) {
    step=a(i);

    // Omit extremely small steps because they might get you stuck.
    if(step>sqrt(DBL_EPSILON))
      break;
  }
  return step;
}

arma::vec fit_polynomial_df(const arma::vec & x, const arma::vec & y) {
  // Fit derivative to polynomial of order p: y(x) = a_0 + a_1*x + ... + a_(p-1)*x^(p-1)

  if(x.n_elem!=y.n_elem) {
    ERROR_INFO();
    throw std::runtime_error("x and y have different dimensions!\n");
  }
  size_t N=x.n_elem;

  // Form mu matrix
  arma::mat mumat(N,N);
  mumat.zeros();
  for(size_t i=0;i<N;i++)
    for(size_t j=0;j<N;j++)
      mumat(i,j)=pow(x(i),j);
  
  // Solve for coefficients. Use inverse matrix, as mumat might be ill
  // conditioned.
  arma::mat invmu;
  bool solveok=arma::inv(invmu,mumat);
  arma::vec coeffs;
  if(solveok)
    coeffs=invmu*y;
  else {
    arma::trans(x).print("x");
    arma::trans(y).print("y");
    mumat.print("Mu");
    throw std::runtime_error("Error solving for coefficients a.\n");
  }
  
  return coeffs;
}

arma::vec fit_polynomial_fdf(const arma::vec & x, const arma::vec & y, const arma::vec & dy) {
  // Fit function and its derivative to polynomial of order p:
  // y(x)  = a_0 + a_1*x + ... +       a_(p-1)*x^(p-1)
  // y'(x) =       a_1   + ... + (p-1)*a_(p-1)*x^(p-2)
  // return coefficients of y'

  if(x.n_elem!=y.n_elem) {
    ERROR_INFO();
    throw std::runtime_error("x and y have different dimensions!\n");
  }
  if(y.n_elem!=dy.n_elem) {
    ERROR_INFO();
    throw std::runtime_error("y and dy have different dimensions!\n");
  }

  // Length of vectors is
  size_t N=x.n_elem;
  // The polynomial degree will then be p-1
  size_t p=2*N;

  // Form mu matrix.
  arma::mat mumat(p,p);
  mumat.zeros();
  // First y(x)
  for(size_t i=0;i<N;i++)
    for(size_t j=0;j<p;j++)
      mumat(i,j)=pow(x(i),j);
  // Then y'(x)
  for(size_t i=0;i<N;i++)
    for(size_t j=1;j<p;j++)
      mumat(i+N,j)=j*pow(x(i),j-1);
  
  // Form rhs vector
  arma::vec data(2*N);
  data.subvec(0,N-1)=y;
  data.subvec(N,2*N-1)=dy;

  // Solve for coefficients. Use inverse matrix, as mumat might be ill
  // conditioned.
  arma::mat invmu;
  bool solveok=arma::inv(invmu,mumat);
  arma::vec c;
  if(solveok)
    c=invmu*data;
  else {
    arma::trans(x).print("x");
    arma::trans(y).print("y");
    arma::trans(dy).print("dy");
    mumat.print("Mu");
    throw std::runtime_error("Error solving for coefficients a.\n");
  }

  // Coefficients for polynomial expansion of y'
  arma::vec cder(c.n_elem-1);
  for(size_t i=1;i<c.n_elem;i++)
    cder(i-1)=i*c(i);
  
  return cder;
}


Brockett::Brockett(size_t N, unsigned long int seed) : Unitary(2, sqrt(DBL_EPSILON), true, true) {
  // Get random complex matrix
  sigma=randn_mat(N,N,seed)+std::complex<double>(0.0,1.0)*randn_mat(N,N,seed+1);
  // Hermitize it
  sigma=sigma+arma::trans(sigma);
  // Get N matrix
  Nmat.zeros(N,N);
  for(size_t i=1;i<=N;i++)
    Nmat(i,i)=i;

  log=fopen("brockett.dat","w");
}

Brockett::~Brockett() {
  fclose(log);
}

bool Brockett::converged(const arma::cx_mat & W) {
  // Update diagonality and unitarity criteria
  unit=unitarity(W);
  diag=diagonality(W);
  // Dummy return
  return false;
}

double Brockett::cost_func(const arma::cx_mat & W) {
  return std::real(arma::trace(arma::trans(W)*sigma*W*Nmat));
}

arma::cx_mat Brockett::cost_der(const arma::cx_mat & W) {
  return sigma*W*Nmat;
}

void Brockett::cost_func_der(const arma::cx_mat & W, double & f, arma::cx_mat & der) {
  f=cost_func(W);
  der=cost_der(W);
}

void Brockett::print_progress(size_t k) const {
  printf("%4i % e % e % e % e",(int) k, J, bracket(G,G), diag, unit);

  fprintf(log,"%4i % e % e % e % e\n",(int) k, J, 10*log10(bracket(G,G)), diag, unit);
  fflush(log);
}

double Brockett::diagonality(const arma::cx_mat & W) const {
  arma::cx_mat WSW=arma::trans(W)*sigma*W;

  double off=0.0;
  double dg=0.0;

  for(size_t i=0;i<WSW.n_cols;i++)
    dg+=std::norm(WSW(i,i));

  for(size_t i=0;i<WSW.n_cols;i++) {
    for(size_t j=0;j<i;j++)
      off+=std::norm(WSW(i,j));
    for(size_t j=i+1;j<WSW.n_cols;j++)
      off+=std::norm(WSW(i,j));
  }

  return 10*log10(off/dg);
}

double Brockett::unitarity(const arma::cx_mat & W) const {
  arma::cx_mat U=W*arma::trans(W);
  arma::cx_mat eye(W);
  eye.eye();
  
  double norm=arma::norm(U-eye,"fro");
  return 10*log10(norm);
}

