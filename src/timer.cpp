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



#include <cmath>
#include <cstdio>
#include <string>
#include <sstream>

#include "timer.h"

Timer::Timer() {
  // Get time.
  time(&start);
  gettimeofday(&tstart,NULL);

  elapsd=0.0;
}

Timer::~Timer() {
}

void Timer::stop() {
  time_t stop;
  struct timeval tstop;

  time(&stop);
  gettimeofday(&tstop,NULL);

  elapsd+=(tstop.tv_sec-tstart.tv_sec)+(tstop.tv_usec-tstart.tv_usec)/1000000.0;
}

void Timer::cont() {
  time(&start);
  gettimeofday(&tstart,NULL);
}

void Timer::set() {
  time(&start);
  gettimeofday(&tstart,NULL);
  elapsd=0.0;
}

void Timer::print() const {
  printf("Time elapsed is %s.\n",elapsed().c_str());
}

double Timer::get() const {
  time_t stop;
  struct timeval tstop;

  time(&stop);
  gettimeofday(&tstop,NULL);
  //  return difftime(stop,start);
  return elapsd+(tstop.tv_sec-tstart.tv_sec)+(tstop.tv_usec-tstart.tv_usec)/1000000.0;
}

std::string Timer::elapsed() const {
  std::ostringstream ret;

  time_t stop;
  struct timeval tstop;

  time(&stop);
  gettimeofday(&tstop,NULL);

  int isecs=tstop.tv_sec-tstart.tv_sec;

  // Minute is 60 sec
  int min=60;
  // Hour is 60 minutes
  int hour=60*min;
  // Day is 24 hours
  int day=24*hour;

  // Compute number of days
  int days=(int) trunc(isecs/day);
  if(days) {
    isecs-=days*day;

    ret << days << " d";
  }
  
  // Compute number of hours
  int hours=(int) trunc(isecs/hour);
  if(hours) {
    isecs-=hours*hour;

    // Check that there is a space at the end
    std::string tmp=ret.str();
    if(tmp.size() && tmp[tmp.size()-1]!=' ')
      ret << " ";
    
    ret << hours << " h";
  }

  // Compute number of minutes
  int mins=(int) trunc(isecs/min);
  if(mins) {
    isecs-=mins*min;

    // Check that there is a space at the end
    std::string tmp=ret.str();
    if(tmp.size() && tmp[tmp.size()-1]!=' ')
      ret << " ";
    
    ret << mins << " min";
  }

  // Compute number of seconds
  double secs=isecs+(tstop.tv_usec-tstart.tv_usec)/1000000.0;
  
  // Check that there is a space at the end
  std::string tmp=ret.str();
  if(tmp.size() && tmp[tmp.size()-1]!=' ')
    ret << " ";

  char hlp[80];
  sprintf(hlp,"%.2f s",secs);
  ret << hlp;
  
  return ret.str();
}