/*
 *                This source code is part of
 *
 *                     E  R  K  A  L  E
 *                             -
 *                       HF/DFT from Hel
 *
 * Written by Susi Lehtola, 2010-2011
 * Copyright (c) 2010-2011, Susi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "elements.h"
#include "dftfuncs.h"
#include "guess.h"
#include "checkpoint.h"
#include "linalg.h"
#include "settings.h"
#include "stringutil.h"
#include "timer.h"
#include <algorithm>

void atomic_guess(const BasisSet & basis, size_t inuc, const std::string & method, std::vector<size_t> & shellidx, BasisSet & atbas, arma::vec & atE, arma::mat & atP, bool dropshells, bool sphave, int Q) {
  // Nucleus is
  nucleus_t nuc=basis.get_nucleus(inuc);

  // Set number
  nuc.ind=0;
  nuc.r.x=0.0;
  nuc.r.y=0.0;
  nuc.r.z=0.0;

  // Settings to use
  Settings set;
  set.add_scf_settings();
  set.add_dft_settings();
  set.add_bool("ForcePol","Force polarized calculation",true);
  set.add_string("SaveChk","Save calculation to","");
  set.add_string("LoadChk","Load calculation from","");
  set.add_bool("FreezeCore","Freeze atomic cores",false);

  set.set_string("Guess","Core");
  set.set_int("MaxIter",200);
  set.set_bool("DensityFitting",false);
  set.set_bool("Verbose",false);
  set.set_bool("Direct",false);
  set.set_bool("DensityFitting",false);
  // Use a rather large grid to make sure the calculation converges
  // even in cases where the functional requires a large grid to be
  // used. The other way would be to pass the user settings to this
  // routine..
  set.set_string("DFTGrid","100 17");
  
  // Don't do PZ-SIC for the initial guess.
  try {
    set.set_bool("PZ",false);
  } catch(...) {
  }
  // Also, turn off non-local correlation for initial guess
  set.set_string("VV10","False");
  
  // Use default convergence settings
  set.set_bool("UseDIIS",true);
  set.set_int("DIISOrder",20);
  set.set_bool("UseADIIS",true);
  set.set_bool("UseBroyden",false);
  set.set_bool("UseTRRH",false);
  // and default charge
  set.set_int("Charge", Q);

  // Method
  set.set_string("Method",method);

  // Relax convergence requirements - open shell atoms may be hard to
  // converge
  set.set_double("DeltaPmax",1e-5);
  set.set_double("DeltaPrms",1e-6);

  // Construct the basis set
  atbas=BasisSet(1,set);
  // Add the nucleus
  atbas.add_nucleus(nuc);
  
  // Add the shells relevant for a single atom.
  int ammax;
  if(dropshells) {
    if(nuc.Z-Q<3)
      // Only s electrons up to lithium
      ammax=0;
    else if(nuc.Z-Q<21)
      // s and p electrons
      ammax=1;
    else if(nuc.Z-Q<57)
      // s, p and d electrons
      ammax=2;
    else
      // s, p, d and f electrons
      ammax=3;
  } else {
    ammax=basis.get_max_am();
  }
  
  std::vector<GaussianShell> shells=basis.get_funcs(inuc);
  // Indices of shells included
  shellidx.clear();
  for(size_t ish=0;ish<shells.size();ish++) {
    if(shells[ish].get_am()<=ammax) {
      // Add shell on zeroth atom, don't sort
      atbas.add_shell(0,shells[ish],false);
      shellidx.push_back(ish);
    }
  }
  
  // Finalize basis set
  atbas.finalize();
  
  // Sanity check for "artificial" basis sets (e.g. only f functions)
  if(ammax < basis.get_max_am() && (int) atbas.get_Nbf()<nuc.Z-Q) {
    // Add the rest of the shells
    for(size_t ish=0;ish<shells.size();ish++)
      if(shells[ish].get_am()>ammax) {
	atbas.add_shell(0,shells[ish],false);
	shellidx.push_back(ish);
      }
    // Refinalize
    atbas.finalize();
  }
  
  // Determine ground state of charged species
  gs_conf_t gs=get_ground_state(nuc.Z-Q);
  
  // Set multiplicity
  set.set_int("Multiplicity",gs.mult);

  // Force polarized calculation
  set.set_bool("ForcePol",true);

  // Get occupancies
  std::vector<double> occa, occb;
  get_unrestricted_occupancy(set,atbas,occa,occb,sphave);

  // Pad to same length
  while(occb.size()<occa.size())
    occb.push_back(0.0);

  std::ostringstream occs;
  for(size_t i=0;i<occa.size();i++)
    occs << occa[i] << " " << occb[i] << " ";
  set.set_string("Occupancies",occs.str());

  // Temporary file name
  char *tmpname=tempnam("./",".chk");
  set.set_string("SaveChk",tmpname);
  
  // Run calculation
  calculate(atbas,set);
  
  // Load energies and density matrix
  {
    // Checkpoint
    Checkpoint chkpt(tmpname,false);
    
    chkpt.read("Ea",atE);
    chkpt.read("P",atP);
  }
  
  // Remove temporary file
  remove(tmpname);
  // Free memory
  free(tmpname);
}

arma::mat atomic_guess(const BasisSet & basis, Settings set, bool dropshells, bool sphave) {
  // First of all, we need to determine which atoms are identical in
  // the way that the basis sets coincide.

  // Get list of identical nuclei
  std::vector< std::vector<size_t> > idnuc=identical_nuclei(basis);

  // Amount of basis functions is
  size_t Nbf=basis.get_Nbf();

  // Density matrix
  arma::mat P(Nbf,Nbf);
  P.zeros();

  // Print out info?
  bool verbose=set.get_bool("Verbose");

  std::string method=set.get_string("Method");
  if(stricmp(set.get_string("AtomGuess"),"Auto")!=0)
    method=set.get_string("AtomGuess");
  
  if(verbose) {
    // Parse method
    bool hf= (stricmp(method,"HF")==0);
    if(hf)
      method="HF";
    else {   
      bool rohf=(stricmp(method,"ROHF")==0);
      if(rohf)
	method="ROHF";
      else {
	// Parse functional
	dft_t dft;
	parse_xc_func(dft.x_func,dft.c_func,method);
	if(dft.c_func>0) {
	  // Correlation exists.
	  method=get_keyword(dft.x_func)+"-"+get_keyword(dft.c_func);
	} else
	  method=get_keyword(dft.x_func);
      }
    }
    
    printf("Performing %s guess for atoms:\n",method.c_str());
    fprintf(stderr,"Calculating initial atomic guess ... ");
    fflush(stdout);
    fflush(stderr);
  }

  Timer ttot;

  // Loop over list of identical nuclei
  for(size_t i=0;i<idnuc.size();i++) {

    Timer tsol;

    if(verbose) {
      printf("%-2s:",basis.get_nucleus(idnuc[i][0]).symbol.c_str());
      for(size_t iid=0;iid<idnuc[i].size();iid++)
	printf(" %i",(int) idnuc[i][iid]+1);
      fflush(stdout);
    }

    BasisSet atbas;
    arma::vec atE;
    arma::mat atP;
    std::vector<size_t> shellidx;

    // Perform the guess
    atomic_guess(basis,idnuc[i][0],method,shellidx,atbas,atE,atP,dropshells,sphave,basis.get_nucleus(idnuc[i][0]).Q);
    // Get the atomic shells
    std::vector<GaussianShell> shells=atbas.get_funcs(0);
    
    // Loop over shells
    for(size_t ish=0;ish<shells.size();ish++)
      for(size_t jsh=0;jsh<shells.size();jsh++) {
	
	// Loop over identical nuclei
	for(size_t iid=0;iid<idnuc[i].size();iid++) {
	  // Get shells on nucleus
	  std::vector<GaussianShell> idsh=basis.get_funcs(idnuc[i][iid]);
	  
	  // Store density
	  P.submat(idsh[shellidx[ish]].get_first_ind(),idsh[shellidx[jsh]].get_first_ind(),idsh[shellidx[ish]].get_last_ind(),idsh[shellidx[jsh]].get_last_ind())=atP.submat(shells[ish].get_first_ind(),shells[jsh].get_first_ind(),shells[ish].get_last_ind(),shells[jsh].get_last_ind());
	}
      }
    
    if(verbose) {
      printf(" (%s)\n",tsol.elapsed().c_str());
      fflush(stdout);
    }
  }
  
  /*
  // Check that density matrix contains the right amount of electrons
  int Neltot=basis.Ztot()-set.get_int("Charge");
  double Nel=arma::trace(P*S);
  if(fabs(Nel-Neltot)/Neltot*100>1e-10)
    fprintf(stderr,"Nel = %i, P contains %f electrons, difference %e.\n",Neltot,Nel,Nel-Neltot);
  */

  if(verbose) {
    printf("Atomic guess formed in %s.\n\n",ttot.elapsed().c_str());
    fprintf(stderr,"done (%s)\n\n",ttot.elapsed().c_str());
    fflush(stderr);
  }

  return P;
}

