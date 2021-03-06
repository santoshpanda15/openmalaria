/*
 This file is part of OpenMalaria.
 
 Copyright (C) 2005-2015 Swiss Tropical and Public Health Institute
 Copyright (C) 2005-2014 Liverpool School Of Tropical Medicine
 
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
// Unittest for the LSTM drug model

#ifndef Hmod_PkPdComplianceSuite
#define Hmod_PkPdComplianceSuite

#include <cxxtest/TestSuite.h>
#include <boost/format.hpp>
#include "PkPd/LSTMModel.h"
#include "UnittestUtil.h"
#include "ExtraAsserts.h"
#include <limits>

using std::pair;
using std::multimap;
using boost::format;
using namespace OM;
using namespace OM::PkPd;

// Use one of these to switch verbosity on/off:
#define PCS_VERBOSE( x )
//#define PCS_VERBOSE( x ) x

/** Test outcomes from the PK/PD code in OpenMalaria with LSTM's external
 * model. Numbers should agree (up to rounding errors of around 5e-3). */
class PkPdComplianceSuite : public CxxTest::TestSuite
{
public:
    PkPdComplianceSuite() :
            proxy(0)
    {
        genotype = 0;                // 0 should work; we definitely don't want random allocation
        bodymass = 50;
       
        PCS_VERBOSE(cout << "\n[ Unittest Output Legend: \033[35mDrug "
                "Factor\033[0m, \033[36mDrug Concentration\033[0m ]" << endl;)
    }
    
    void setUp () {
        UnittestUtil::initTime(1);
        UnittestUtil::PkPdSuiteSetup();
        proxy = new LSTMModel ();
        schedule.clear();
    }
    
    void tearDown () {
        delete proxy;
        LSTMDrugType::clear();
    }
    
    void assembleTripleDosageSchedule ( double dosage ){
        schedule.clear();
        schedule.insert(make_pair(0, make_pair(0, dosage)));
        schedule.insert(make_pair(1, make_pair(0, dosage)));
        schedule.insert(make_pair(2, make_pair(0, dosage)));
    }
    void assembleHexDosageSchedule ( double dosage ){
        schedule.clear();
        schedule.insert(make_pair(0, make_pair(0, dosage)));
        schedule.insert(make_pair(0, make_pair(0.5, dosage)));
        schedule.insert(make_pair(1, make_pair(0, dosage)));
        schedule.insert(make_pair(1, make_pair(0.5, dosage)));
        schedule.insert(make_pair(2, make_pair(0, dosage)));
        schedule.insert(make_pair(2, make_pair(0.5, dosage)));
    }
    
    void assembleCQDosageSchedule ( double dosage ){
        // only used for CQ, which needs 10, 10, 5 dosages instead of constant dosages
        schedule.clear();
        schedule.insert(make_pair(0, make_pair(0, dosage)));
        schedule.insert(make_pair(1, make_pair(0, dosage)));
        schedule.insert(make_pair(2, make_pair(0, dosage/2)));
    }
    
    struct debugDrugSimulations {
        double factor, f_error, f_rel_error;
    };
    
    string drugDebugOutputHeader(bool secondDrug, string drugName){
        /*
        *   Verbose output is formatted in markdown, so it can be used in a github-wiki.
        */
        string white = "\033[0m";
        string yellow = "\033[33m";
        string green = "\033[32m"; // TODO check if really green
        string red = "\033[31m";
        // title
        if ( secondDrug ){
            cout << drugName << endl << "----" << endl;
        }
        string fm = white + "|%1$=3d|"
            + yellow + "%|14t|%2$-12d|" + red + "%|32t|%3$+-12d|" + red + "%|50t|%4$+-12d|"
            + green  + "%|68t|%5$-12d|" + red + "%|86t|%6$+-12d|" + red + "%|102t|%7$+-12d|";
        if ( secondDrug ) {
            fm += white + "%|120t|%8$=4s|";
        }
        fm += white + "\n";
        // head row
        if ( !secondDrug ){
            cout << format(fm) % "day" % "factor" % "f_abs_error" % "f_rel_err %" % "conc" % "c_abs_error" % "c_rel_err %";
            //fmter % day % factor % f_abs_error % f_rel_error % concentration % c_abs_error % c_rel_error;
        } else {
            cout << format(fm) % "day" % "factor" % "f_abs_error" % "f_rel_err %" % "conc" % "c_abs_error" % "c_rel_err %" % "type";
            //fmter % day % factor % f_abs_error % f_rel_error % concentration % c_abs_error % c_rel_error % type;
        }
        // head row separator
        string sfill = "------------";
        if ( !secondDrug ){
            cout << format(fm) % "---" % sfill % sfill % sfill % sfill % sfill % sfill;
        } else {
            cout << format(fm) % "---" % sfill % sfill % sfill % sfill % sfill % sfill % "----";
        }
        return fm;
    }
    
