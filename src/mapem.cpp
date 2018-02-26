#include "mapem.h"


/** Prapares for fitting.
*
* bc - number of ER-CHMM branches,
* ri - array of number of states in ER-CHMM branches,
* alphaArr - array of initial branch probabilities,
* lambdaArr - array of transition rates in each ER-CHMM branch,
* timeCount - number of inter-arrivals,
* timeArr - array of inter-arrivals,
* minIterCount/maxIterCount - min/max number of iterations,
* eps - min relative change in likelihood values before stopping iterations,
* sumVarCount - number of variables used in summation technique.
* maxPhi - a parameter in summation technique.
*/
void ErChmmEm::prepare(
    int bc,
    int *ri,
    float *alphaArr,
    float *lambdaArr,
    float *pArr,
    int timeCount,
    float *timeArr, 
    int minIterCount,
    int maxIterCount,
    float eps, 
    int sumVarCount,
    float maxPhi
  ){

  mMinIterCount = minIterCount;
  mMaxIterCount = maxIterCount;
  mEps = eps;
  mMaxPhi = maxPhi;
  mSumVarCount = sumVarCount;
  mTimeCount = timeCount;
  mBc = bc;

  mRi = new int[bc];
  mLambdaArr = new float[bc];
  mAlphaArr = new float[bc];
  mPArr = new float[bc*bc];
  mTimeArr = new float[timeCount];

  for(int i = 0; i < bc; i++)
    mRi[i] = ri[i];

  for(int i = 0; i < bc; i++){
    mLambdaArr[i] = lambdaArr[i];
    mAlphaArr[i] = alphaArr[i];
    for(int j = 0; j < bc; j++)
      mPArr[i*bc+j] = pArr[i*bc+j];
  }

  for(int i = 0; i < timeCount; i++)
    mTimeArr[i] = timeArr[i]; 

  msumqk = new float[sumVarCount*bc];
  msumxqk = new float[sumVarCount*bc];
  mnewP = new float[sumVarCount*bc*bc];
 
  mfMat = new float[bc*timeCount];
  maMat = new float[bc*timeCount];
  mbMat = new float[bc*timeCount];

  maScale = new int[timeCount];
  mbScale = new int[timeCount];

  mqVecCurr = new float[bc];
}

void ErChmmEm::add_vec_value(float value, float *vecArr, int i,  int to){
 
  if(to+1 == mSumVarCount){
    vecArr[to*mBc + i] += value;
    return;  
  }

  float mag = vecArr[to*mBc + i] / value;
  if(mag < mMaxPhi){
    vecArr[to*mBc + i] += value;
    return;
  }

  float value2 = vecArr[to*mBc + i];
  add_vec_value(value2, vecArr, i, to+1);
  vecArr[to*mBc + i] = value;
}

void ErChmmEm::sum_vec_values(float *vecArr){
  for(int sp = 1; sp < mSumVarCount; sp++)
    for(int i = 0; i < mBc; i++)
      vecArr[i] += vecArr[sp*mBc + i];
}
 
void ErChmmEm::add_mat_value(float value, float *matArr, int i, int j,  int to){
  
  if(to+1 == mSumVarCount){
    matArr[to*mBc*mBc + i*mBc + j] += value;
    return;  
  }

  float mag = matArr[to*mBc*mBc + i*mBc + j] / value;
  if(mag < mMaxPhi){
    matArr[to*mBc*mBc + i*mBc + j] += value;
    return;
  }

  float value2 = matArr[to*mBc*mBc + i*mBc + j];
  add_mat_value(value2, matArr, i, j, to+1);
  matArr[to*mBc*mBc + i*mBc + j] = value;

}

void ErChmmEm::sum_mat_values(float *matArr){
  for(int sp = 1; sp < mSumVarCount; sp++)
    for(int i = 0; i < mBc; i++)
      for(int j = 0; j < mBc; j++)
        matArr[i*mBc + j] += matArr[sp*mBc*mBc + i*mBc + j];
}
 

