/*

 This file is part of OpenMalaria.
 
 Copyright (C) 2005,2006,2007,2008 Swiss Tropical Institute and Liverpool School Of Tropical Medicine
 
 OpenMalaria is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or (at
 your option) any later version.
 
 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/
//This is needed to get symbols like M_PI with MSVC:
#define _USE_MATH_DEFINES

#include "Transmission/TransmissionModel.h"
#include "global.h" 
#include "intervention.h" 
#include "inputData.h"
#include "Transmission/NonVector.h"
#include "Transmission/Vector/VectorTransmission.h"
#include "Transmission/PerHostTransmission.h"
#include "simulation.h"
#include "summary.h"
#include "util/BoincWrapper.h"
#include <math.h> 
#include <cfloat>
#include <gsl/gsl_vector.h> 


TransmissionModel* TransmissionModel::createTransmissionModel () {
  // EntoData contains either a list of at least one anopheles or a list of at
  // least one EIRDaily.
  const scnXml::EntoData::VectorOptional& vectorData = getEntoData().getVector();
  if (vectorData.present())
    return new VectorTransmission(vectorData.get());
  else {
    const scnXml::EntoData::NonVectorOptional& nonVectorData = getEntoData().getNonVector();
    if (!nonVectorData.present())	// should be a validation error, but anyway...
      throw xml_scenario_error ("Neither vector nor non-vector data present in the XML!");
    return new NonVectorTransmission(nonVectorData.get());
  }
}

TransmissionModel::TransmissionModel() :
    simulationMode(equilibriumMode), _sumAnnualKappa(0.0), annualEIR(0.0), timeStepNumEntoInnocs (0)
#ifdef OMV_CSV_REPORTING
  , csvReporting ("vector.csv", ios::app)
#endif
{
  kappa.resize (Global::intervalsPerYear, 0.0);
  initialisationEIR.resize (Global::intervalsPerYear);
  innoculationsPerAgeGroup.resize (Simulation::gMainSummary->getNumOfAgeGroups(), 0.0);
  innoculationsPerDayOfYear.resize (Global::intervalsPerYear, 0.0);
  timeStepEntoInnocs.resize (innoculationsPerAgeGroup.size(), 0.0);
  
  noOfAgeGroupsSharedMem = std::max(Simulation::gMainSummary->getNumOfAgeGroups(),KappaArraySize);
}

TransmissionModel::~TransmissionModel () {
#ifdef OMV_CSV_REPORTING
  csvReporting.close();
#endif
}

void TransmissionModel::updateKappa (const std::list<Human>& population, int simulationTime) {
  // We calculate kappa for output and non-vector model, and kappaByAge for
  // the shared graphics.
  
  double sumWt_kappa= 0.0;
  double sumWeight  = 0.0;
  kappaByAge.assign (noOfAgeGroupsSharedMem, 0.0);
  nByAge.assign (noOfAgeGroupsSharedMem, 0);
  
  for (std::list<Human>::const_iterator h = population.begin(); h != population.end(); ++h) {
    double ageYears = h->getAgeInYears();
    double t = h->perHostTransmission.relativeAvailabilityHetAge(ageYears) * PerHostTransmission::ageCorrectionFactor;
    sumWeight += t;
    t *= h->probTransmissionToMosquito();
    sumWt_kappa += t;
    
    // kappaByAge and nByAge are used in the screensaver only
    int ia = h->ageGroup();
    kappaByAge[ia] += t;
    ++nByAge[ia];
  }
  
#ifndef NDEBUG
  if (sumWeight < DBL_MIN * 4.0)	// if approx. eq. 0 or negative
    throw range_error ("sumWeight is invalid");
#endif
  
  size_t tmod = (simulationTime-1) % Global::intervalsPerYear;
  kappa[tmod] = sumWt_kappa / sumWeight;
  
  //Calculate time-weighted average of kappa
  _sumAnnualKappa += kappa[tmod] * initialisationEIR[tmod];
  if (tmod == Global::intervalsPerYear - 1) {
    if (annualEIR == 0.0) {
      _annualAverageKappa=0;
      cerr << "aE.eq.0" << endl;
    } else {
      _annualAverageKappa = _sumAnnualKappa / annualEIR;
    }
    _sumAnnualKappa = 0.0;
  }
  
  double timeStepTotal = 0.0;
  for (size_t group = 0; group < timeStepEntoInnocs.size(); ++group) {
    timeStepTotal += timeStepEntoInnocs[group];
    innoculationsPerAgeGroup[group] += timeStepEntoInnocs[group];
    // Reset to zero:
    timeStepEntoInnocs[group] = 0.0;
  }
  innoculationsPerDayOfYear[tmod] = timeStepTotal / timeStepNumEntoInnocs;
  timeStepNumEntoInnocs = 0;
  
  // Shared graphics: report infectiousness
  if (Simulation::simulationTime % 6 ==  0) {
    for (int i = 0; i < Simulation::gMainSummary->getNumOfAgeGroups(); i++)
      kappaByAge[i] /= nByAge[i];
    SharedGraphics::copyKappa(&kappaByAge[0]);
  }
  
#ifdef OMV_CSV_REPORTING
  csvReporting << initialisationEIR[simulationTime%Global::intervalsPerYear]
	       << ',' << innoculationsPerDayOfYear[tmod]
	       << ',' << kappa[tmod]
	       << ',' << PerHostTransmission::ageCorrectionFactor
	       << ','<< endl;
#endif
}

double TransmissionModel::getEIR (int simulationTime, PerHostTransmission& host, double ageInYears) {
  /* For the NonVector model, the EIR should just be multiplied by the
   * availability. For the Vector model, the availability is also required
   * for internal calculations, but again the EIR should be multiplied by the
   * availability. */
  double EIR = calculateEIR (simulationTime, host, ageInYears);
  
  int ageGroup = Simulation::gMainSummary->ageGroup (ageInYears);
  timeStepEntoInnocs[ageGroup] += EIR;
  timeStepNumEntoInnocs ++;
  return EIR;
}

