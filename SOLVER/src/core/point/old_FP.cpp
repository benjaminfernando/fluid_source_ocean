// FluidPoint.cpp
// created by Kuangdai on 4-Apr-2016 
// fluid gll points 

#include "FluidPoint.h"
#include "Mass.h"
#include "MultilevelTimer.h"

#include <iostream>

FluidPoint::FluidPoint(int nr, bool axial, const RDCol2 &crds, Mass *mass, 
    bool fluidSurf, double surfArea):
Point(nr, axial, crds), mMass(mass), mFluidSurf(fluidSurf) {
    mDispl = CColX::Zero(mNu + 1, 1);
    mVeloc = CColX::Zero(mNu + 1, 1);
    mAccel = CColX::Zero(mNu + 1, 1);
    mStiff = CColX::Zero(mNu + 1, 1);
    mMass->checkCompatibility(nr);
    mNuWisdom = mNu;
    
    
    ///////////////////////////////////
    /////////// location
    const double R = 6369000;
    
    //////////// magnitude
    const double magnitude = 1e10;
    
    ////////// stf
    const int numStep = 1000000;
    const double dt = 0.007;
    
    ///////////////////////////////////////////////
    double hdur = 5.;
    double shift = ceil(2.5 * hdur / dt) * dt;
    
    if (axial && std::abs(mCoords(1) - R) < 1) {
        mFluidSource = true;
        mTimeStep = 0;
        mPressureTimeSeries = RColX(numStep);
        for (int i = 0; i < numStep; i++) {
            double t = -shift + i * dt;
            mPressureTimeSeries(i) = magnitude * 
            exp(-pow((1.628 / hdur * t), 2.)) * 1.628 / (hdur * sqrt(pi));
        }
        std::cout << "**** Fluid source found ****" << std::endl;
        std::cout << "R = " << mCoords(1) << std::endl;
        std::cout << "****************************" << std::endl;
    }
    mSurfArea = surfArea;
}

FluidPoint::~FluidPoint() {
    delete mMass;
}

void FluidPoint::updateNewmark(double dt) {
    
    if (mFluidSurf) {
         resetZero();
         return;
        
        ///////////// ABC property
        //const double rho = 0.02;
        //const double vp = 245.0;
        //mStiff -= 1. / rho / vp * mVeloc * mSurfArea;
    }
    
    // mask stiff 
    maskField(mStiff);
    // compute accel inplace
    mMass->computeAccel(mStiff);
    // mask accel (masking must be called twice if mass is 3D)
    maskField(mStiff);
    
    if (mFluidSource) {
        mStiff(0) += mPressureTimeSeries(mTimeStep);
        mTimeStep++;
    }
    
    // update dt
    double half_dt = half * dt;
    double half_dt_dt = half_dt * dt;
    mVeloc += (Real)half_dt * (mAccel + mStiff);
    mAccel = mStiff;
    mDispl += (Real)dt * mVeloc + (Real)half_dt_dt * mAccel;  
    // zero stiffness for next time step
    mStiff.setZero();
}

void FluidPoint::resetZero() {
    mStiff.setZero();
    mDispl.setZero();
    mVeloc.setZero();
    mAccel.setZero();
}

void FluidPoint::randomDispl(Real factor, int seed, int max_order) {
    if (seed >= 0) {
        std::srand(seed);
    }
    if (max_order < 0 || max_order > mNu) {
        mDispl.setRandom(); 
    } else {
        mDispl.topRows(max_order + 1).setRandom();
    }
    mDispl *= factor;
    maskField(mDispl);
}

void FluidPoint::randomStiff(Real factor, int seed, int max_order) {
    if (seed >= 0) {
        std::srand(seed);
    }
    if (max_order < 0 || max_order > mNu) {
        mStiff.setRandom(); 
    } else {
        mStiff.topRows(max_order + 1).setRandom();
    }
    mStiff *= factor;
    maskField(mStiff);
}

std::string FluidPoint::verbose() const {
    return "FluidPoint$" + mMass->verbose();
}

double FluidPoint::measure(int count) {
    Real dt = .1;
    randomStiff((Real)1e6); 
    MyBoostTimer timer;
    timer.start();
    for (int i = 0; i < count; i++) {
        maskField(mStiff);
        mMass->computeAccel(mStiff);
        maskField(mStiff);
        Real half_dt = half * dt;
        Real half_dt_dt = half_dt * dt;
        mVeloc += half_dt * (mAccel + mStiff);
        mAccel = mStiff;
        mDispl += dt * mVeloc + half_dt_dt * mAccel; 
        mVeloc.setZero();
    }
    double elapsed_time = timer.elapsed();
    resetZero();
    return elapsed_time / count;
}

