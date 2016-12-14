/*----------------------------------------------------------------
  Raven Library Source Code
  Copyright � 2008-2014 the Raven Development Team
----------------------------------------------------------------*/
#ifndef TRANSPORTMODEL_H
#define TRANSPORTMODEL_H

#include "RavenInclude.h"
#include "Model.h"

struct constituent
{
  string   name;          ///< constituent name (e.g., "Nitrogen")

  bool     is_tracer;     ///< true if tracer (actually unitless)
  bool     can_evaporate; ///< true if constituent can be trasported through evaporation

  double   decay_rate;    ///< linear decay rate of constituent [1/d]
  
  double   cumul_input;   ///< cumulative mass lost from system [mg]
  double   cumul_output;  ///< cumulative mass lost from system [mg]
  double   initial_mass;  ///< initial mass in system [mg]

  ofstream OUTPUT;        ///< for concentrations.csv 
  ofstream POLLUT;        ///< for pollutograph.csv

};
struct constit_source
{
  bool dirichlet;         ///< =true for dirichlet, false for neumann
  int constit_index;      ///< constituent index (c)
  int i_stor;             ///< index of water storage compartment
  int kk;                 ///< index of HRU group to which source is applied (default is all for DOESNT_EXIST)
  double concentration;   ///< fixed concentration [mg/m2] (or DOESNT_EXIST=-1 if time series should be used)
  double flux;            ///< fixed flux [mg/m2/d] (or DOESNT_EXIST=-1 if time series should be used)
  const CTimeSeries *pTS; ///< time series of fixed concentration or flux (or NULL if fixed should be used)
};
struct transport_params
{
  double decay_coeff;                          ///< constituent linear decay coefficient [1/d]
  double retardation     [MAX_SOIL_CLASSES+1]; ///< constituent retardation factors (one per soil) [-]
  //double uptake_moderator[MAX_VEG_CLASSES +1]; ///< constitutent uptake moderators for transpiration (one per vegetation class) [-]
  //double transf_coeff    [MAX_SPECIES     +1]; ///< linear transformation coefficients (one per species) [1/d]   
};
///////////////////////////////////////////////////////////////////
/// \brief Class for coordinating transport simulation
/// \details Implemented in Transport.cpp
//
class CModel;
class CTransportModel
{  
  private:/*------------------------------------------------------*/
    CModel *pModel;                  ///< pointer to model object
    static CTransportModel *pTransModel;    ///< pointer to only transport model (needed for access to transport model through static functions)

    int  nAdvConnections;              ///< number of advective/dispersive transport connections between water storage units
    int *iFromWater;                   ///< state variable indices of water compartment source [size: nAdvConnections]
    int *iToWater;                     ///< state variable indices of water compartment destination [size: nAdvConnections]
    int *js_indices;                   ///< process index (j*) of connection [size: nAdvConnections] 

    int  nWaterCompartments;           ///< number of water storage compartments which may contain constituent
    int *iWaterStorage;                ///< state variable indices of water storage compartments which may contain constituent [size: nWaterCompartments]

    int *aIndexMapping;                ///< lookup table to convert state variable index i to local water storage index [size: pModel::_nStateVars prior to transport variables being included]
                                       ///< basically inverse of iWaterStorage

    int                nConstituents;  ///< number of transported constituents [c=0..nConstituents-1]
    constituent      **pConstituents;  ///< array of pointers to constituent structures [size: nConstituents]
    transport_params **pConstitParams; ///< array of pointers to constituent parameters [size: nConstituents]

    double ***aMinHist;                ///< array used for storing routing upstream loading history [mg/d] [size: nSubBasins x nConstituents x nMinhist(p)]
    double ***aMlatHist;               ///< array used for storing routing lateral loading history [mg/d] [size: nSubBasins x nConstituents x nMlathist(p)]
    double ***aMout;                   ///< array used for storing routing channel loading history [mg/d] [size: nSubBasins x nConstituents x _nSegments(p)] 

