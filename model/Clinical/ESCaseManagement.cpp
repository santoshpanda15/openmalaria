/*
 This file is part of OpenMalaria.
 
 Copyright (C) 2005-2009 Swiss Tropical Institute and Liverpool School Of Tropical Medicine
 
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

#include "Clinical/ESCaseManagement.h"
#include "inputData.h"
#include "util/gsl.h"

using namespace OM::util::errors;

namespace Clinical {

ESCaseManagement::TreeType ESCaseManagement::cmTree;
cmid ESCaseManagement::cmMask;

// -----  Static  -----

void ESCaseManagement::init () {
    const scnXml::CaseManagementTree& xmlCM = getEventScheduler().getCaseManagementTree();
    for (scnXml::CaseManagementTree::CM_pBranchSetConstIterator it = xmlCM.getCM_pBranchSet().begin(); it != xmlCM.getCM_pBranchSet().end(); ++it) {
	cmTree[it->getID ()] = new CMPBranchSet (it->getCM_pBranch());
    }
    for (scnXml::CaseManagementTree::CM_leafConstIterator it = xmlCM.getCM_leaf().begin(); it != xmlCM.getCM_leaf().end(); ++it) {
	cmTree[it->getID ()] = new CMLeaf (CaseTreatment (it->getMedicate()));
    }
    
    cmMask = xmlCM.getMask();
}


// -----  Non-static  -----

cmid ESCaseManagement::execute (list<MedicateData>& medicateQueue, Pathogenesis::State pgState, WithinHostModel& withinHostModel, double ageYears, SurveyAgeGroup ageGroup) {
#ifndef NDEBUG
    if (! (pgState & Pathogenesis::SICK))
        throw domain_error ("doCaseManagement shouldn't be called if not sick");
#endif

    // We always remove any queued medications.
    medicateQueue.clear();
    
    //FIXME: static check that Pathogenesis::MORBIDITY_MASK and Decision::MORBIDITY_MASK are equal
    cmid decisionID = pgState & Pathogenesis::MORBIDITY_MASK;;
    if (ageYears > 5.0)
	decisionID |= Decision::AGE_OVER5;

    /*FIXME: report treatment
    const CaseTypeEndPoints* endPoints;
    if (pgState & Pathogenesis::MALARIA) { // NOTE: report treatment shouldn't be done like this so it's handled correctly when treatment is cancelled
        if (pgState & Pathogenesis::COMPLICATED) {
            endPoints = &caseManagementEndPoints[ageIndex].caseSev;
            Surveys.current->reportTreatments3 (ageGroup, 1);
        } else if (pgState & Pathogenesis::SECOND_CASE) {
            endPoints = &caseManagementEndPoints[ageIndex].caseUC2;
            Surveys.current->reportTreatments2 (ageGroup, 1);
        } else {
            endPoints = &caseManagementEndPoints[ageIndex].caseUC1;
            Surveys.current->reportTreatments1 (ageGroup, 1);
        }
    } else / *if (pgState & Pathogenesis::SICK) [true by above check]* / { // sick but not from malaria
        if (withinHostModel.parasiteDensityDetectible())
            endPoints = &caseManagementEndPoints[ageIndex].caseNMFWithParasites;
        else
            endPoints = &caseManagementEndPoints[ageIndex].caseNMFWithoutParasites;
    }*/
    
    pair<cmid,CaseTreatment&> leaf = traverse (decisionID);
    leaf.second.apply (medicateQueue, leaf.first);
    return leaf.first;
}

pair<cmid,CaseTreatment&> ESCaseManagement::traverse (cmid id) {
    // Parasite test:
    //NOTE: currently only one test, but only used in UC trees
    // (can make test depend on severe/UC state)
    if ((id & Decision::RESULT_MASK) == Decision::RESULT_DETERMINE) {
	id = id & (~Decision::RESULT_MASK);	// remove result flags
	
	//FIXME: or RESULT_NEGATIVE
	id |= Decision::RESULT_POSITIVE;
	
	return traverse(id);
    }
    
    TreeType::const_iterator it = cmTree.find (id & cmMask);
    if (it == cmTree.end()) {
	ostringstream msg;
	msg << "No node for id "<<(id&cmMask)<<" (unmasked: "<<id<<")";
	throw xml_scenario_error (msg.str());
    }
    return it->second->traverse (id);
}


// -----  CMNode derivatives  -----

ESCaseManagement::CMPBranchSet::CMPBranchSet (const scnXml::CM_pBranchSet::CM_pBranchSequence& branchSeq) {
    double pAccumulation = 0.0;
    branches.resize (branchSeq.size());
    for (size_t i = 0; i < branchSeq.size(); ++i) {
	branches[i].outcome = branchSeq[i].getOutcome ();
	pAccumulation += branchSeq[i].getP ();
	branches[i].cumP = pAccumulation;
    }
    // Test cumP is approx. 1.0 (in case the XML is wrong).
    if (pAccumulation < 0.999 || pAccumulation > 1.001)
	throw xml_scenario_error ("EndPoint probabilities don't add up to 1.0 (CaseManagementTree)");
    // In any case, force it exactly 1.0 (because it could be slightly less,
    // meaning a random number x could have cumP<x<1.0, causing index errors.
    branches[branchSeq.size()-1].cumP = 1.0;
}

pair<cmid,CaseTreatment&> ESCaseManagement::CMPBranchSet::traverse (cmid id) {
    double randCum = gsl::rngUniform();
    size_t i = 0;
    while (branches[i].cumP < randCum) {
	++i;
	assert (i < branches.size());
    }
    id |= branches[i].outcome;
    
    return ESCaseManagement::traverse (id);
}

pair<cmid,CaseTreatment&> ESCaseManagement::CMLeaf::traverse (cmid id) {
    return pair<cmid,CaseTreatment&> (id, ct);
}

}