    void drugDebugOutputLine(bool secondDrug, size_t day, double factor, double f_abs_error, double f_rel_error, double concentration, double c_abs_error, double c_rel_error, string type, string fm) {
        
        if ( !secondDrug ){
            cout << format(fm) % day % factor % f_abs_error % f_rel_error % concentration % c_abs_error % c_rel_error;
        } else {
            cout << format(fm) % day % factor % f_abs_error % f_rel_error % concentration % c_abs_error % c_rel_error % type;
        }
    }
    
    void runDrugSimulations (string drugName, string drug2Name,
                          const double drug_conc[], const double drug2_conc[],
                          const double drug_factors[])
    {
        bool secondDrug = drug2_conc != 0;
        PCS_VERBOSE(
            cout << "\n\033[32mTesting \033[1m" << drugName ;
            if( secondDrug ) {
                cout << " - " << drug2Name << " Conversion";
            }
            cout <<  endl << "====\033[0m" << endl;
        )
        size_t drugIndex = LSTMDrugType::findDrug( drugName );
        size_t drug2Ind = secondDrug ? LSTMDrugType::findDrug( drug2Name ) : 0;
        const size_t maxDays = 6;
        PCS_VERBOSE(double res_Fac[maxDays];)
        double res_Conc[maxDays];
        double res_Conc2[maxDays];
        double totalFac = 1;
        for( size_t i = 0; i < maxDays; i++){
            // before update (after last step):
            double fac = proxy->getDrugFactor(genotype, bodymass);
            totalFac *= fac;
            TS_ASSERT_APPROX_TOL (totalFac, drug_factors[i], 5e-3, 1e-24);
            PCS_VERBOSE(res_Fac[i] = totalFac;)
            
            // update (two parts):
            UnittestUtil::incrTime(sim::oneDay());
            proxy->decayDrugs(bodymass);
            
            // after update:
            res_Conc[i] = proxy->getDrugConc(drugIndex);
            TS_ASSERT_APPROX_TOL (res_Conc[i], drug_conc[i], 5e-3, 1e-18);
            res_Conc2[i] = secondDrug ? proxy->getDrugConc(drug2Ind) : 0.0;
            if( secondDrug ) TS_ASSERT_APPROX_TOL (res_Conc2[i], drug2_conc[i], 5e-3, 1e-9);
            
            // medicate (take effect on next update):
            medicate( drugIndex, i );
        }
        PCS_VERBOSE( double c_abs_error = c_rel_error = f_abs_error = f_rel_error = c2_abs_error = c2_rel_error = 0.0; )
        PCS_VERBOSE(
            string fmt = drugDebugOutputHeader(secondDrug, drugName);
            for( size_t i = 0; i < maxDays; i++){
                // calculate relative and absolute differences to expected values

                f_abs_error = res_Fac[i] - drug_factors[i];
                f_rel_error = floor((res_Fac[i] / drug_factors[i] -1 )*1000000)/10000;
                c_abs_error = res_Conc[i] - drug_conc[i];
                c_rel_error = floor((res_Conc[i] / drug_conc[i] - 1 )*1000000)/10000;
                c2_abs_error = secondDrug ? res_Conc2[i] - drug2_conc[i] : 0.0;
                c2_rel_error = secondDrug ? floor((res_Conc2[i] / drug2_conc[i] - 1 )*1000000)/10000: 0.0;

                // (parent) drug debug
                drugDebugOutputLine(secondDrug, i, res_Fac[i], f_abs_error, f_rel_error, res_Conc[i], c_abs_error, c_rel_error, "P", fmt);

                // metabolite debug
                if( secondDrug ) {
                    drugDebugOutputLine(true, i, res_Fac[i], f_abs_error, f_rel_error, res_Conc2[i], c2_abs_error, c2_rel_error, "M", fmt);
                }
            }
        )

    }
    