void TransmissionModel::summarize (Summary& summary) {
  summary.setNumTransmittingHosts(kappa[(Simulation::simulationTime-1) % Global::intervalsPerYear]);
  summary.setAnnualAverageKappa(_annualAverageKappa);
  
  summary.setInnoculationsPerDayOfYear (innoculationsPerDayOfYear);
  summary.setKappaPerDayOfYear (kappa);
  
  summary.setInnoculationsPerAgeGroup (innoculationsPerAgeGroup);	// Array contents must be copied.
  innoculationsPerAgeGroup.assign (innoculationsPerAgeGroup.size(), 0.0);
}

void TransmissionModel::intervLarviciding (const scnXml::Larviciding&) {
  throw xml_scenario_error ("larviciding when not using a Vector model");
}


// -----  checkpointing  -----

void TransmissionModel::write(ostream& out) const {
  out << simulationMode << endl;
  for (size_t i = 0; i < Global::intervalsPerYear; ++i)
    out << kappa[i] << endl;
  out << _annualAverageKappa << endl;
  out << _sumAnnualKappa << endl;
  out << annualEIR << endl;
  for (size_t i = 0; i < Global::intervalsPerYear; ++i)
    out << innoculationsPerDayOfYear[i] << endl;
  out << innoculationsPerAgeGroup.size() << endl;
  for (size_t i = 0; i < innoculationsPerAgeGroup.size(); ++i)
    out << innoculationsPerAgeGroup[i] << endl;
  writeV (out);
}
void TransmissionModel::read(istream& in) {
  in >> simulationMode;
  for (size_t i = 0; i < Global::intervalsPerYear; ++i)
    in >> kappa[i];
  in >> _annualAverageKappa;
  in >> _sumAnnualKappa;
  in >> annualEIR;
  for (size_t i = 0; i < Global::intervalsPerYear; ++i)
    in >> innoculationsPerDayOfYear[i];
  size_t size;
  in >> size;
  if (size != innoculationsPerAgeGroup.size())
    throw checkpoint_error ("innoculationsPerAgeGroup has wrong size");
  for (size_t i = 0; i < innoculationsPerAgeGroup.size(); ++i)
    in >> innoculationsPerAgeGroup[i];
  readV (in);
}