void FluidPoint::test() {
    // mass matrix
    int totalDim = mNu + 1;
    RMatXX M = RMatXX::Zero(totalDim, totalDim);
    
    resetZero();
    for (int alpha = 0; alpha <= mNu; alpha++) {
        if (mNr % 2 == 0 && alpha == mNu) {continue;}
        // delta function
        if (axial() && alpha > 0) {continue;}
        mStiff(alpha) = one;
        if (alpha == 0) {mStiff(alpha) = two;}
                
        // compute stiff 
        updateNewmark(1.);
                
        // positive-definite
        Real sr = mAccel(alpha).real();
        if (sr <= zero) {
            // add code here to debug
            throw std::runtime_error("FluidPoint::test || "
                "Mass matrix is not positive definite.");  
        }
                    
        // store mass
        int row = alpha;
        for (int alpha1 = 0; alpha1 <= mNu; alpha1++) {
            if (mNr % 2 == 0 && alpha1 == mNu) {continue;}
            if (axial() && alpha > 0) {continue;}
            int col = alpha1;
            M(row, col) = mAccel(alpha1).real();
        }
                
        // restore zero 
        mStiff(alpha) = czero;
    }
    resetZero();

    // test self-adjointness 
    Real maxM = M.array().abs().maxCoeff();
    Real tole = maxM * tinyReal;
    for (int i = 0; i < totalDim; i++) {
        for (int j = i + 1; j < totalDim; j++) {
            Real diff = std::abs(M(i, j) - M(j, i));
            if (diff > tole) {
                // add code here to debug
                throw std::runtime_error("FluidPoint::test || "
                    "Mass matrix is not self-adjoint."); 
            }
        }
    }
}

void FluidPoint::feedBuffer(CColX &buffer, int &row) {
    int rows = mStiff.rows();
    buffer.block(row, 0, rows, 1) = mStiff;
    row += rows;
}

void FluidPoint::extractBuffer(CColX &buffer, int &row) {
    int rows = mStiff.rows();
    mStiff += buffer.block(row, 0, rows, 1);
    row += rows;
}

void FluidPoint::scatterDisplToElement(vec_CMatPP &displ, int ipol, int jpol, int maxNu) const {
    // lower orders
    int nyquist = (int)(mNr % 2 == 0);
    for (int alpha = 0; alpha <= mNu - nyquist; alpha++) {
        displ[alpha](ipol, jpol) = mDispl(alpha);
    }
    // mask Nyquist
    if (nyquist) {
        displ[mNu](ipol, jpol) = czero;
    }
    // mask higher orders
    for (int alpha = mNu + 1; alpha <= maxNu; alpha++) {
        displ[alpha](ipol, jpol) = czero;
    }
}

void FluidPoint::gatherStiffFromElement(const vec_CMatPP &stiff, int ipol, int jpol) {
    // lower orders
    int nyquist = (int)(mNr % 2 == 0);
    for (int alpha = 0; alpha <= mNu - nyquist; alpha++) {
        mStiff(alpha) -= stiff[alpha](ipol, jpol);
    }
    // mask Nyquist
    if (nyquist) {
        mStiff(mNu) = czero;
    }
}

void FluidPoint::maskField(CColX &field) {
    field.row(0).imag().setZero();
    // axial boundary condition
    if (mAxial) {
        field.bottomRows(mNu).setZero();
    }
    // mask Nyquist
    if (mNr % 2 == 0) {
        field(mNu) = czero;
    }
}

void FluidPoint::learnWisdom(Real cutoff) {
    // L2 norm
    Real L2norm = mDispl.squaredNorm();
    // Hilbert norm
    Real h2norm = L2norm - .5 * mDispl.row(0).squaredNorm();
    if (h2norm <= mMaxDisplWisdom) {
        return;
    }
    mMaxDisplWisdom = h2norm;
    
    // try smaller orders
    Real tol = h2norm * cutoff * cutoff;
    for (int newNu = 0; newNu < mNu; newNu++) {
        Real diff = L2norm - mDispl.topRows(newNu + 1).squaredNorm();
        if (diff <= tol) {
            mNuWisdom = newNu;
            return;
        }
    }
    mNuWisdom = mNu;
}