    void runDrugSimulations (string drugName, const double drug_conc[],
                          const double drug_factors[])
    {
        runDrugSimulations(drugName, "", drug_conc, 0, drug_factors);
    }
    
    void medicate ( size_t drugIndex,  size_t i) {
            typedef multimap<size_t,pair<double, double> >::const_iterator iter;
            pair<iter, iter> doses_tmp = schedule.equal_range(i);
            for( iter it = doses_tmp.first; it != doses_tmp.second; it++){
                const double time = it->second.first, qty = it->second.second;
                UnittestUtil::medicate( *proxy, drugIndex, qty, time, bodymass );
            }
    }
    
    void testAR1 () { /* Artemether no conversion */
        const double dose = 1.7 * bodymass;   // 1.7 mg/kg * 50 kg
        assembleHexDosageSchedule(dose);
        const double drug_conc[] = { 0.0, 0.0153520114,
            0.0156446685, 0.01560247467, 0.000298342, 5.68734e-6 };
        const double drug_factors[] = { 1,1.0339328333924E-012, 1.06887289270302E-024,
            1.10329635519261E-036, 4.73064069783747E-042, 4.73064069783747E-042 };
        runDrugSimulations("AR1", drug_conc, drug_factors);
    }
    
    void testAR () { /* Artemether with conversion */
        const double dose = 1.7 * bodymass;   // 1.7 mg/kg * 50 kg
        assembleHexDosageSchedule(dose);
        const double AR_conc[] = { 0, 0.0001825231, 0.0001825242, 0.0001825242, 1.15E-09, 7.19E-15 };
        const double DHA_conc[] = { 0, 0.0002013126, 0.0002013139, 0.0002013139, 1.27E-09, 7.94E-15 };
        const double drug_factors[] = { 1, 1.695240e-07, 2.838147e-14, 4.740015e-21, 4.751478e-21, 4.751478e-21 };
        runDrugSimulations("AR", "DHA_AR", AR_conc, DHA_conc, drug_factors);
    }
    
    void testAS1 () { /* Artesunate no conversion */
        const double dose = 4 * bodymass;   // 4 mg/kg * 50 kg
        assembleTripleDosageSchedule(dose);
        const double drug_conc[] = { 0, 8.98E-008, 8.98E-008, 8.98E-008, 5.55E-015, 3.43E-022 };
        const double drug_factors[] = { 1, 0.000012, 1.45E-010, 1.75E-015,  1.75E-015, 1.75E-015 };
        runDrugSimulations("AS1", drug_conc, drug_factors);
    }
    
    void testAS () { /* Artesunate with conversion */
        const double dose = 4 * bodymass;   // 4 mg/kg * 50 kg
        assembleTripleDosageSchedule(dose);
        const double AS_conc[] = { 0, 2.30E-14, 2.30E-14, 2.30E-14, 8.25E-28, 2.95E-41 };
        const double DHA_conc[] = { 0, 1.14E-10, 1.14E-10, 1.14E-10, 1.07E-21, 9.94E-33 };
        const double drug_factors[] = { 1, 5.322908e-04, 2.833335e-07, 1.508160e-10, 1.508160e-10, 1.508160e-10 };
        runDrugSimulations("AS", "DHA_AS", AS_conc, DHA_conc, drug_factors);
    }
    
