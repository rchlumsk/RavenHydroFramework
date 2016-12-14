/*----------------------------------------------------------------
  Raven Library Source Code
  Copyright � 2008-2014 the Raven Development Team
----------------------------------------------------------------*/
#include "Model.h"
#include "Radiation.h"
#include "Properties.h"
#include "Forcings.h"

double EstimateRelativeHumidity(const relhum_method method, 
                                const force_struct &F);
double EstimateWindVelocity    (const windvel_method method,
                                const force_struct &F);
double EstimateAirPressure     (const airpress_method method,
                                const force_struct &F,
                                const double       &elev);

//////////////////////////////////////////////////////////////////
/// \brief Updates HRU forcing functions
/// \details Interpolate meteorological information from Gauge stations and 
///  assign to each HRU
///  Additionally estimates missing forcings from avaialable data and 
///  corrects for orographic effects
///  \remark Called prior to each computational timestep and presumes constant 
///  forcing functions over global time step
///
/// \param &Options [in] Global model options information
/// \param &tt [in] Current model time
//
void CModel::UpdateHRUForcingFunctions(const optStruct &Options,
                                       const time_struct &tt)
{
  force_struct  F;
  static force_struct Fg[MAX_GAUGES];
  double        elev,slope;
  int           mo,yr;
  int           k,g,nn;
  double        mid_day;
  int           model_day;

  double t  =tt.model_time;
  mo        =tt.month; 
  yr        =tt.year;
  nn        =(int)((tt.model_time+TIME_CORRECTION)/Options.timestep);//current timestep index.
  model_day =(int)(floor(tt.model_time));          //current day (measured from simulation start)

  mid_day= ((int)tt.julian_day)+0.5;//mid day

  //Extract data from gauge time series
  for (g=0;g<_nGauges;g++)
  {
    ZeroOutForcings(Fg[g]);
    
    Fg[g].precip          =_pGauges[g]->GetForcingValue    (F_PRECIP        ,nn);//mm/d 
    Fg[g].precip_daily_ave=_pGauges[g]->GetForcingValue    (F_PRECIP, model_day, 1);
    Fg[g].precip_5day     =_pGauges[g]->GetForcingValue    (F_PRECIP,t-5.0,5.0)*5.0;
    Fg[g].snow_frac       =_pGauges[g]->GetAverageSnowFrac (nn);
    Fg[g].temp_ave        =_pGauges[g]->GetForcingValue    (F_TEMP_AVE      ,nn);
    Fg[g].temp_daily_ave  =_pGauges[g]->GetForcingValue    (F_TEMP_DAILY_AVE,nn); 
    Fg[g].temp_daily_min  =_pGauges[g]->GetForcingValue    (F_TEMP_DAILY_MIN,nn); 
    Fg[g].temp_daily_max  =_pGauges[g]->GetForcingValue    (F_TEMP_DAILY_MAX,nn); 
    Fg[g].temp_ave_unc    =Fg[g].temp_daily_ave; 
    Fg[g].temp_min_unc    =Fg[g].temp_daily_min; 
    Fg[g].temp_max_unc    =Fg[g].temp_daily_max; 

    Fg[g].temp_month_max  =_pGauges[g]->GetMonthlyMaxTemp  (mo);
    Fg[g].temp_month_min  =_pGauges[g]->GetMonthlyMinTemp  (mo);
    Fg[g].temp_month_ave  =_pGauges[g]->GetMonthlyAveTemp  (mo);
    Fg[g].PET_month_ave   =_pGauges[g]->GetMonthlyAvePET   (mo);

    //if (Options.uses_full_data)
    { 
    // \todo [optimize] should add Options.uses_full_data to be calculated if LW,SW,etc. methods=USE_DATA or otherwise need data streams
    Fg[g].LW_radia        =_pGauges[g]->GetForcingValue    (F_LW_RADIA,nn);  
    Fg[g].SW_radia        =_pGauges[g]->GetForcingValue    (F_SW_RADIA,nn); 
    Fg[g].SW_radia_net    =_pGauges[g]->GetForcingValue    (F_SW_RADIA_NET,nn); 
    Fg[g].ET_radia        =_pGauges[g]->GetForcingValue    (F_ET_RADIA,nn); 
    Fg[g].SW_radia_unc    =Fg[g].SW_radia;

    Fg[g].PET             =_pGauges[g]->GetForcingValue    (F_PET,nn); 
    Fg[g].potential_melt  =_pGauges[g]->GetForcingValue    (F_POTENTIAL_MELT,nn); 

    Fg[g].air_pres        =_pGauges[g]->GetForcingValue    (F_AIR_PRES,nn);  
    Fg[g].rel_humidity    =_pGauges[g]->GetForcingValue    (F_REL_HUMIDITY,nn); 
    Fg[g].cloud_cover     =_pGauges[g]->GetForcingValue    (F_CLOUD_COVER,nn);  
    Fg[g].wind_vel        =_pGauges[g]->GetForcingValue    (F_WIND_VEL,nn); 

    //Fg[g].subdaily_corr =_pGauges[g]->GetForcingValue    (F_SUBDAILY_CORR,nn); 
    }
    if (Fg[g].temp_daily_max < Fg[g].temp_daily_min)
    {
      WriteWarning("UpdateHRUForcingFunctions: max_temp<min_temp at gauge: "+_pGauges[g]->GetName() + " on " + tt.date_string,Options.noisy);
    }
  }
  g_debug_vars[4]=_pGauges[0]->GetElevation();//RFS Emulation cheat

  //Generate HRU-specific forcings from gauge data
  //---------------------------------------------------------------------
  double ref_elev;
  for (k = 0; k < _nHydroUnits; k++)
  {
    elev  = _pHydroUnits[k]->GetElevation();
    slope = _pHydroUnits[k]->GetSlope();

    ZeroOutForcings(F);
    ref_elev = 0.0;

    //not gauge-based
    if (tt.day_changed)
    {
      F.day_angle  = CRadiation::DayAngle(mid_day, yr);
      F.day_length = CRadiation::DayLength(_pHydroUnits[k]->GetLatRad(), CRadiation::SolarDeclination(F.day_angle));
    }

    //interpolate forcing values from gauges
    //-------------------------------------------------------------------
    double wt;
    for (g = 0; g < _nGauges; g++)
    {
      wt=_aGaugeWeights[k][g];
      if (wt != 0.0){
        F.precip         += wt * Fg[g].precip;
        F.precip_daily_ave+=wt * Fg[g].precip_daily_ave;
        F.precip_5day    += wt * Fg[g].precip_5day;
        F.snow_frac      += wt * Fg[g].snow_frac;

        F.temp_ave       += wt * Fg[g].temp_ave;
        F.temp_daily_ave += wt * Fg[g].temp_daily_ave;
        F.temp_daily_min += wt * Fg[g].temp_daily_min;
        F.temp_daily_max += wt * Fg[g].temp_daily_max;
        F.temp_month_max += wt * Fg[g].temp_month_max;
        F.temp_month_min += wt * Fg[g].temp_month_min;
        F.temp_month_ave += wt * Fg[g].temp_month_ave;
        F.rel_humidity   += wt * Fg[g].rel_humidity;
        F.wind_vel       += wt * Fg[g].wind_vel;
        F.cloud_cover    += wt * Fg[g].cloud_cover;
        F.ET_radia       += wt * Fg[g].ET_radia;
        F.LW_radia       += wt * Fg[g].LW_radia;
        F.SW_radia       += wt * Fg[g].SW_radia;
        F.SW_radia_net   += wt * Fg[g].SW_radia_net;
        F.PET_month_ave  += wt * Fg[g].PET_month_ave;
        F.PET            += wt * Fg[g].PET;
        F.OW_PET         += wt * Fg[g].OW_PET;
        F.potential_melt += wt * Fg[g].potential_melt;
        //F.subdaily_corr+= wt * Fg[g].subdaily_corr;

        ref_elev         += wt * _pGauges[g]->GetElevation();
      }
    }
    //-------------------------------------------------------------------
    //  Gridded data support
    //-------------------------------------------------------------------
    //Override forcing functions with gridded data, if present
    /// \todo [funct] support gridded data read in 
    /*for (int gg = 0; gg < _nGridCells; gg++)
    {
      tt=//get this from time step and current storage history buffer of gridded data 
      wt = _aGridWeights[k][gg];
      F.precip += wt * GridForcings[gg][tt].precip;
      F.precip_daily_ave += wt * GridForcings[gg][tt].precip_daily_ave;
      F.precip_5day += wt * GridForcings[gg][tt].precip_5day;
      F.snow_frac += wt * GridForcings[gg][tt].snow_frac;

      F.temp_ave += wt * GridForcings[gg][tt].temp_ave;
      F.temp_daily_ave += wt * GridForcings[gg][tt].temp_daily_ave;
      F.temp_daily_min += wt * GridForcings[gg][tt].temp_daily_min;
      F.temp_daily_max += wt * GridForcings[gg][tt].temp_daily_max;
    }*/

    F.temp_ave_unc = F.temp_daily_ave;
    F.temp_min_unc = F.temp_daily_min;
    F.temp_max_unc = F.temp_daily_max;
    
    //-------------------------------------------------------------------
    //  Temperature Corrections
    //-------------------------------------------------------------------
    CorrectTemp(Options, F, elev, ref_elev, tt);

    //-------------------------------------------------------------------
    //  Copy Daily values from current day, earlier time steps  
    //    (done after temperature corrections so that the uncorrected values are used in calculating the lapse rate for temp_ave)
    //-------------------------------------------------------------------
    if (!tt.day_changed){
      _pHydroUnits[k]->CopyDailyForcings(F);
    }

    //-------------------------------------------------------------------
    //  Subdaily Corrections
    //-------------------------------------------------------------------
    F.subdaily_corr = CalculateSubDailyCorrection(F, Options, elev, ref_elev, tt, k);

    //-------------------------------------------------------------------
    //  Air Pressure, Density, relative humidity
    //-------------------------------------------------------------------
    F.air_pres = EstimateAirPressure(Options.air_pressure, F, elev);

    F.air_dens = GetAirDensity(F.temp_ave, F.air_pres);

    F.rel_humidity = EstimateRelativeHumidity(Options.rel_humidity, F);

    //-------------------------------------------------------------------
    // Snow fraction Calculations
    //-------------------------------------------------------------------
    F.snow_frac =EstimateSnowFraction(Options.rainsnow,&F,Options);

    //-------------------------------------------------------------------
    //  Precip Corrections
    //-------------------------------------------------------------------

    //--Gauge Corrections------------------------------------------------
    double gauge_corr;
    F.precip=F.precip_5day=F.precip_daily_ave=0.0;
    for(int g=0; g<_nGauges; g++)
    {        
        gauge_corr= F.snow_frac*_pGauges[g]->GetSnowfallCorr() + (1.0-F.snow_frac)*_pGauges[g]->GetRainfallCorr();
        wt=_aGaugeWeights[k][g];

        F.precip         += wt*gauge_corr*Fg[g].precip;
        F.precip_daily_ave+=wt*gauge_corr*Fg[g].precip_daily_ave;
        F.precip_5day    += wt*gauge_corr*Fg[g].precip_5day;
    }
    //--Orographic corrections-------------------------------------------
    CorrectPrecip(Options,F,elev,ref_elev,k,tt); 

    //-------------------------------------------------------------------
    //  Wind Velocity
    //-------------------------------------------------------------------

    F.wind_vel = EstimateWindVelocity(Options, F, k);

    //-------------------------------------------------------------------
    //  Cloud Cover
    //-------------------------------------------------------------------
    F.cloud_cover = EstimateCloudCover(Options, F, k);

    //-------------------------------------------------------------------
    //  Radiation Calculations
    //-------------------------------------------------------------------

    F.SW_radia = CRadiation::EstimateShortwaveRadiation(Options, &F, _pHydroUnits[k], tt, F.ET_radia);
    F.SW_radia_unc = F.SW_radia;
    F.SW_radia *= CRadiation::SWCloudCoverCorrection(Options, &F);
    F.SW_radia *= CRadiation::SWCanopyCorrection(Options, _pHydroUnits[k]);

    //if (Options.SW_radiation_net == SW_RAD_NET_CALCULATE)
    {
      F.SW_radia_net = F.SW_radia*(1 - _pHydroUnits[k]->GetTotalAlbedo());
    }//otherwise, uses data

    F.LW_radia=CRadiation::EstimateLongwaveRadiation  (Options,&F,_pHydroUnits[k]);
	
    //-------------------------------------------------------------------
    //  Potential Melt Rate
    //-------------------------------------------------------------------

    F.potential_melt=EstimatePotentialMelt(&F,Options,_pHydroUnits[k],tt);
    
    //-------------------------------------------------------------------
    //  PET Calculations
    //-------------------------------------------------------------------
    // last but not least - needs all of the forcing params calculated above
    F.PET   =EstimatePET(F,_pHydroUnits[k],Options.evaporation   ,tt);
    F.OW_PET=EstimatePET(F,_pHydroUnits[k],Options.ow_evaporation,tt);

    CorrectPET(Options,F,_pHydroUnits[k],elev,ref_elev,k);
    
    //-------------------------------------------------------------------
    // Update
    //-------------------------------------------------------------------
    _pHydroUnits[k]->UpdateForcingFunctions(F);
  }
}

