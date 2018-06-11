#pragma once
/** class PoseGraphSLAM

        This runs in a separate thread. Retrives data from NodeDataManager
        on the node's poses and closure edges. Does the non-linear
        least squares.

        Author  : Manohar Kuse <mpkuse@connect.ust.hk>
        Created : 6th, June, 2018

*/
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <queue>
#include <ostream>


#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>


//opencv
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/calib3d.hpp>
#include <cv_bridge/cv_bridge.h>

//ceres
#include <ceres/ceres.h>


// ros
#include <ros/ros.h>
#include <ros/package.h>

#include "NodeDataManager.h"
#include "ScreenColors.h"

#include "pose_manip_utils/PoseManipUtils.h"

using namespace std;
using namespace Eigen;

class PoseGraphSLAM
{
public:
    PoseGraphSLAM( NodeDataManager* _manager );
    // void stop() { run = false; }

    // This is intended to be run in a separate thread.
    void optimize6DOF();

    // Get the optimized pose at node i. This function is thread-safe
    Matrix4d getNodePose( int i );
    int nNodes();
    void getAllNodePose( vector<Matrix4d>& vec_w_T_ci );

private:
    //TODO leran how to correctly use atomic.

    const NodeDataManager * manager;

    // Optimization variables
    std::mutex * mutex_opt_vars;
    vector<double*> opt_quat;
    vector<double*> opt_t;



};

template <typename T>
T NormalizeAngle(const T& angle_degrees) {
	// if this non-linearity can be removed the angular problem (for constant translation) is a linear problem.
	// the l1-norm iros2014 paper by Luca Corlone suggest to use another latent variable for this and penalize it to be near zero.
  if (angle_degrees > T(180.0))
  	return angle_degrees - T(360.0);
  else if (angle_degrees < T(-180.0))
  	return angle_degrees + T(360.0);
  else
  	return angle_degrees;
};

class SixDOFError
{
public:
    SixDOFError( const Matrix4d& _observed__c1_T_c2 ) : observed__c1_T_c2( _observed__c1_T_c2 )
    {
        double a[10], b[10];
        PoseManipUtils::eigenmat_to_rawyprt(  observed__c1_T_c2, a, b );
        observed_c1_ypr_c2 << a[0], a[1], a[2];
        // observed_c1_t_c2 << b[0], b[1], b[2];

        double c[10], d[10];
        PoseManipUtils::eigenmat_to_raw( observed__c1_T_c2, c, d );
        observed_c1_q_c2 = Quaterniond( c[0], c[1], c[2], c[3] );
        observed_c1_t_c2 << d[0], d[1], d[2];
    }

