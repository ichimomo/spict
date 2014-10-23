// Surplus Production in Continuous-Time (SPiCT)
// 14.10.2014
#include <TMB.hpp>


/* Predict log F */
template<class Type>
inline Type predictlogF(const Type &phi1, const Type &logF1, const Type &phi2, const Type &logF2)
{
  return phi1*logF1 + phi2*logF2;
}

/* Calculate B_infinity */
template<class Type>
inline Type calculateBinf(const Type &K, const Type &F, const Type &r, const Type &sdb2=0, int lamperti=0)
{
  if(lamperti){
    return K * (1 - F/r - 0.5*sdb2/r);
  } else {
    return K * (1 - F/r);
  }
}

/* Predict biomass */
template<class Type>
inline Type predictB(const Type &B0, const Type &Binf, const Type &F, const Type &r, const Type &K, const Type &dt, const Type &sdb2=0, int lamperti=0, int euler=0)
{
  if(euler) lamperti = 1;
  Type rate;
  if(lamperti){
    rate = r - F - 0.5*sdb2;
  } else {
    rate = r - F;    
  }
  if(euler){
    return exp( log(B0) + (rate - r/K*B0)*dt ); // Euler
  } else {
    return 1 / ( 1/Binf + (1/B0 - 1/Binf) * exp(-rate*dt) ); // Approximative analytical
  }
}

/* Calculate  */
template<class Type>
inline Type predictC(const Type &F, const Type &K, const Type &r, const Type &B0, const Type &Binf, const Type &dt, const Type &sdb2=0, int lamperti=0, int euler=0)
{
  if(euler) lamperti = 1;
  Type rate;
  if(lamperti){
    rate = r - F - 0.5*sdb2;
  } else {
    rate = r - F;    
  }
  if(euler){
    return F*B0*dt;
  } else {
    return K/r*F * log( 1 - B0/Binf * (1 - exp(rate*dt)));
  }
}