//////////////////////////////////////////////////////////////////
/// \brief Estimates air pressure given elevation [kPa]
/// \param method [in] Method of calculating air pressure
/// \param &F [in] Forcing functons structure
/// \param &elev [in] Elevation at which air pressure is to be estimated
/// \return Estimated air pressure at elev [kPa]
//
double EstimateAirPressure     (const airpress_method method,
                                const force_struct &F,
                                const double       &elev)
{
  if (method==AIRPRESS_DATA){
    return F.air_pres;
  }
  else if (method==AIRPRESS_BASIC)
  {
    return AMBIENT_AIR_PRESSURE*pow(1.0-0.0065*elev/(ZERO_CELSIUS+F.temp_ave),5.26);
  }
  else if (method==AIRPRESS_UBC)
  {
    return KPA_PER_ATM*(1.0-0.0001*elev);//UBC_WM (c) Michael Quick
  }
  else if (method==AIRPRESS_CONST){
    return AMBIENT_AIR_PRESSURE;
  }
  else{
    return AMBIENT_AIR_PRESSURE;
  }
}

//////////////////////////////////////////////////////////////////
/// \brief Estimates relative humidity = ea/esat
/// \param method [in] Method of calculating relative humidity
/// \param &F [in] Reference to forcing function structure
/// \return Estimated relative humidity
//
double EstimateRelativeHumidity(const relhum_method method, 
                                const force_struct &F)
{
  if (method==RELHUM_CONSTANT)
  {
    return 0.5;
  }
  else if (method==RELHUM_MINDEWPT)
  { //uses minimum daily temperature as proxy for dew point temperature
    double dew_point_temp=F.temp_daily_min;
    return min(GetSaturatedVaporPressure(dew_point_temp)/GetSaturatedVaporPressure(F.temp_ave),1.0);
  }
  else if (method==RELHUM_DATA)
  {
    return F.rel_humidity;
  }
  return 0.5;
}