    // q1, t1 : w_T_c1
    // q2, t2 : w_T_c2
    template <typename T>
    bool operator() ( const T* const q1, const T* const t1,   const T* const q2, const T* const t2, T* residue_ptr ) const
    {

        // q1,t1 --> w_T_c1
        Eigen::Map<const Eigen::Matrix<T,3,1> > p_1( t1 );
        Eigen::Map<const Eigen::Quaternion<T> > q_1( q1 );

        // q2,t2 --> w_T_c2
        Eigen::Map<const Eigen::Matrix<T,3,1> > p_2( t2 );
        Eigen::Map<const Eigen::Quaternion<T> > q_2( q2 );

        // relative transforms between the 2 frames
        Quaternion<T> q_1_inverse = q_1.conjugate();
        Quaternion<T> q_12_estimated = q_1_inverse * q_2;
        Matrix<T,3,1> p_12_estimated = q_1_inverse * (p_2 - p_1);

        // compute error between orientations estimates
        Quaternion<T> delta_q = observed_c1_q_c2.cast<T>() * q_12_estimated.conjugate();
        Matrix<T,3,1> delta_t = observed_c1_t_c2.cast<T>() - p_12_estimated;

        // Eigen::Map<Matrix<T,6,1> > residuals( residue_ptr );
        // residuals.block(0,0,  3,1) = delta_t;
        // residuals.block(3,0,  3,1) = T(2.0) * delta_q.vec();
        residue_ptr[0] = delta_t[0,0];
        residue_ptr[1] = delta_t[1,0];
        residue_ptr[2] = delta_t[2,0];
        residue_ptr[3] = T(2.0) * delta_q.x();
        residue_ptr[4] = T(2.0) * delta_q.y();
        residue_ptr[5] = T(2.0) * delta_q.z();
        return true;


        ///////////// OLD implementation /////////////
/*
        // q1,t1 ---> w_T_c1
        Matrix<T,4,4> w_T_c1;
        Quaternion<T> quat1( q1[0], q1[1], q1[2], q1[3] );
        Matrix<T,3,1> trans1;
        trans1<< t1[0], t1[1], t1[2];
        w_T_c1 << quat1.toRotationMatrix(), trans1, T(0.), T(0.), T(0.), T(1.);

        // q2,t2 ---> w_T_c2
        Matrix<T,4,4> w_T_c2;
        Quaternion<T> quat2( q2[0], q2[1], q2[2], q2[3] );
        Matrix<T,3,1> trans2;
        trans2<< t2[0], t2[1], t2[2];
        w_T_c2 << quat2.toRotationMatrix(), trans2, T(0.), T(0.), T(0.), T(1.);

        // (w_T_c1).inverse() * w_T_c2
        Matrix<T,4,4> c1_T_c2 = w_T_c1.inverse() * w_T_c2;

        Matrix<T,3,3> M;
        M = c1_T_c2.topLeftCorner(3,3);
        Matrix<T,3,1> ypr = R2ypr( M );

        residue_ptr[0] = c1_T_c2(0,3) - T(observed__c1_T_c2(0,3));
        residue_ptr[1] = c1_T_c2(1,3) - T(observed__c1_T_c2(1,3));
        residue_ptr[2] = c1_T_c2(2,3) - T(observed__c1_T_c2(2,3));
        residue_ptr[3] = NormalizeAngle( ypr(0,0) - T(observed_c1_ypr_c2(0)) ) / T(10.);
        residue_ptr[4] = NormalizeAngle( ypr(1,0) - T(observed_c1_ypr_c2(1)) ) / T(10.);
        residue_ptr[5] = NormalizeAngle( ypr(2,0) - T(observed_c1_ypr_c2(2)) ) / T(10.);
        // residue[3] = ( ypr(0,0) - T(observed_c1_ypr_c2(0)) ) / T(5.);
        // residue[4] = ( ypr(1,0) - T(observed_c1_ypr_c2(1)) ) / T(5.);
        // residue[5] = ( ypr(2,0) - T(observed_c1_ypr_c2(2)) ) / T(5.);


        //// Dynamic Co-variance Scaling
        // T res_ = residue[0]*residue[0]+residue[1]*residue[1]+residue[2]*residue[2]+residue[3]*residue[3]+residue[4]*residue[4]+residue[5]*residue[5];
        // T m = T( 2.*5.) / ( T(5.) * T(0.01) * res_ );
        // residue[0] *= m;
        // residue[1] *= m;
        // residue[2] *= m;
        // residue[3] *= m;
        // residue[4] *= m;
        // residue[5] *= m;

        return true;*/
    }


    static ceres::CostFunction* Create( const Matrix4d& _observed__c1_T_c2 )
    {
      return ( new ceres::AutoDiffCostFunction<SixDOFError,6,4,3,4,3>
        (
          new SixDOFError(_observed__c1_T_c2)
        )
      );
    }


private:
    template <typename T>
    Matrix<T,3,1> R2ypr( const Matrix<T,3,3>& R )
    {
        Matrix<T,3,1> n = R.col(0);
        Matrix<T,3,1> o = R.col(1);
        Matrix<T,3,1> a = R.col(2);

       Matrix<T,3,1>ypr;
       T y = atan2(n(1), n(0));
       T p = atan2(-n(2), n(0) * cos(y) + n(1) * sin(y));
       T r = atan2(a(0) * sin(y) - a(1) * cos(y), -o(0) * sin(y) + o(1) * cos(y));
       ypr(0,0) = y;
       ypr(1,0) = p;
       ypr(2,0) = r;

       return ypr / T(M_PI) * T(180.0);
    }

    Matrix4d observed__c1_T_c2;
    Vector3d observed_c1_ypr_c2;
    Quaterniond observed_c1_q_c2;
    // Vector3d observed_c1_t_c2;
    Matrix<double,3,1> observed_c1_t_c2;
};