void molecular_guess(const BasisSet & basis, const Settings & set, std::string & chkname) {
  Timer t;

  // Get temporary file name
  char *tmpn=tempnam("./",".chk");
  std::string tempname=std::string(tmpn);
  free(tmpn);

  // New settings
  Settings newset(set);
  newset.set_string("LoadChk","");
  newset.set_string("SaveChk",tempname);
  newset.set_string("Guess","atomic");
  newset.set_bool("DensityFitting",false);
  newset.set_bool("Verbose",true);

  // Use relaxed convergence settings
  newset.set_double("DeltaEmax",std::max(set.get_double("DeltaEmax"),1e-6));
  newset.set_double("DeltaPmax",std::max(set.get_double("DeltaPmax"),1e-5));
  newset.set_double("DeltaPrms",std::max(set.get_double("DeltaPrms"),1e-6));

  // Construct new basis
  BasisSet newbas(basis.get_Nnuc(),newset);

  // Add the nuclei
  std::vector<nucleus_t> nuclei=basis.get_nuclei();
  for(size_t i=0;i<nuclei.size();i++)
    newbas.add_nucleus(nuclei[i]);

  // Indices of added shells
  std::vector<size_t> addedidx;
  // Indices of missing shells
  std::vector<size_t> missingidx;

  // Add the shells
  std::vector<GaussianShell> shells=basis.get_shells();
  for(size_t i=0;i<shells.size();i++) {
    // Add the shell to the minimal basis? Default is true
    bool add=true;

    // Check for polarization shells
    if(shells[i].get_am() > atom_am(nuclei[shells[i].get_center_ind()].Z))
      add=false;

    if(add) {
      // Add the shell to the basis set
      newbas.add_shell(shells[i].get_center_ind(),shells[i],false);
      // and to the list
      addedidx.push_back(i);
    } else
      // Add the shell to the missing list
      missingidx.push_back(i);
  }
  newbas.finalize();

  // Now we have built the basis set and we can proceed with the solution.
  printf("Calculating molecular guess.\nFull basis has %i functions, whereas reduced basis only has %i.\n",(int) basis.get_Nbf(),(int) newbas.get_Nbf());
  fprintf(stderr,"Calculating molecular guess.\nFull basis has %i functions, whereas reduced basis only has %i.\n",(int) basis.get_Nbf(),(int) newbas.get_Nbf());

  // Calculate the solution in the temporary file.
  calculate(newbas,newset);

  printf("\nSolving the density in the reduced basis took %s.\n",t.elapsed().c_str());
  fflush(stdout);
  fprintf(stderr,"\nSolving the density in the reduced basis took %s.\n",t.elapsed().c_str());
  fflush(stderr);
  t.set();

  // Get another temporary file name. This will contain the returned
  // orbitals and energies.
  tmpn=tempnam("./",".chk");
  chkname=std::string(tmpn);
  free(tmpn);

  // Open the return file
  Checkpoint chkpt(chkname,true);

  {
    // Open the temp file
    Checkpoint load(tempname,false);

    bool restr;
    load.read("Restricted",restr);

    if(restr) {
      BasisSet oldbas;
      arma::mat Cold;
      arma::vec Eold;
      load.read(oldbas);
      load.read("E",Eold);
      load.read("C",Cold);

      // Project the orbitals
      arma::mat C=project_orbitals(Cold,oldbas,basis);
      chkpt.write("C",C);

      // and generate dummy energies
      arma::vec E(C.n_cols);
      E.subvec(0,Eold.n_elem-1)=Eold;
      for(size_t i=Eold.n_elem;i<E.n_elem;i++)
	E(i)=Eold(Eold.n_elem-1);
      chkpt.write("E",E);

    } else {
      BasisSet oldbas;
      arma::mat Caold, Cbold;
      arma::vec Eaold, Ebold;

      load.read(oldbas);
      load.read("Ca",Caold);
      load.read("Cb",Cbold);
      load.read("Ea",Eaold);
      load.read("Eb",Ebold);

      // Project the orbitals
      arma::mat Ca=project_orbitals(Caold,oldbas,basis);
      arma::mat Cb=project_orbitals(Cbold,oldbas,basis);
      chkpt.write("Ca",Ca);
      chkpt.write("Cb",Cb);

      // and generate dummy energies
      arma::vec Ea(Ca.n_cols);
      arma::vec Eb(Cb.n_cols);
      Ea.subvec(0,Eaold.n_elem-1)=Eaold;
      Eb.subvec(0,Eaold.n_elem-1)=Ebold;
      for(size_t i=Eaold.n_elem;i<Ea.n_elem;i++)
	Ea(i)=Eaold(Eaold.n_elem-1);
      for(size_t i=Ebold.n_elem;i<Eb.n_elem;i++)
	Eb(i)=Ebold(Ebold.n_elem-1);

      chkpt.write("Ea",Ea);
      chkpt.write("Eb",Eb);
    }
  }

  // Delete the temporary file
  remove(tempname.c_str());

  fprintf(stderr,"Projection of molecular guess took %s.\n",t.elapsed().c_str());
  fflush(stderr);
  printf("Projection of molecular guess took %s.\n",t.elapsed().c_str());
  fflush(stdout);
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
      // Check charge status
      if(basis.get_nucleus(i).Q != basis.get_nucleus(ret[j][0]).Q)
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
	  for(size_t ic=0;ic<lhc.size();ic++) {
	    if(!(lhc[ic]==rhc[ic])) {
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