//////////////////////////////////////////////////////////////////
/// \brief Estimates wind velocity [m/s]
/// \details UBC method adapted from UBC Watershed model source code,
/// \copyright (c) Michael Quick
///
/// \param &Options [in] Global model options information
/// \param &F [in] Reference to forcing functions for HRU k
/// \param k [in] index of HRU
/// \return Estimated wind velocity in HRU k [m/s]
double CModel::EstimateWindVelocity(const optStruct &Options,
                                    const force_struct &F,
                                    const int k)
{
  //---------------------------------------------------------------------
  if (Options.wind_velocity==WINDVEL_CONSTANT)
  {
    return 2.0;//m/s (global average)
  }
  //---------------------------------------------------------------------
  else if (Options.wind_velocity==WINDVEL_DATA)
  {
    return F.wind_vel;
  }
  //---------------------------------------------------------------------
  else if (Options.wind_velocity==WINDVEL_UBCWM)
  {
    double TED, A1term;
    TED=max(F.temp_daily_max-F.temp_daily_min,0.0);

    const double ref_elev=2000;
    double elev=_pHydroUnits[k]->GetElevation();
    double Fc  =_pHydroUnits[k]->GetSurfaceProps()->forest_coverage; 
    double P0TEDL=CGlobalParams::GetParams()->UBC_lapse_params.P0TEDL;
    double P0TEDU=CGlobalParams::GetParams()->UBC_lapse_params.P0TEDU;
    double A0term=CGlobalParams::GetParams()->UBC_lapse_params.max_range_temp;
    if (elev>=ref_elev)
    {
      A1term=25.0-P0TEDL*0.001*ref_elev-P0TEDU*0.001*(elev-ref_elev);
    }
    else{
      A1term=25.0-P0TEDL*0.001*elev;
    }
    lowerswap(A1term,A0term); 
    
    const double max_wind_speed=8.0;//P0VBMX [km/h]
    lowerswap(TED,A1term);

    double wt=min(TED/25.0,1.0);

    double wind_vel = (1-wt)*max_wind_speed +(wt)*1;

    upperswap(wind_vel,1.0);
    lowerswap(wind_vel,max_wind_speed - 1.0); //why the minus one? who knows...

    //elevation correction
    wind_vel*=max(sqrt(elev/1000.0),1.0);

    //forest correction
    const double F0WIND=0.7; //ratio of wind in forest vs wind in open
    wind_vel*=((Fc)*F0WIND+(1.0-Fc)*1.0);

    wind_vel*=M_PER_KM/SEC_PER_HR; //KPH_TO_M_PER_S;
    return wind_vel;
  }
  else
  {
    return 2.0;
  }
}

