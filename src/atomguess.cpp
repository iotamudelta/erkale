/*
 *                This source code is part of
 *
 *                     E  R  K  A  L  E
 *                             -
 *                       HF/DFT from Hel
 *
 * Written by Jussi Lehtola, 2010-2011
 * Copyright (c) 2010-2011, Jussi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "atomguess.h"
#include "checkpoint.h"
#include "scf.h"
#include "timer.h"
#include <algorithm>

arma::mat atomic_density(const BasisSet & basis) {
  // First of all, we need to determine which atoms are identical in
  // the way that the basis sets coincide.

  // Get list of identical nuclei
  std::vector< std::vector<size_t> > idnuc=identical_nuclei(basis);

  // Amount of basis functions is
  size_t Nbf=basis.get_Nbf();

  // Density matrix
  arma::mat P(Nbf,Nbf);
  P.zeros();
  
  // SCF settings to use
  Settings set;
  set.add_scf_settings();
  set.set_bool("Verbose",false);
  set.set_bool("CoreGuess",true);

  printf("Performing atomic guess for atoms:\n");
  Timer ttot;

  fprintf(stderr,"Calculating initial atomic guess ... ");
  fflush(stderr);
  
  // Loop over list of identical nuclei
  for(size_t i=0;i<idnuc.size();i++) {

    Timer tsol;

    printf("\t");
    for(size_t iid=0;iid<idnuc[i].size();iid++)
      printf("%i ",(int) idnuc[i][iid]);
    fflush(stdout);

    // Nucleus is
    nucleus_t nuc=basis.get_nucleus(idnuc[i][0]);
    // Set number
    nuc.ind=0;

    // Construct the basis set
    BasisSet atbas(1,set);
    // Add the nucleus
    atbas.add_nucleus(nuc);

    // Add the shells
    std::vector<GaussianShell> shells=basis.get_funcs(idnuc[i][0]);
    for(size_t ish=0;ish<shells.size();ish++)
      // Add shell on zeroth atom, don't sort
      atbas.add_shell(0,shells[ish],false);

    // Finalize basis set
    atbas.finalize();

    // Determine ground state
    gs_conf_t gs=get_ground_state(nuc.Z);

    // Set multiplicity
    set.set_int("Multiplicity",gs.mult);
    printf("Atom with Z=%i has multiplicity %i.\n",nuc.Z,gs.mult);


    // Checkpoint
    Checkpoint chkpt(".erkale.at",true);

    // Solver
    SCF solver(atbas,set,chkpt);

    convergence_t conv;
    conv.deltaEmax=set.get_double("DeltaEmax");
    conv.deltaPmax=set.get_double("DeltaPmax");
    conv.deltaPrms=set.get_double("DeltaPrms");

    // Count number of electrons
    int Nel_alpha;
    int Nel_beta;
    get_Nel_alpha_beta(atbas.Ztot()-set.get_int("Charge"),set.get_int("Multiplicity"),Nel_alpha,Nel_beta);

    // Solve ROHF
    uscf_t sol;
    solver.ROHF(sol,Nel_alpha,Nel_beta,conv);
    
    // Re-get shells, in new indexing.
    shells=atbas.get_funcs(0);

    // Loop over shells
    for(size_t ish=0;ish<shells.size();ish++)
      for(size_t jsh=0;jsh<shells.size();jsh++) {

	// Loop over identical nuclei
	for(size_t iid=0;iid<idnuc[i].size();iid++) {
	  // Get shells on nucleus
	  std::vector<GaussianShell> idsh=basis.get_funcs(idnuc[i][iid]);

	  // Store density
	  P.submat(idsh[ish].get_first_ind(),idsh[jsh].get_first_ind(),idsh[ish].get_last_ind(),idsh[jsh].get_last_ind())=sol.P.submat(shells[ish].get_first_ind(),shells[jsh].get_first_ind(),shells[ish].get_last_ind(),shells[jsh].get_last_ind());
	}
      }

    printf(" (%s)\n",tsol.elapsed().c_str());
  }

  fprintf(stderr,"done (%s)\n\n",ttot.elapsed().c_str());

  return P;
}

std::vector< std::vector<size_t> > identical_nuclei(const BasisSet & basis) {
  // Returned list
  std::vector< std::vector<size_t> > ret;

  // Loop over nuclei
  for(size_t i=0;i<basis.get_Nnuc();i++) {
    // Check that nucleus isn't BSSE
    nucleus_t nuc=basis.get_nucleus(i);
    if(nuc.bsse)
      continue;

    // Get the shells on the nucleus
    std::vector<GaussianShell> shi=basis.get_funcs(i);

    // Check if there something already on the list
    bool found=false;
    for(size_t j=0;j<ret.size();j++) {
      std::vector<GaussianShell> shj=basis.get_funcs(ret[j][0]);

      // Check nuclear type
      if(basis.get_symbol(i).compare(basis.get_symbol(ret[j][0]))!=0)
	continue;

      // Do comparison
      if(shi.size()!=shj.size())
	continue;
      else {

	bool same=true;
	for(size_t ii=0;ii<shi.size();ii++) {
	  // Check angular momentum
	  if(shi[ii].get_am()!=shj[ii].get_am()) {
	    same=false;
	    break;
	  }

	  // and exponents
	  std::vector<contr_t> lhc=shi[ii].get_contr();
	  std::vector<contr_t> rhc=shj[ii].get_contr();

	  if(lhc.size() != rhc.size()) {
	    same=false;
	    break;
	  }
	  for(size_t i=0;i<lhc.size();i++) {
	    if(!(lhc[i]==rhc[i])) {
	      same=false;
	      break;
	    }
	  }

	  if(!same)
	    break;
	}

	if(same) {
	  // Found identical atom.
	  found=true;

	  // Add it to the list.
	  ret[j].push_back(i);
	}
      }
    }

    if(!found) {
      // Didn't find the atom, add it to the list.
      std::vector<size_t> tmp;
      tmp.push_back(i);

      ret.push_back(tmp);
    }
  }

  return ret;
}


bool operator<(const el_conf_t & lhs, const el_conf_t & rhs) {
  if(lhs.n + lhs.l < rhs.n + rhs.l)
    return true;
  else if(lhs.n + lhs.l == rhs.n + rhs.l)
    return lhs.n < rhs.n;

  return false;
}

std::vector<el_conf_t> get_occ_order(int nmax) {
  std::vector<el_conf_t> confs;
  for(int n=1;n<nmax;n++)
    for(int l=0;l<n;l++) {
      el_conf_t tmp;
      tmp.n=n;
      tmp.l=l;
      confs.push_back(tmp);
    }
  std::sort(confs.begin(),confs.end());

  return confs;
}

gs_conf_t get_ground_state(int Z) {
  // The returned configuration
  gs_conf_t ret;

  // Get the ordering of the shells
  std::vector<el_conf_t> confs=get_occ_order(8);

  // Start occupying.
  size_t i=0;
  while(Z>=2*(2*confs[i].l+1)) {
    Z-=2*(2*confs[i].l+1);
    i++;
  }

  if(Z==0) {
    // All shells are full.
    ret.mult=1;
    ret.L=0;
    ret.dJ=0;
  } else {
    // Determine how the states electrons are occupied.
    
    arma::imat occs(2*confs[i].l+1,2);
    occs.zeros();

    // Column to fill
    int col=0;
    do {
      // Occupy column
      for(int ml=confs[i].l;ml>=-confs[i].l;ml--)
	if(Z>0) {
	  occs(confs[i].l-ml,col)=1;
	  Z--;
	}

      // If we still have electrons left, switch to the next column
      if(Z>0) {
	if(col==0)
	  col=1;
	else {
	  ERROR_INFO();
	  throw std::runtime_error("Should not end up here!\n");
	}
      }
    } while(Z>0);
    
    // Compute S and L value
    int m=0, L=0, dJ=0;
    for(size_t j=0;j<occs.n_rows;j++) {
      m+=occs(j,0)-occs(j,1);
      L+=(confs[i].l-j)*(occs(j,0)+occs(j,1));
    }

    if(col==0)
      dJ=abs(2*L-m);
    else if(col==1)
      dJ=2*L+m;

    ret.mult=m+1;
    ret.L=L;
    ret.dJ=dJ;
  }

  return ret;
}