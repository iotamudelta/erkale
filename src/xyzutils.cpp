/*
 *                This source code is part of
 * 
 *                     E  R  K  A  L  E
 *                             -
 *                       DFT from Hel
 *
 * Written by Jussi Lehtola, 2010-2011
 * Copyright (c) 2010-2011, Jussi Lehtola
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */



#include "global.h"
#include "xyzutils.h"
#include "stringutil.h"

#include <sstream>
#include <stdexcept>

#include <fstream>
#include <cstdio>


std::vector<atom_t> load_xyz(std::string filename) {

  // Input file
  std::ifstream in(filename.c_str());
  // Returned array
  std::vector<atom_t> atoms;

  if(in.good()) {
    // OK, file was succesfully opened.
    
    // Read the first line to get the number of atoms
    std::string line=readline(in);
    std::vector<std::string> words=splitline(line);
    int Nat=readint(words[0]);
    
    // Reserve enough memory.
    atoms.reserve(Nat);
    
    // The next line contains the comment, skip it.
    line=readline(in);
    
    // Now, proceed with reading in the atoms.
    for(int i=0;i<Nat;i++) {
      // Helper structure
      atom_t tmp;
      
      if(!in.good()) {
	ERROR_INFO();
	throw std::domain_error("XYZ file ended unexpectedly!\n");
      }

      // Get line containing the input
      line=readline(in);
      // and split it to words
      words=splitline(line);
      
      if(!words.size()) {
	ERROR_INFO();
	throw std::domain_error("XYZ file ended unexpectedly!\n");
      }
      
      // and extract the information
      tmp.el=words[0]; // Element type
      tmp.x=readdouble(words[1])*ANGSTROMINBOHR;
      tmp.y=readdouble(words[2])*ANGSTROMINBOHR;
      tmp.z=readdouble(words[3])*ANGSTROMINBOHR;
      // and add the atom to the list.
      atoms.push_back(tmp);
    }
  } else {
    ERROR_INFO();
    throw std::runtime_error("Could not open xyz file!\n");
  }

  if(atoms.size()==0) {
    ERROR_INFO();
    throw std::domain_error("No atoms found in xyz file!\n");
  }

  return atoms;
}

void save_xyz(const std::vector<atom_t> & at, const std::string & comment, const std::string & fname) {
  // Output file
  FILE *out;
  
  out=fopen(fname.c_str(),"w");

  // Print out number of atoms
  fprintf(out,"%lu\n",at.size());
  // Print comment
  fprintf(out,"%s\n",comment.c_str());
  // Print atoms
  for(size_t i=0;i<at.size();i++)
    fprintf(out,"%s\t%g\t%g\t%g\n",at[i].el.c_str(),at[i].x,at[i].y,at[i].z);
  fclose(out);
}