    void testCQ () {
        const double dose = 10 * bodymass;
        assembleCQDosageSchedule(dose);
        const double drug_conc[] = { 0.0, 0.03257216, 0.06440052, 0.07921600, 0.07740709, 0.07563948 };
        const double drug_factors[] = { 1, 9.259311e-02, 4.623815e-03, 2.057661e-04, 9.262133e-06, 4.218529e-07 };
        runDrugSimulations("CQ", drug_conc, drug_factors);
    }
    
    void testDHA () {
        const double dose = 4 * bodymass;   // 4 mg/kg * 50 kg
        assembleTripleDosageSchedule(dose);
        const double drug_conc[] = { 0, 6.76E-009, 6.76E-009, 6.76E-009, 1.7E-017, 4.28E-026 };
        const double drug_factors[] = { 1, 0.000355234, 0.000000126, 4.48E-011, 4.48E-011, 4.48E-011 };
        runDrugSimulations("DHA", drug_conc, drug_factors);
    }
    
    void testLF () {
        const double dose = 12 * bodymass;   // 12 mg/kg * 50 kg
        assembleHexDosageSchedule(dose);
        const double drug_conc[] = { 0, 1.014434363, 1.878878305, 2.615508841, 2.228789614, 1.899249226 };
        const double drug_factors[] = { 1, 0.031746317, 0.001007809, 0.000032, 0.00000102, 3.22E-008 };
        runDrugSimulations("LF", drug_conc, drug_factors);
    }
    
    void testMQ () {
        const double dose = 8.3 * bodymass;   // 8.3 mg/kg * 50 kg
        assembleTripleDosageSchedule( dose) ;
        const double drug_conc[] = { 0, 0.378440101, 0.737345129, 1.077723484,
                1.022091411, 0.969331065 };
        const double drug_factors[] = { 1, 0.031745814, 0.001007791, 0.000032,
                1.02e-6, 3.22E-008 };
        runDrugSimulations("MQ", drug_conc, drug_factors);
    }
    
    // PPQ with a 1-compartment model (not preferred)
    void testPPQ1C (){
        const double dose = 18 * bodymass;   // 18 mg/kg * 50 kg
        assembleTripleDosageSchedule( dose );
        const double drug_conc[] = { 0, 0.116453464, 0.2294652081, 0.339137, 0.3291139387, 0.3193871518 };
        const double drug_factors[] = { 1, 0.0524514512, 0.0016818644,
                5.344311603535E-005, 1.69855029226935E-006, 0.000000054 };
        runDrugSimulations("PPQ", drug_conc, drug_factors);
    }
    
    // PPQ with a 2-compartment model (Hodel2013)
    void testPPQ_Hodel2013 (){
        const double dose = 18 * bodymass;   // 18 mg/kg * 50 kg
        assembleTripleDosageSchedule( dose );
        const double drug_conc[] = { 0, 0.0724459062, 0.1218019809,
            0.1561173647, 0.1081632036, 0.0768569742 };
        const double drug_factors[] = { 1, 0.034225947, 0.001086594,
            0.0000345, 0.0000011, 3.48E-008 };
        runDrugSimulations("PPQ2", drug_conc, drug_factors);
    }
    
    // PPQ with a 3-compartment model (Tarning 2012 AAC)
    void testPPQ_Tarning2012AAC (){
        const double dose = 18 * bodymass;   // 18 mg/kg * 50 kg
        assembleTripleDosageSchedule( dose );
        const double drug_conc[] = { 0, 0.0768788483, 0.1201694285,
            0.1526774077, 0.1016986483, 0.0798269206 };
        const double drug_factors[] = { 1, 0.0342175609, 0.0010863068,
            3.44853898048478E-005, 1.09489352156011E-006, 3.47830222985575E-008 };
        runDrugSimulations("PPQ3", drug_conc, drug_factors);
    }
    
private:
    LSTMModel *proxy;
    uint32_t genotype;
    double bodymass;
    std::multimap<size_t, pair<double, double> > schedule; // < day, pair < part of day, dosage > >
};

#endif
