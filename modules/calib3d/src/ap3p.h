#ifndef P3P_P3P_H
#define P3P_P3P_H

#include "precomp.hpp"

class ap3p {
 private:
  inline void polishQuarticRoots(const double *coeffs, double *roots);

  inline void vect_cross(const double *a, const double *b, double *result);

  inline double vect_dot(const double *a, const double *b);

  inline double vect_norm(const double *a);

  inline void vect_scale(const double s, const double *a, double *result);

  inline void vect_sub(const double *a, const double *b, double *result);

  inline void vect_divide(const double *a, const double d, double *result);

  inline void mat_mult(const double a[3][3], const double b[3][3], double result[3][3]);

  template<typename T>
  void init_camera_parameters(const cv::Mat &cameraMatrix) {
    cx = cameraMatrix.at<T>(0, 2);
    cy = cameraMatrix.at<T>(1, 2);
    fx = cameraMatrix.at<T>(0, 0);
    fy = cameraMatrix.at<T>(1, 1);
  }
  template<typename OpointType, typename IpointType>
  void extract_points(const cv::Mat &opoints, const cv::Mat &ipoints, std::vector<double> &points) {
    points.clear();
    points.resize(20);
    for (int i = 0; i < 4; i++) {
      points[i * 5] = ipoints.at<IpointType>(i).x * fx + cx;
      points[i * 5 + 1] = ipoints.at<IpointType>(i).y * fy + cy;
      points[i * 5 + 2] = opoints.at<OpointType>(i).x;
      points[i * 5 + 3] = opoints.at<OpointType>(i).y;
      points[i * 5 + 4] = opoints.at<OpointType>(i).z;
    }
  }
  void init_inverse_parameters();
  double fx, fy, cx, cy;
  double inv_fx, inv_fy, cx_fx, cy_fy;
 public:
  ap3p() {}
  ap3p(double fx, double fy, double cx, double cy);
  ap3p(cv::Mat cameraMatrix);

  bool solve(cv::Mat &R, cv::Mat &tvec, const cv::Mat &opoints, const cv::Mat &ipoints);
  int solve(double R[4][3][3], double t[4][3],
            double mu0, double mv0, double X0, double Y0, double Z0,
            double mu1, double mv1, double X1, double Y1, double Z1,
            double mu2, double mv2, double X2, double Y2, double Z2);
  bool solve(double R[3][3], double t[3],
             double mu0, double mv0, double X0, double Y0, double Z0,
             double mu1, double mv1, double X1, double Y1, double Z1,
             double mu2, double mv2, double X2, double Y2, double Z2,
             double mu3, double mv3, double X3, double Y3, double Z3);

  int computePoses(const double featureVectors[3][3], const double worldPoints[3][3], double solutionsR[4][3][3],
                   double solutionsT[4][3]);

  int solveQuartic(const double *factors, double *realRoots);
};


#endif //P3P_P3P_H