void ErChmmEm::calc(){

  // parameters
  float *alphaArr = mAlphaArr;
  float *pArr = mPArr; 
  float *lambdaArr = mLambdaArr;

  // data
  float *sumqk = msumqk;
  float *sumxqk = msumxqk;

  float *fMat = mfMat;
  float *aMat = maMat;
  float *bMat = mbMat;
  int *aScale = maScale;
  int *bScale = mbScale;
  float *newP = mnewP;
  float *qVecCurr = mqVecCurr;

  // structure
  int bc = mBc;
  int *ri = mRi;
  
  float logli = -FLT_MAX, ologli = -FLT_MAX; 
  float log2 = log(2.0);
  float stopCriteria = log(1 + mEps);
  int iterCounter = 0;

  for(int iter = 0; iter < mMaxIterCount + 1; iter++) {

    // Calculate vector f (branch densities)
    for(int k = 0; k < mTimeCount; k++) {
      float x = mTimeArr[k];
      for(int m=0; m < bc; m++)
        fMat[k*bc+m] = computeBranchDensity (x, lambdaArr[m], ri[m]);        
    }

    // calculate vectors a and vector b (forward and backward likelihood vectors)
    memset(aMat, 0, bc*mTimeCount*sizeof(float));
    memset(bMat, 0, bc*mTimeCount*sizeof(float));
    memset(aScale, 0, mTimeCount*sizeof(int));
    memset(bScale, 0, mTimeCount*sizeof(int));

    // vectors a
    for(int i = 0; i < bc; i++)
      for(int j=0; j < bc; j++)
        aMat[i] += alphaArr[j]*fMat[j]*pArr[j*bc+i];

    for(int k = 1; k < bc; k++) {
      int ofs = k*bc;
      float asum = 0.0;
      for(int i = 0; i < bc; i++){
        for(int j = 0; j < bc; j++)
          aMat[ofs+i] += aMat[ofs+j-bc]*fMat[ofs+j]*pArr[j*bc+i];
        asum += aMat[ofs+i];
      }
      aScale[k] = aScale[k-1];
      if(asum==0) 
        break;
                    

      int scaleDiff = ceil(log(asum) / log(2));
      aScale[k] += scaleDiff;

      float factor = pow(2.0, -scaleDiff);
      for(int i = 0; i < bc; i++)
        aMat[ofs+i] *= factor;
    }

    // vectors b
    int ofs = (mTimeCount-1)*bc;
    for(int j = 0; j < bc; j++)
      for(int i = 0; i < bc; i++)
        bMat[ofs+j] += fMat[ofs+j]*pArr[j*bc+i];

    for(int k = mTimeCount - 2; k >= 0; k--) {
      ofs = k*bc;
      float bsum = 0.0;
      for(int j = 0; j < bc; j++){
        for(int i = 0; i < bc; i++)
          bMat[ofs+j] += bMat[ofs+i+bc]*fMat[ofs+j]*pArr[j*bc+i];
        bsum += bMat[ofs+j];
      }
      bScale[k] = bScale[k+1];
      if(bsum==0) 
        break;

      int scaleDiff = ceil(log(bsum) / log(2));
      bScale[k] += scaleDiff;

      float factor = pow(2.0, -scaleDiff);
      for(int i = 0; i < bc; i++)
        bMat[ofs+i] *= factor;
    }

    // store the previous log-likelihood value
    ologli = logli;

    // calculate log-likelihood
    float llhval = 0.0;
    for(int i = 0; i < bc; i++)
      llhval += alphaArr[i]*bMat[i];
    logli = (log(llhval) + bScale[0] * log2);  

    // check for stop conditions
    if(iter > mMinIterCount + 1)
      if((logli - ologli) < stopCriteria)
        break;
    if(iter == mMaxIterCount)
      break;
    iterCounter++;

    float illh = 1.0 / llhval;

    memset(newP, 0, mSumVarCount*bc*bc*sizeof(float));
    memset(sumqk, 0, mSumVarCount*bc*sizeof(float));
    memset(sumxqk, 0,mSumVarCount*bc*sizeof(float));

    // calculate new estimates for the parameters
    for(int k = 0; k < mTimeCount; k++) {
      int ofs = k*bc;
      float x = mTimeArr[k];
      float qVecSum = 0.0;
      float normalizer;
      if(k==0)
        normalizer = ldexp (illh, bScale[1] - bScale[0]);
      else if (k < mTimeCount - 1)
        normalizer = ldexp(illh, aScale[k-1] + bScale[k+1] - bScale[0]);

      for(int m = 0; m < bc; m++) {
        float pMul = (k==0 ? alphaArr[m] : aMat[ofs+m-bc]);
        qVecSum += qVecCurr[m] = pMul * bMat[ofs+m];
                   
        if(k < mTimeCount - 1) {
          pMul *= fMat[ofs+m] * normalizer;
          for(int j = 0; j < bc; j++){
            float value =  pMul * pArr[m*bc+j] * bMat[ofs+bc+j];
            add_mat_value(value, newP, m, j, 0);
          } 
        }
      }

      for(int m = 0; m < bc; m++) {
        float value, mag;
        value = qVecCurr[m] / qVecSum; 
        add_vec_value(value, sumqk, m, 0);

        value = x * qVecCurr[m] / qVecSum;
        add_vec_value(value, sumxqk, m, 0);
      }
    }

    sum_mat_values(newP);
    sum_vec_values(sumqk);
    sum_vec_values(sumxqk);

    // store new estimates
    for(int i = 0; i < bc; i++) {
      lambdaArr[i] = ri[i] * sumqk[i] / sumxqk[i];
      alphaArr[i] = sumqk[i] / mTimeCount; 
 
      float rowSum = 0.0;
      for(int j = 0; j < bc; j++)
        rowSum += newP[i*bc+j];
      for(int j = 0; j < bc; j++)
        pArr[i*bc+j] = newP[i*bc+j] / rowSum;
    }
  }

  mLogLikelihood = logli; 
}

void ErChmmEm::finish(){

  delete [] mRi;
  delete [] mLambdaArr;
  delete [] mAlphaArr;
  delete [] mPArr;

  delete [] mTimeArr;

  delete [] msumqk;
  delete [] msumxqk;
  delete [] mnewP;

  delete [] mfMat;
  delete [] maMat;
  delete [] mbMat;

  delete [] maScale;
  delete [] mbScale;


  delete [] mqVecCurr;
}    



 
float ErChmmEm::computeBranchDensity(float x, float lambda, int branchSize){
  float erlFact = lambda;
  for (int n=1; n < branchSize; n++)
    erlFact *= lambda * x / n;
  return exp (-lambda * x) * erlFact;
}

float ErChmmEm::getLogLikelihood(){
  return mLogLikelihood;
}

float* ErChmmEm::getAlphaArr(){
  return mAlphaArr;
}

float* ErChmmEm::getLambdaArr(){
  return mLambdaArr;
}

float* ErChmmEm::getPArr(){
  return mPArr;
}