/* Main script */
template<class Type>
Type objective_function<Type>::operator() ()
{
  Type ans=0;

  DATA_INTEGER(delay);       // Delay
  DATA_VECTOR(dt);       // Time steps
  DATA_SCALAR(dtpred);       // Time step for prediction
  DATA_VECTOR(Cobs);       // Catches
  DATA_VECTOR(ic); // Vector such that B(ii(i)) is the state corresponding to Cobs(i)
  DATA_VECTOR(nc); // nc(i) gives the number of time intervals Cobs(i) spans
  DATA_VECTOR(I);       // Index
  DATA_VECTOR(ii); // A vector such that B(ii(i)) is the state corresponding to I(i)
  DATA_VECTOR(isum); // A vector indicating indices of summer
  DATA_INTEGER(lamperti);       // Lamperti flag.
  DATA_INTEGER(euler);       // Euler flag.
  DATA_SCALAR(dbg);       // Debug flag, if == 1 then print stuff.
  PARAMETER(phi1);       // 
  PARAMETER(phi2);       // 
  PARAMETER(alpha);       // sdi = alpha*sdb
  PARAMETER(beta);       // sdc = beta*sdf
  PARAMETER(loggamma);       // rsum = gamma*r, where rsum is r in the summer (Q2+Q3)
  PARAMETER(logr);         // Intrinsic growth
  PARAMETER(logK);         // Carrying capacity
  PARAMETER(logq);         // Catchability
  PARAMETER(logsdf);   // Standard deviation for F
  PARAMETER(logsdb);   // Standard deviation for Index
  PARAMETER_VECTOR(logF);  // Random effects vector
  PARAMETER_VECTOR(logB);  // Random effects vector

  Type r = exp(logr);
  Type K = exp(logK);
  Type q = exp(logq);
  Type sdf = exp(logsdf);
  Type sdb = exp(logsdb);
  Type sdb2 = sdb*sdb;
  Type sdi = alpha*sdb;
  Type sdc = beta*sdf;
  Type gamma = exp(loggamma);
  int nCobs = Cobs.size();
  int nIobs = I.size();
  int ns = logF.size();
  vector<Type> F = exp(logF);
  vector<Type> P(ns-1);
  vector<Type> Binf(ns);
  vector<Type> logBinf(ns);
  vector<Type> B = exp(logB);
  vector<Type> Bpred(ns);
  vector<Type> rvec(ns);
  vector<Type> Cpred(nCobs);
  for(int i=0; i<nCobs; i++){ Cpred(i) = 0; }
  vector<Type> Cpredsub(ns);
  vector<Type> logIpred(nIobs);
  vector<Type> logCpred(nCobs);
  Type Bmsy = K/2;
  Type Fmsy;
  if(lamperti){
    Fmsy = r/2 - 0.5*sdb2;
  } else {
    Fmsy = r/2;
  }
  Type MSY = Bmsy * Fmsy;
  Type logBmsy = log(Bmsy);
  Type logFmsy = log(Fmsy);
  Type likval;

  if(dbg>0){
    std::cout << "--- DEBUG: script start ---" << std::endl;
    //for(int i=0; i<ns; i++) std::cout << "F(i): " << F(i) << std::endl;
    std::cout << "INPUT: logr: " << logr << std::endl;
    std::cout << "INPUT: logK: " << logK << std::endl;
    std::cout << "INPUT: logq: " << logq << std::endl;
    std::cout << "INPUT: logsdf: " << logsdf << std::endl;
    std::cout << "INPUT: logsdb: " << logsdb << std::endl;
    std::cout << "Cobs.size(): " << Cobs.size() << "  Cpred.size(): " << Cpred.size() << "  I.size(): " << I.size() << "  dt.size(): " << dt.size() << "  F.size(): " << F.size() << "  B.size(): " << B.size() << "  P.size(): " << P.size() << "  rvec.size(): " << rvec.size() << std::endl;
  }
  for(int i=0; i<ns; i++){
    if(F(i)==r) std::cout << "Warning: F(i)-r: " << F(i)-r << std::endl;
  }
  // Calculate rvec
  for(int i=0; i<ns; i++){
    if(isum(i)==1){
      rvec(i) = gamma*r;
    } else {
      rvec(i) = r;
    }
    if(dbg>1){
      std::cout << "-- i: " << i << " -   rvec(i): " << rvec(i) << std::endl;
    }
  }

  /*
  dt[i] is the length of the time interval between t_i and t_i+1
  B[t] is biomass at the beginning of the time interval starting at time t
  Binf[t] is equilibrium biomass with the parameters given at the beginning of the time interval starting at time t
  I[t] is an index of abundance (e.g. CPUE) at the beginning of the time interval starting at time t
  P[t] is the accumulated biomass production over the interval starting at time t
  F[t] is the constant fishing mortality during the interval starting at time t
  rvec[t] is the constant intrinsic growth rate during the interval starting at time t
  C[t] is the catch removed during the interval starting at time t.
  */

  /*
  --- PROCESS EQUATIONS ---
  */


  // FISHING MORTALITY
  if(dbg>0){
    std::cout << "--- DEBUG: F loop start" << std::endl;
  }
  for(int i=delay; i<ns; i++){
    //Type logFpred = phi1*logF(i-1) + phi2*logF(i-delay); // Prediction of F
    Type logFpred = predictlogF(phi1, logF(i-1), phi2, logF(i-delay));
    likval = dnorm(logF(i), logFpred, sqrt(dt(i-1))*sdf, 1);
    ans-=likval;
    // DEBUGGING
    if(dbg>1){
      std::cout << "-- i: " << i << " -   logF(i-1): " << logF(i-1) << "  logF(i): " << logF(i) << "  sdf: " << sdf << "  likval: " << likval << std::endl;
    }
  }

  // CALCULATE B_infinity
  //for(int i=0; i<ns; i++) Binf(i) = K * (1 - F(i)/rvec(i));
  for(int i=0; i<ns; i++) Binf(i) = calculateBinf(K, F(i), rvec(i), sdb2, lamperti); 

  // BIOMASS PREDICTIONS
  for(int i=0; i<(ns-1); i++){
    // To predict B(i) use dt(i-1), which is the time interval from t_i-1 to t_i
    //Bpred(i+1) = predictB(B(i), Binf(i), F(i), rvec(i), K, dt(i), sdb2, lamperti, euler);
    Bpred(i+1) = predictB(B(i), Binf(i+1), F(i+1), rvec(i+1), K, dt(i), sdb2, lamperti, euler);
    likval = dnorm(log(Bpred(i+1)), logB(i+1), sqrt(dt(i))*sdb, 1);
    ans-=likval;
    // DEBUGGING
    if(dbg>1){
      std::cout << "-- i: " << i << " -   logB(i+1): " << logB(i+1) << "  log(Bpred(i+1)): " << log(Bpred(i+1)) << "  sdb: " << sdb << "  likval: " << likval << std::endl;
    }
  }

  // CATCH PREDICTIONS
  for(int i=0; i<(ns-1); i++){ // ns-1 because dt is 1 shorter than state vec
    // For Cpredsub(i) use dt(i) because Cpredsub(i) is integrated over t_i to t_i+1
    Cpredsub(i) =  predictC(F(i), K, rvec(i), B(i), Binf(i), dt(i), sdb2, lamperti, euler);
  }

  // CALCULATE PRODUCTION
  //if(lamperti){
  for(int i=0; i<(ns-1); i++) P(i) = B(i+1) - B(i) + Cpredsub(i);
  //} else {
  //for(int i=0; i<(ns-1); i++) P(i) = B(i)*(r - r/K*B(i))*dt(i);
    //for(int i=0; i<(ns-1); i++) P(i) = B(i+1) - B(i) + F(i)*B(i)*dt; //Cpredsub(i);
    //}



  /*
  --- OBSERVATION EQUATIONS ---
  */

  // CATCHES
  if(dbg>0){
    std::cout << "--- DEBUG: Cpred loop start" << std::endl;
  }
  int ind;
  for(int i=0; i<nCobs; i++){
    // Sum catch contributions from each sub interval
    for(int j=0; j<nc(i); j++){
      ind = CppAD::Integer(ic(i)-1) + j; // minus 1 because R starts at 1 and c++ at 0
      Cpred(i) += Cpredsub(ind);
    }
    logCpred(i) = log(Cpred(i));
    likval = dnorm(log(Cpred(i)), log(Cobs(i)), sdc, 1);
    ans-=likval;
    // DEBUGGING
    if(dbg>1){
      std::cout << "-- i: " << i << " -  ind: " << ind << " -   logCobs(i): " << log(Cobs(i))<< "  log(Cpred(i)): " << log(Cpred(i)) << "  sdc: " << sdc << "  likval: " << likval << std::endl;
    }
  }

  // ABUNDANCE INDEX
  if(dbg>0){
    std::cout << "--- DEBUG: Ipred loop start" << std::endl;
  }
  for(int i=0; i<nIobs; i++){
    if(I(i)>0){
      ind = CppAD::Integer(ii(i)-1);
      logIpred(i) = logq+log(B(ind));
      likval = dnorm(log(I(i)), logIpred(i), sdi, 1);
      ans-=likval;
      // DEBUGGING
      if(dbg>1){
	std::cout << "-- i: " << i << " -  ind: " << ind << " -   log(I(i)): " << log(I(i)) << "  logIpred(i): " << logIpred(i) << "  sdi: " << sdi << "  likval: " << likval << std::endl;
      }
    }
  }

  // ONE-STEP-AHEAD PREDICTIONS
  Type logFp = predictlogF(phi1, logF(ns-1), phi2, logF(ns-delay));
  Type Fp = exp(logFp);
  Type Bp;
  Type Binfp;
  Type Cp;
  Binfp = calculateBinf(K, Fp, rvec(ns-1), sdb2, lamperti);
  Bp = predictB(B(ns-1), Binfp, Fp, rvec(ns-1), K, dtpred, sdb2, lamperti, euler);
  Cp = predictC(Fp, K, rvec(ns-1), Bp, Binfp, dtpred, sdb2, lamperti, euler);
  Type logIp = logq + log(Bp);

  Type Cinfp = predictC(Fp, K, rvec(ns-1), Binfp, Binfp, dtpred, sdb2, lamperti, euler); // This one doesn't accommodate delays

  // MSY PREDICTIONS
  Type Bpmsy;
  Type Binfpmsy;
  Type Cpmsy;
  Binfpmsy = calculateBinf(K, Fmsy, rvec(ns-1), sdb2, lamperti);
  Bpmsy = predictB(B(ns-1), Binfpmsy, Fmsy, rvec(ns-1), K, dtpred, sdb2, lamperti, euler);
  Cpmsy = predictC(Fmsy, K, rvec(ns-1), Bpmsy, Binfpmsy, dtpred, sdb2, lamperti, euler);

  // ADREPORTS
  ADREPORT(r);
  ADREPORT(K);
  ADREPORT(q);
  ADREPORT(sdf);
  ADREPORT(sdc);
  ADREPORT(sdi);
  ADREPORT(Bmsy);
  ADREPORT(MSY);
  ADREPORT(Fmsy);
  ADREPORT(logBmsy);
  ADREPORT(logFmsy);
  Type logBp = log(Bp);
  ADREPORT(logBp);
  Type logBpmsy = log(Bpmsy);
  ADREPORT(logBpmsy);
  ADREPORT(Cpmsy);
  ADREPORT(Cinfp);
  ADREPORT(Cpredsub);
  ADREPORT(logIpred);
  ADREPORT(logCpred);
  ADREPORT(P);
  logBinf = log(Binf);
  ADREPORT(logBinf);
  ADREPORT(logFp);
  // REPORTS (these don't require sdreport to be output)
  REPORT(Cp);
  REPORT(logIp);

  return ans;
}