//////////////////////////////////////////////////////////////////
/// \brief Returns estimate of cloud cover
/// \param &Options [in] Global model options information
/// \param &F [in] Reference to forcing functions for HRU
/// \param k [in] index of HRU
/// \return Estimated cloud cover [0..1] in HRU k
//
double CModel::EstimateCloudCover  (const optStruct &Options,
                                    const force_struct &F,
                                    const int k)
{
  double cover=0.0;
  switch(Options.cloud_cover)
  {
    case(CLOUDCOV_NONE):
    {
      cover=0.0;
      break;
    }
    case(CLOUDCOV_DATA):
    {
      cover=F.cloud_cover;
      break;
    }
    case (CLOUDCOV_UBCWM):
    {
      double range=(F.temp_max_unc-F.temp_min_unc); //uses uncorrected station temperature 
      double cloud_min_range(0.0),cloud_max_range(0.0);

      for (int g=0;g<_nGauges;g++){
        cloud_min_range+=_aGaugeWeights[k][g]*_pGauges[g]->GetCloudMinRange();//[C] A0FOGY in UBC_WM
        cloud_max_range+=_aGaugeWeights[k][g]*_pGauges[g]->GetCloudMaxRange();//[C] A0SUNY in UBC_WM
      }
      cover=1.0-(range-cloud_min_range)/(cloud_max_range-cloud_min_range);
      lowerswap(cover,1.0);
      upperswap(cover,0.0);
      break;
    }
  }
  return cover;
}
//////////////////////////////////////////////////////////////////
/// \brief Returns sub-daily correction for daily snowmelt or PET calculations
/// \param &Options [in] Global model options information
/// \param &F [in] Reference to forcing functions for HRU
/// \param &elev [in] elevation of HRU
/// \param &ref_elev [in] reference elevation of gauge
/// \param &tt [in] current time structure
/// \param k [in] index of HRU
/// \return sub-daily correction for daily snowmelt or PET calculations
//
double CModel::CalculateSubDailyCorrection(const force_struct &F,
                                           const optStruct    &Options,
                                           const double       &elev, 
                                           const double       &ref_elev, 
                                           const time_struct  &tt,
                                           const int          k)
{
  if (Options.timestep>=1.0){return 1.0;}

  //-----------------------------------------------------
  if (Options.subdaily==SUBDAILY_NONE)
  {
    return 1.0;
  }
  //-----------------------------------------------------
  else if (Options.subdaily==SUBDAILY_SIMPLE)
  { //tested and working - simple and elegant
    double DL=F.day_length;
    double dawn=0.5-0.5*DL;
    double dusk=0.5+0.5*DL;
    double t=tt.model_time-floor(tt.model_time); //time of day [d]
    double dt=Options.timestep;

    if      ((t>dawn) && (t+dt<=dusk)){
      return -0.5*(cos(PI*(t+dt-dawn)/DL)-cos(PI*(t-dawn)/DL))/Options.timestep;
    }
    else if ((t<dawn) && (t+dt>=dawn)){
      return -0.5*(cos(PI*(t+dt-dawn)/DL)-1                  )/Options.timestep;
    }
    else if ((t<dusk) && (t+dt>=dusk)){
       return -0.5*(-1                   -cos(PI*(t-dawn)/DL))/Options.timestep;
    }
    return 0.0;
  }
  //-----------------------------------------------------
  else if (Options.subdaily==SUBDAILY_UBC)
  {
    //this is not pretty (and somewhat expensive), due to the need to correct all daily temperatures for every timestep, but it works
    int nn_start=(int)(floor(tt.model_time    )/Options.timestep);//index of first timestp in day
    int nn_end  =(int)(floor(tt.model_time+1.0)/Options.timestep);//index of first timestp in following day
    force_struct Ftmp;
    time_struct  tt_tmp;
    tt_tmp=tt;
    tt_tmp.day_changed=true;
    
    double sum=0.0;
    for (int nnn=nn_start;nnn<nn_end;nnn++)
    {
      tt_tmp.model_time=nnn*Options.timestep;
      ZeroOutForcings(Ftmp);
      for (int g=0;g<_nGauges;g++)
      {
        Ftmp.precip_daily_ave+=_aGaugeWeights[k][g]*_pGauges[g]->GetForcingValue(F_PRECIP,floor(tt_tmp.model_time),1);
        Ftmp.temp_ave        +=_aGaugeWeights[k][g]*_pGauges[g]->GetForcingValue(F_TEMP_AVE,nnn);
        Ftmp.temp_daily_max  +=_aGaugeWeights[k][g]*_pGauges[g]->GetForcingValue(F_TEMP_DAILY_MAX,nnn);
        Ftmp.temp_daily_min  +=_aGaugeWeights[k][g]*_pGauges[g]->GetForcingValue(F_TEMP_DAILY_MIN,nnn);
      }
      CorrectTemp(Options,Ftmp,elev,ref_elev,tt_tmp);
      sum+=max(Ftmp.temp_ave,0.0);
    }
    
    if (sum==0){return 0.0;}
    else       {return max(F.temp_ave,0.0)/sum/Options.timestep;}
  }
  return 1.0;
}
//////////////////////////////////////////////////////////////////
/// \brief Returns participating parameter list for all forcing estimation/correction algorithms
///
/// \param *aP [out] array of parameter names needed by all forcing algorithms
/// \param *aPC [out] Class type (soil, vegetation, landuse or terrain) corresponding to each parameter
/// \param &nP [out] Number of parameters required (size of aP[] and aPC[])
//
void CModel::GetParticipatingParamList(string *aP, class_type *aPC, int &nP, const optStruct &Options) const
{

  /// \todo [clean] - tear out this ugly routine and replace with file read of this information
  nP=0;

  //Just assume needed:
  aP[nP]="FOREST_COVERAGE";   aPC[nP]=CLASS_LANDUSE; nP++;
  aP[nP]="POROSITY";          aPC[nP]=CLASS_SOIL;    nP++;

  // Interpolation Method parameters
  //----------------------------------------------------------------------
  if ((Options.interpolation==INTERP_NEAREST_NEIGHBOR) || (Options.interpolation==INTERP_AVERAGE_ALL))
  {
	// no parameter required
  }
  else if (Options.interpolation==INTERP_FROM_FILE)
  {
	// timeseries at gauge
  }
  else if (Options.interpolation==INTERP_INVERSE_DISTANCE)
  {
    // This method have not been tested yet
  }

  // Routing algorithms parameters
  //----------------------------------------------------------------------
  // Routing Method
  if ((Options.routing==ROUTE_NONE) || (Options.routing==ROUTE_DIFFUSIVE_WAVE) || (Options.routing==ROUTE_PLUG_FLOW)
	  || (Options.routing==ROUTE_STORAGECOEFF) || (Options.routing==ROUTE_MUSKINGUM) || (Options.routing==ROUTE_MUSKINGUM_CUNGE))
  {
    // Parameters are located in the RVH file?
	// Channel Geometry
	// Manning's n
  }

  // Catchement Route Method
  //----------------------------------------------------------------------
  if (Options.catchment_routing==ROUTE_DELAYED_FIRST_ORDER)
  {
    // This method have not been tested yet
    // Parameters are located in the RVH file, and there's no checking routine for that file yet. 
	  //aP[nP]="TIME_LAG"; aPC[nP]=CLASS_; nP++;
	  //aP[nP]="RES_CONSTANT"; aPC[nP]=CLASS_; nP++;
  }
  //else if (Options.catchment_routing==ROUTE_LAG) 
  //{
    //ROUTE_LAG method not implemented yet (See ParseInput.cpp)
	  //aP[nP]="TIME_LAG"; aPC[nP]=CLASS_L; nP++;
  //}
  else if (Options.catchment_routing==ROUTE_GAMMA_CONVOLUTION)
  {
    // Parameters are located in the RVH file, and there's no checking routine for that file yet. 
	  //aP[nP]="TIME_TO_PEAK"; aPC[nP]=CLASS_; nP++;
  }
  else if (Options.catchment_routing==ROUTE_TRI_CONVOLUTION)
  {
    // Parameters are located in the RVH file, and there's no checking routine for that file yet. 
	  //aP[nP]="TIME_TO_PEAK"; aPC[nP]=CLASS_; nP++;
	  //aP[nP]="TIME_CONC"; aPC[nP]=CLASS_; nP++;
  }
  else if (Options.catchment_routing==ROUTE_RESERVOIR_SERIES)
  {
    // Parameters are located in the RVH file, and there's no checking routine for that file yet.
	  //aP[nP]="RES_CONSTANT"; aPC[nP]=CLASS_; nP++;
	  //aP[nP]="NUM_RESERVOIRS"; aPC[nP]=CLASS_; nP++;
  }
  else if (Options.catchment_routing==ROUTE_DUMP)
  {
	// no parameter required
  }
  
  // Evaporation algorithms parameters
  //----------------------------------------------------------------------

  // Evaporation Method
  if (Options.evaporation==PET_DATA)
  {
	// timeseries at gauge
  }
  else if (Options.evaporation==PET_FROMMONTHLY)
  {
    // Parameters are located in the RVT file, and there's no checking routine for that file yet.
	//aP[nP]=":MonthlyAveEvaporation"; aPC[nP]=CLASS_; nP++;
	//aP[nP]=":MonthlyAveTemperature"; aPC[nP]=CLASS_; nP++;
  }
  else if (Options.evaporation==PET_MONTHLY_FACTOR)
  {
	aP[nP]="FOREST_PET_CORR"; aPC[nP]=CLASS_LANDUSE; nP++;
    // Parameters are located in the RVT file, and there's no checking routine for that file yet.
	//aP[nP]=":MonthlyEvapFactor";   aPC[nP]=CLASS_; nP++;
  }
  else if (Options.evaporation==PET_PENMAN_MONTEITH)
  {
	aP[nP]="MAX_HEIGHT";    aPC[nP]=CLASS_VEGETATION; nP++;
	aP[nP]="RELATIVE_HT";   aPC[nP]=CLASS_VEGETATION; nP++;
	aP[nP]="MAX_LAI";       aPC[nP]=CLASS_VEGETATION; nP++;
	aP[nP]="RELATIVE_LAI";  aPC[nP]=CLASS_VEGETATION; nP++;
	aP[nP]="MAX_LEAF_COND"; aPC[nP]=CLASS_VEGETATION; nP++;
	aP[nP]="FOREST_SPARSENESS";    aPC[nP]=CLASS_LANDUSE; nP++;
  aP[nP]="ROUGHNESS";     aPC[nP]=CLASS_LANDUSE; nP++;
  }
  else if (Options.evaporation==PET_PENMAN_COMBINATION)
  {
	aP[nP]="MAX_HEIGHT";  aPC[nP]=CLASS_VEGETATION; nP++;
	aP[nP]="RELATIVE_HT"; aPC[nP]=CLASS_VEGETATION; nP++;
  }
  else if (Options.evaporation==PET_HARGREAVES)
  {
	  // Need Max and Min Monthly Temp
	  //aP[nP]="TEMP_MONTH_MAX";  aPC[nP]=CLASS_; nP++;
	  //aP[nP]="TEMP_MONTH_MIN";  aPC[nP]=CLASS_; nP++;
  }
  else if ((Options.evaporation==PET_CONSTANT) || (Options.evaporation==PET_HAMON) || (Options.evaporation==PET_HARGREAVES_1985) 
			|| (Options.evaporation==PET_TURC_1961) || (Options.evaporation==PET_MAKKINK_1957) || (Options.evaporation==PET_PRIESTLEY_TAYLOR) || (Options.evaporation==PET_SHUTTLEWORTH_WALLACE))	
  {
	  // no parameter required
  }

  //Anywhere Albedo needs to be calculated for SW_Radia_net (will later be moved to albedo options)
  if ((Options.evaporation==PET_PRIESTLEY_TAYLOR) || (Options.evaporation==PET_SHUTTLEWORTH_WALLACE) || 
    (Options.evaporation==PET_PENMAN_MONTEITH) || (Options.evaporation==PET_PENMAN_COMBINATION) || 
    (Options.evaporation==PET_JENSEN_HAISE))
  {
  aP[nP]="ALBEDO";            aPC[nP]=CLASS_VEGETATION; nP++; //for SW_Radia_net
  aP[nP]="ALBEDO_WET";        aPC[nP]=CLASS_SOIL; nP++; //for SW_Radia_net
  aP[nP]="ALBEDO_DRY";        aPC[nP]=CLASS_SOIL; nP++; //for SW_Radia_net
  aP[nP]="SVF_EXTINCTION";    aPC[nP]=CLASS_VEGETATION; nP++; //for SW_Radia_net   
  }

  // OW_Evaporation Method 
  //----------------------------------------------------------------------
  if (Options.ow_evaporation==PET_DATA)
  {
	  // timeseries at gauge
  }
  else if (Options.ow_evaporation==PET_FROMMONTHLY)
  {
    // Parameters are located in the RVT file, and there's no checking routine for that file yet.
	  //aP[nP]=":MonthlyAveEvaporation"; aPC[nP]=CLASS_; nP++;
	  //aP[nP]=":MonthlyAveTemperature"; aPC[nP]=CLASS_; nP++;
  }
  else if (Options.ow_evaporation==PET_MONTHLY_FACTOR)
  {
	  aP[nP]="FOREST_PET_CORR"; aPC[nP]=CLASS_LANDUSE; nP++;
    // Parameters are located in the RVT file, and there's no checking routine for that file yet.
	  //aP[nP]=":MonthlyEvapFactor";   aPC[nP]=CLASS_; nP++;
  }
  else if (Options.ow_evaporation==PET_PENMAN_MONTEITH)
  {
	  aP[nP]="MAX_HEIGHT";    aPC[nP]=CLASS_VEGETATION; nP++;
	  aP[nP]="RELATIVE_HT";   aPC[nP]=CLASS_VEGETATION; nP++;
	  aP[nP]="MAX_LAI";       aPC[nP]=CLASS_VEGETATION; nP++;
	  aP[nP]="RELATIVE_LAI";  aPC[nP]=CLASS_VEGETATION; nP++;
	  aP[nP]="MAX_LEAF_COND"; aPC[nP]=CLASS_VEGETATION; nP++;
	  aP[nP]="FOREST_SPARSENESS";    aPC[nP]=CLASS_LANDUSE; nP++;\
    aP[nP]="ROUGHNESS";     aPC[nP]=CLASS_LANDUSE; nP++;
  }
  else if (Options.ow_evaporation==PET_PENMAN_COMBINATION)
  {
	  aP[nP]="MAX_HEIGHT";  aPC[nP]=CLASS_VEGETATION; nP++;
	  aP[nP]="RELATIVE_HT"; aPC[nP]=CLASS_VEGETATION; nP++;
  }
  else if ((Options.ow_evaporation==PET_CONSTANT) || (Options.ow_evaporation==PET_HAMON) || (Options.ow_evaporation==PET_HARGREAVES)
	  || (Options.ow_evaporation==PET_HARGREAVES_1985) || (Options.ow_evaporation==PET_TURC_1961) 
	  || (Options.ow_evaporation==PET_MAKKINK_1957) || (Options.ow_evaporation==PET_PRIESTLEY_TAYLOR))
  {
	// no parameter required/listed
  }
  // Orographic PET Correction Method
  //------------------------------------------------------------
  if (Options.orocorr_PET==OROCORR_UBCWM)
  {
	// hardcoded for now
  }
  else if ((Options.orocorr_PET==OROCORR_NONE) || (Options.orocorr_PET==OROCORR_SIMPLELAPSE))
  {
	// no parameter required
  }

  // Radiation algorithms parameters
  //----------------------------------------------------------------------
  // SW Radiation Method
  if (Options.SW_radiation==SW_RAD_UBCWM)
  {
	// currently hardcoded 
    // Parameters are located in the RVH file, and there's no checking routine for that file yet.
	//aP[nP]="HORIZON_CORR"; aPC[nP]=CLASS_; nP++;
	//aP[nP]="RES_CONSTANT"; aPC[nP]=CLASS_; nP++;
    aP[nP]="UBC_EXPOSURE_FACT"; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="UBC_SW_S_CORR";     aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="UBC_SW_N_CORR";     aPC[nP]=CLASS_GLOBAL; nP++;
  }
  else if (Options.SW_radiation==SW_RAD_DEFAULT)
  {
	// SLOPE and ASPECT are requried, but a check is unnecessary
  }
  else if (Options.SW_radiation==SW_RAD_DATA)
  {
	// timeseries at gauge
  }

  // LW Radiation Method
  //----------------------------------------------------------------------
  if (Options.LW_radiation==LW_RAD_DEFAULT) 
  {
    // FOREST_COVERAGE is required, but a check is unnecessary
  }
  else if (Options.LW_radiation==LW_RAD_UBCWM)
  {
	
    aP[nP]="UBC_LW_FOREST_FACT"; aPC[nP]=CLASS_GLOBAL; nP++;
  }
  else if (Options.LW_radiation==LW_RAD_DATA)
  {
	// timeseries at gauge
  }

  // Cloud Cover Method
  //----------------------------------------------------------------------
  if (Options.cloud_cover==CLOUDCOV_DATA)
  {
	// timeseries at gauge
  }
  else if (Options.cloud_cover==CLOUDCOV_NONE)
  {
    //no parameters needed
  }
  else if (Options.cloud_cover==CLOUDCOV_UBCWM)
  {
     //no parameters needed except Gauge->_cloud_min_temp and Gauge->_cloud_max_temp, which defaults to no cloud cover
  }
  //else if (Options.cloud_cover==CLOUDCOV_HARGREAVES)
  //{
    // Not Implemented yet
  //}

  //Canopy cover SW correction method
  //----------------------------------------------------------------------
  if (Options.SW_canopycorr==SW_CANOPY_CORR_STATIC){
    aP[nP]="RELATIVE_LAI";     aPC[nP]=CLASS_VEGETATION; nP++;
    aP[nP]="FOREST_SPARSENESS";aPC[nP]=CLASS_LANDUSE; nP++;
    aP[nP]="SVF_EXTINCTION";  aPC[nP]=CLASS_VEGETATION; nP++;
  }
  else if (Options.SW_canopycorr ==SW_CANOPY_CORR_UBCWM){
    aP[nP]="UBC_EXPOSURE_FACT";  aPC[nP]=CLASS_GLOBAL; nP++;
  }

  //Cloud cover SW correction method
  //----------------------------------------------------------------------
  if (Options.SW_cloudcovercorr ==SW_CLOUD_CORR_UBCWM){
    aP[nP]="UBC_CLOUD_PENET";  aPC[nP]=CLASS_GLOBAL; nP++;
  }

  // ET Radiation Method
  //----------------------------------------------------------------------
  // Not Implemented yet

  // Precipitation algorithms parameters
  //----------------------------------------------------------------------
  // Rain Snow Fraction Method
  if (Options.rainsnow==RAINSNOW_DATA)
  {
 	// timeseries at gauge
  }
  else if (Options.rainsnow==RAINSNOW_DINGMAN)
  {
	aP[nP]="RAINSNOW_TEMP"; aPC[nP]=CLASS_GLOBAL; nP++;
  }
  else if ((Options.rainsnow==RAINSNOW_HBV) || (Options.rainsnow==RAINSNOW_UBCWM))
  {
	aP[nP]="RAINSNOW_TEMP";  aPC[nP]=CLASS_GLOBAL; nP++;
	aP[nP]="RAINSNOW_DELTA"; aPC[nP]=CLASS_GLOBAL; nP++;
  }

  // Precipitation Interception Fraction Method
  //----------------------------------------------------------------------
  //handled by CmvPrecipitation::GetParticipatingParamList

  // Orographic Precipitation Correction Method
  //----------------------------------------------------------------------
  if (Options.orocorr_precip==OROCORR_HBV)
  {
    // Parameters are located in the RVT file, and there's no checking routine for that file yet.
	//aP[nP]=":RainCorrection"; aPC[nP]=CLASS_; nP++;
	//aP[nP]=":SnowCorrection"; aPC[nP]=CLASS_; nP++;
  }
  else if ((Options.orocorr_precip==OROCORR_UBCWM) || (Options.orocorr_precip==OROCORR_UBCWM2))
  {
	  aP[nP]="UBC_E0LHI"  ; aPC[nP]=CLASS_GLOBAL; nP++;
	  aP[nP]="UBC_E0LLOW" ; aPC[nP]=CLASS_GLOBAL; nP++;
	  aP[nP]="UBC_E0LMID" ; aPC[nP]=CLASS_GLOBAL; nP++;
	  aP[nP]="UBC_P0GRADL"; aPC[nP]=CLASS_GLOBAL; nP++;
	  aP[nP]="UBC_P0GRADM"; aPC[nP]=CLASS_GLOBAL; nP++;
	  aP[nP]="UBC_P0GRADU"; aPC[nP]=CLASS_GLOBAL; nP++;
	  aP[nP]="UBC_A0STAB" ; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="UBC_MAX_RANGE_TEMP" ; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="UBC_A0PPTP" ;       aPC[nP]=CLASS_GLOBAL; nP++; 
    aP[nP]="MAX_INTERCEPT_RATE";aPC[nP]=CLASS_VEGETATION; nP++; 
    aP[nP]="RAIN_ICEPT_PCT";    aPC[nP]=CLASS_VEGETATION; nP++; 
    aP[nP]="FOREST_COVERAGE";   aPC[nP]=CLASS_LANDUSE; nP++; 
    aP[nP]="UBC_ICEPT_FACTOR";   aPC[nP]=CLASS_LANDUSE; nP++; 
  }
  else if (Options.orocorr_precip==OROCORR_SIMPLELAPSE)
  {
    aP[nP]="PRECIP_LAPSE"; aPC[nP]=CLASS_GLOBAL; nP++;
  }
  else if (Options.orocorr_precip==OROCORR_NONE) 
  {
	// no parameter required
  }

  // Temperature algorithms parameters
  //----------------------------------------------------------------------
  // Orographic Temperature Correction Method
  if ((Options.orocorr_temp==OROCORR_UBCWM)  || (Options.orocorr_temp==OROCORR_UBCWM2)) 
  {
    aP[nP]="UBC_A0TLXH"; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="UBC_A0TLNM"; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="UBC_A0TLXM"; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="UBC_A0TLNH"; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="UBC_P0TEDL"; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="UBC_P0TEDU"; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="UBC_MAX_RANGE_TEMP"; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="ADIABATIC_LAPSE";     aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="WET_ADIABATIC_LAPSE"; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="MAX_INTERCEPT_RATE"; aPC[nP]=CLASS_VEGETATION; nP++;
    aP[nP]="RAIN_ICEPT_PCT";     aPC[nP]=CLASS_VEGETATION; nP++;
  }
  else if ((Options.orocorr_temp==OROCORR_HBV) || (Options.orocorr_temp==OROCORR_SIMPLELAPSE))
  {
	  aP[nP]="ADIABATIC_LAPSE"; aPC[nP]=CLASS_GLOBAL; nP++;
  }
  else if (Options.orocorr_temp==OROCORR_NONE)
  {
  	// no parameter required
  }

  // Energy algorithms parameters
  //----------------------------------------------------------------------
  // Potential Melt Method
  if (Options.pot_melt == POTMELT_DATA)
  {
    //none
  }
  else if (Options.pot_melt == POTMELT_DEGREE_DAY)
  {
    aP[nP]="MELT_FACTOR"; aPC[nP]=CLASS_LANDUSE; nP++;
  }
  else if (Options.pot_melt==POTMELT_RESTRICTED)
  {
    aP[nP]="MELT_FACTOR"; aPC[nP]=CLASS_LANDUSE; nP++;
  }	
  else if (Options.pot_melt==POTMELT_HBV)
  {
    aP[nP]="MELT_FACTOR";       aPC[nP]=CLASS_LANDUSE; nP++;
    aP[nP]="MIN_MELT_FACTOR";   aPC[nP]=CLASS_LANDUSE; nP++;
    aP[nP]="HBV_MELT_ASP_CORR"; aPC[nP]=CLASS_LANDUSE; nP++;
    aP[nP]="HBV_MELT_FOR_CORR"; aPC[nP]=CLASS_LANDUSE; nP++;
  }
  else if (Options.pot_melt==POTMELT_UBCWM)
  {
    aP[nP]="FOREST_COVERAGE";     aPC[nP]=CLASS_LANDUSE; nP++;
    aP[nP]="UBC_MIN_SNOW_ALBEDO"; aPC[nP]=CLASS_GLOBAL; nP++;
	  aP[nP]="UBC_SW_S_CORR";       aPC[nP]=CLASS_GLOBAL; nP++;
	  aP[nP]="UBC_SW_N_CORR";       aPC[nP]=CLASS_GLOBAL; nP++;
  }
  else if (Options.pot_melt==POTMELT_USACE)
  {
    aP[nP]="WIND_EXPOSURE";       aPC[nP]=CLASS_LANDUSE; nP++;
  }
  else if(Options.pot_melt==POTMELT_EB)
  {
    aP[nP]="MAX_HEIGHT";       aPC[nP]=CLASS_VEGETATION; nP++;
    aP[nP]="RELATIVE_HT";      aPC[nP]=CLASS_VEGETATION; nP++;
    aP[nP]="ROUGHNESS";        aPC[nP]=CLASS_LANDUSE; nP++;
    aP[nP]="SNOW_TEMPERATURE"; aPC[nP]=CLASS_GLOBAL; nP++;
  }
  // Sub Daily Method
  //----------------------------------------------------------------------
  if ((Options.subdaily==SUBDAILY_NONE) || (Options.subdaily==SUBDAILY_SIMPLE) || (Options.subdaily==SUBDAILY_UBC))
  {
  	// no parameter required
  }

  // Atmospheric Variables algorithms parameters
  //----------------------------------------------------------------------
  // Wind Speed Method
  if (Options.wind_velocity==WINDVEL_DATA)
  {
	// timeseries at gauge
  }
  else if (Options.wind_velocity==WINDVEL_UBCWM)
  {
	  aP[nP]="FOREST_COVERAGE"; aPC[nP]=CLASS_LANDUSE; nP++;
	  aP[nP]="UBC_P0TEDL"; aPC[nP]=CLASS_GLOBAL; nP++;
	  aP[nP]="UBC_P0TEDU"; aPC[nP]=CLASS_GLOBAL; nP++;
    aP[nP]="UBC_MAX_RANGE_TEMP"; aPC[nP]=CLASS_GLOBAL; nP++;
  }
  else if (Options.wind_velocity==WINDVEL_CONSTANT)
  {
  	// no parameter required
  }

  // Relative Humidity Method parameters
  //----------------------------------------------------------------------
  if ((Options.rel_humidity==RELHUM_CONSTANT) || (Options.rel_humidity==RELHUM_DATA) || (Options.rel_humidity==RELHUM_MINDEWPT))
  {
  	// no parameter required
  }

  // Air Pressure Method parameters
  //----------------------------------------------------------------------
  if (Options.air_pressure==AIRPRESS_DATA)
  {
	// timeseries at gauge
  }
  else if ((Options.air_pressure==AIRPRESS_BASIC) || (Options.air_pressure==AIRPRESS_UBC) || (Options.air_pressure==AIRPRESS_CONST))
  {
  	// no parameter required
  }

  // Monthly Interpolation Method parameters
  //----------------------------------------------------------------------
  if ((Options.month_interp==MONTHINT_UNIFORM) || (Options.month_interp==MONTHINT_LINEAR_FOM)
	  || (Options.month_interp==MONTHINT_LINEAR_MID) || (Options.month_interp==MONTHINT_LINEAR_21))
  {
  	// no parameter required
  }


  //...
}