    constit_source **pSources;         ///< array of pointers to constituent sources [size: nSources]
    int              nSources;         ///< number of constituent sources
    int            **aSourceIndices;   ///> lookup table to convert constitutent and (global) water storage index to corresponding source, if any [size: nConstituents x nStateVariables]

    void m_to_cj(const int layerindex, int &c, int &j) const;
    void DeleteRoutingVars();
    void InitializeConstitParams(transport_params *P);

  public:/*-------------------------------------------------------*/
    CTransportModel(CModel *pMod);
    ~CTransportModel();

    //Accessors
    static int    GetLayerIndexFromName(const string name,const int comp_m);
    int          GetLayerIndexFromName2(const string name,const int comp_m) const;     //non-static version
    
    static string GetConstituentTypeName (const int m); //e.g., "Nitrogen" (m=layer index)
    static string GetConstituentTypeName2(const int c); //e.g., !Nitrogen (c=constituent index)
    static string GetConstituentLongName (const int layerindex);  //e.g., "Nitrogen in Soil Water[2]"
    string GetConstituentName     (const int layerindex) const; //e.g., "Nitrogen in Soil Water[2]"
    string GetConstituentShortName(const int layerindex) const; //e.g., "!Nitrogen_SOIL[2]"

    int    GetNumConstituents() const;
    const constituent      *GetConstituent(const int c) const;
    const transport_params *GetConstituentParams(const int c) const;
    int    GetConstituentIndex(const string name) const;
    
    int    GetNumWaterCompartments() const;
    int    GetNumAdvConnections() const;

    int    GetFromIndex(const int c,const int q) const;
    int    GetToIndex  (const int c,const int q) const;
    int    GetStorIndex(const int c,const int ii) const;

    int    GetFromWaterIndex(const int q) const;
    int    GetToWaterIndex  (const int q) const;
    int    GetJsIndex       (const int q) const;
    int    GetStorWaterIndex(const int ii) const;
    int    GetWaterStorIndexFromLayer(const int m) const;

    int    GetLayerIndex(const int c, const int i_stor) const;

    double GetOutflowConcentration(const int p, const int c) const;
    double GetIntegratedMassOutflow(const int p, const int c) const;

    double GetDecayCoefficient (const int c,const CHydroUnit *pHRU, const int iStorWater) const;
    double GetRetardationFactor(const int c,const CHydroUnit *pHRU, const int iFromWater,const int iToWater) const;

    bool   IsDirichlet(const int i_stor, const int c, const int k, const time_struct tt, double &Cs) const;
    double GetSpecifiedMassFlux(const int i_stor, const int c, const int k, const time_struct tt) const;

    //Manipulators
    void   AddConstituent(string name, bool is_tracer);
    void   AddDirichletCompartment(const string const_name, const int i_stor, const int kk, const double Cs);
    void   AddDirichletTimeSeries (const string const_name, const int i_stor, const int kk, const CTimeSeries *pTS);
    void   AddInfluxSource        (const string const_name, const int i_stor, const int kk, const double flux); 
    void   AddInfluxTimeSeries    (const string const_name, const int i_stor, const int kk, const CTimeSeries *pTS);
    //
    void   Prepare(const optStruct &Options);
    void   Initialize();
    void   InitializeRoutingVars();

    void   IncrementCumulInput (const optStruct &Options, const time_struct &tt);
    void   IncrementCumulOutput(const optStruct &Options);

    void   SetMassInflows    (const int p, const double *aMinnew);
    void   SetLateralInfluxes(const int p, const double *aRoutedMass);
    void   RouteMass         (const int p,       double **aMoutnew, const optStruct &Options) const;
    void   UpdateMassOutflows(const int p,       double **aMoutnew,double dummy_var,const optStruct &Options,bool initialize);

    void   WriteOutputFileHeaders     (const optStruct &Options) const;
    void   WriteMinorOutput           (const optStruct &Options, const time_struct &tt) const;
    void   WriteEnsimOutputFileHeaders(const optStruct &Options) const;
    void   WriteEnsimMinorOutput      (const optStruct &Options, const time_struct &tt) const;
    void   CloseOutputFiles           () const;
};
#endif