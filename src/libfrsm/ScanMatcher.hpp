/**
 * SECTION:frsm
 * @title: ScanMatcher
 * @short_description: Class for peforming scan matching of 2D laser scans
 *
 * A fast and accurate scan matching module for aligning 2D laser scans.
 *
 * The scan matcher maintains a sliding window local occupancy gridmap for laser scan-matching.
 *
 *
 * Linking: -lfrsm
 * namespace: frsm
 */

#ifndef _FRSM_H_
#define _FRSM_H_

#include "RasterLookupTable.hpp"
#include "Contour.hpp"
#include "Scan.hpp"
#include <vector>
#include <list>
#include <glib.h>
#include <float.h>

namespace frsm {
/*
 * matchingMode indicates which type of matching should be applied
 * FRSM_GRID_ONLY   - don't run coordinate ascent
 * FRSM_GRID_COORD  - run coordinate ascent after grid search (a safe default)
 * FRSM_Y_GRID_COORD- run coordinate ascent on y-theta for height matching after grid
 * FRSM_RUN_GRID    - dummy enum for separating the mode types
 * FRSM_COORD_ONLY  - only run the coordinate ascent
 * FRSM_Y_COORD_ONLY- only run the coordinate ascent on y-theta for height matching
 */
typedef enum {
  FRSM_GRID_ONLY = 0, FRSM_GRID_COORD = 1, FRSM_Y_GRID_COORD = 2, FRSM_RUN_GRID = 3, //dummy enum... don't use
  FRSM_COORD_ONLY = 4,
  FRSM_Y_COORD_ONLY = 5
//don't search in x (for height matching)
} frsm_incremental_matching_modes_t;

class ScanMatcher {
public:
  /*
   * ScanMatcher:
   * Constructor for the scan matching object.
   * Pass in a the matching parameters.
   * @metersPerPixel: Resolution for the local gridmap, and the xy resolution for the grid search
   * @thetaResolution_: theta resolution for the grid search
   * @useMultires_: perform multi resolution matching, with the specified downsampling factor.
   *          useMultires_=0, no downsampling
   *          useMultires_=1, downsample low res by factor of 2
   *          useMultires_=k, downsample low res by factor of 2^k
   *          normally, ~3 is a good value
   * @useThreads: using threading to make the occupancy map rebuilds occur in a background thread
   * @verbose: print out some status info
   */
  ScanMatcher(double metersPerPixel_, double thetaResolution_, int useMultires_, bool useThreads_, bool verbose_);

  /**
   * initSuccessiveMatching:
   * set the parameters for successive matching to other than the default:
   * @initialSearchRangeXY_: nominal search region in meters for the grid search in XY.
   *          If the best match is on the edge of the region,
   *          the window may be expanded up to the maxSearchRangeXY
   * @maxSearchRangeXY_: max XY window size
   * @initialSearchRangeTheta_: same, but for theta/radians
   * @maxSearchRangeTheta_: same, but for theta/radians
   * @matchingMode_: which matching mode to use
   * @addScanHitThresh_: add a scan to the local map if the percentage of points that are "hits" is below this threshold.
   * @stationaryMotionModel_: use a "stationary" motion model instead of the normal constant velocity one
   * @motionModelPriorWeight_: indicates the standard deviation of the motion model estimate.
   *                          If its less than 0.1, we don't use the priors for anything other
   *                          than centering the search window
   * @startPose: initialize pose to something other than (0,0)-0
   */
  void
  initSuccessiveMatchingParams(unsigned int maxNumScans_, double initialSearchRangeXY_, double maxSearchRangeXY_,
      double initialSearchRangeTheta_, double maxSearchRangeTheta_, frsm_incremental_matching_modes_t matchingMode_,
      double addScanHitThresh_, bool stationaryMotionModel_, double motionModelPriorWeight_, ScanTransform * startPose);

  virtual
  ~ScanMatcher();

  /**
   * matchSuccessive:
   * Match consecutive laser scans with each other.
   * Assumes the vehicle moves with a constant velocity between scans.
   * This method that should be called on successive incoming laser scans to perform "incremental" scan matching.
   *
   * @points: laser points (in body frame)
   * @numPoints: number of laser points
   * @laser_type: the type of laser that generated the points
   * @utime: timestamp for the laser reading
   * @preventAddScan: perform the matching, but don't allow the scan to be added to the map
   *          (used in case the vehicle is pitching/rolling too much)
   * @prior: If you have a better guess than constant velocity propogation (ie: from odometry)
   *          pass in an initial guess for the aligning position of the scan to be used as the center of the window.
   *          otherwise leave prior=NULL.
   *          The prior.score field indicates the standard deviation of the motion model estimate.
   *          If its less than 0.1, we don't use the prior for anything other than centering the search window,
   *          otherwise a gaussian prior with that standard deviation is used.
   * @xrange: x-range to be searched in meters
   * @yrange: y-range to be searched in meters
   * @thetarange: theta-range to be searched in radians
   *
   * Returns: a ScanTransform containing the best alignment for the input set of point
   */
  ScanTransform
  matchSuccessive(frsmPoint * points, unsigned numPoints, frsm_laser_type_t laser_type, int64_t utime, bool preventAddScan =
      false, ScanTransform * prior = NULL);
  /*
   * gridMatch:
   * match the set of points to the current map (perform Scan Matching)
   * this would be the interface to use for "loop-closing"
   *
   * @points: laser points (in body frame)
   * @numPoints: number of laser points
   * @prior: initial guess for the aligning position of the scan. Used as the center of the window
   *          The prior.score field indicates the standard deviation of the motion model estimate.
   *          If its less than 0.1, we don't use the prior for anything other than centering the search window,
   *          otherwise a gaussian prior with that standard deviation is used.
   * @xrange: x-range to be searched in meters
   * @yrange: y-range to be searched in meters
   * @thetarange: theta-range to be searched in radians
   * @xSat: return value for whether the best transform was on the x-edge of the search window
   * @ySat: return value for whether the best transform was on the y-edge of the search window
   * @thetaSat: return value for whether the best transform was on the theta-edge of the search window
   *
   * Returns: a ScanTransform containing the best alignment for the input set of point
   */
  ScanTransform
  gridMatch(frsmPoint * points, unsigned numPoints, ScanTransform * prior, double xRange, double yRange,
      double thetaRange, int * xSat = NULL, int *ySat = NULL, int * thetaSat = NULL);

  /*
   * coordAscentMatch:
   * Use gradient ascent to refine the resulting match...
   * should normally be done on the result from calling ScanMatcher::match()
   * to get a reasonable startPoint
   * @points: the set of points that make up the scan to be matched
   * @numPoints: number of points
   * @startPoint: initial guess of the alignment, needs to be close for gradient ascent to work
   *
   */
  ScanTransform
  coordAscentMatch(frsmPoint * points, unsigned numPoints, ScanTransform * startPoint);

  /*
   * addScanToBeProcessed:
   * add this scan to the queue to get processed in a background thread. This ensures
   * we can rebuild the map without disturbing the realtime processing thread
   * if useThreads is false, this just calls the regular addScan
   *
   * @points: the set of points that make up the scan
   * @numPoints: number of points
   * @T: the transform to project the points to the world coordinate frame
   * @laser_type: the type of laser that generated the points
   * @utime: timestamp for the laser reading
   *
   */
  void
  addScanToBeProcessed(frsmPoint * points, unsigned numPoints, ScanTransform * T, frsm_laser_type_t laser_type,
      int64_t utime);

  /*
   * addScan:
   * add this scan to the map immediately
   *
   * @points: the set of points that make up the scan
   * @numPoints: number of points
   * @T: the transform to project the points to the world coordinate frame
   * @laser_type: the type of laser that generated the points
   * @utime: timestamp for the laser reading
   * @rebuildNow: if true, rlt will be rebuilt, otherwise,
   *     the scan will just be added to the list without touching the occupancy map
   */
  void
  addScan(frsmPoint * points, unsigned numPoints, ScanTransform *T, frsm_laser_type_t laser_type, int64_t utime,
      bool rebuildNow = true);

  /*
   * addScan:
   * add this scan to the map immediately
   * @s: the Scan to be added
   * @rebuildNow: if true, rlt will be rebuilt, otherwise,
   *     the scan will just be added to the list without touching the occupancy map
   */
  void
  addScan(Scan *s, bool rebuildNow);

  /*numScans:
   * get the number of scans in currently being used
   */
  inline int numScans()
  {
    return scans.size();
  }

  /*
   * clearScans:
   * remove all the scans from the current local map
   * @deleteScans: also delete the contained Scan objects
   */
  void
  clearScans(bool deleteScans);

#ifndef NO_BOT_LCMGL
  /*
   * draw_func:
   */
  void draw_state_lcmgl(bot_lcmgl_t * lcmgl);
  void draw_scan_lcmgl(bot_lcmgl_t * lcmgl, frsmPoint * points, unsigned numPoints, const ScanTransform * T);

#endif

  /*
   * isUsingIPP:
   * check whether the scan matcher was compiled with IPP
   */
  int isUsingIPP();
private:
  /*
   * rebuildRaster:
   * rebuild rasterTable using the current set of scans
   */
  void
  rebuildRaster(RasterLookupTable ** rasterTable);

  void
  rebuildRaster_olson(RasterLookupTable ** rasterTable);
  void
  rebuildRaster_blur(RasterLookupTable ** rasterTable);
  void
  drawBlurredScan(RasterLookupTable * rt, Scan * s);
  void
  rebuildRaster_blurLine(RasterLookupTable ** rasterTable);
  /**
   * computeBouds:
   * compute the bounds of the currently stored set of scans
   */
  void
  computeBounds(double *minx, double *miny, double *maxx, double *maxy);

public:
  /*
   * General scan matching params:
   */
  /*
   * Resolution for the local gridmap, and the xy resolution for the grid search
   */
  double metersPerPixel;

  /*
   * theta resolution for the grid search
   */
  double thetaResolution;

  /*
   * Max number of scans to keep in the local sliding window map
   */
  unsigned int maxNumScans;

  /*
   * Threshold for considering a point a "hit" in the lookup table
   */
  int hitThresh;

  /*
   * use the mutli resolution search, should be faster for large search windows (and small, but not as big of a difference)
   */
  bool useMultiRes;
  /*
   * if using the multi-res search, low res table should be downsampled by this factor
   */
  int downsampleFactor;

  /*
   * rebuild the local occupancy map in a background thread.
   */
  bool useThreads;

  /*
   * successive matching params:
   * These only have an effect if using the matchSuccessive Interface
   */

  /*
   * add a scan to the local map if the percentage of points that are "hits" is below this threshold.
   */
  double addScanHitThresh;
  /**
   * default search range to be search over in xy
   */
  double initialSearchRangeXY;
  /**
   * If the best transform is on the edge of the window, the search range may be expanded up to this much
   * for the search over in xy
   */
  double maxSearchRangeXY;
  /**
   * default search range to be search over in theta
   */
  double initialSearchRangeTheta;
  /**
   * If the best transform is on the edge of the window, the search range may be expanded up to this much
   * for the search over in theta
   */
  double maxSearchRangeTheta;

  /*
   * indicates which type of matching should be applied
   */
  frsm_incremental_matching_modes_t matchingMode;

  /*
   * indicates weather we want to use the stationary motion model instead of the usual constant velocity motion model.
   */
  bool stationaryMotionModel;

  /*
   * indicates the standard deviation of the motion model estimate.
   * If we're using one of the above motion models.
   * If its less than 0.1, we don't use the prior for anything other than centering the search window
   */
  double motionModelPriorWeight;

  bool verbose;

  /*
   * current pose from successive matching
   */
  ScanTransform currentPose;
  /*
   * previous pose from successive matching
   */
  ScanTransform prevPose;

  /*
   * list of scans that are used to create the Raster Lookup table (occupancy map)
   */
  std::list<Scan *> scans;
  /*
   * The internal occupancy map
   */
  RasterLookupTable *rlt;

private:
  RasterLookupTable *rltTmp;

  RasterLookupTable *rlt_low_res;
  RasterLookupTable *rltTmp_low_res;

  ContourExtractor * contour_extractor;

  //stuff for creating raster table using my  line drawing primitive
  LutKernel * draw_kernels;

  //for creating the raster table using olson's method
  int lutSq_first_zero;
  unsigned char * lutSq;

  //threading stuff
  friend void * ScanMatcher_thread_wrapper_func(void * user);
  int killThread;
  int cancelAdd;
  GThread * rebuilder;
  void rebuildThreadFunc();
  std::list<Scan *> scansToBeProcessed;

  GMutex* scans_mutex; //this ordering must be obeyed... lock scans first!
  GMutex* rlt_mutex;
  GMutex* toBeProcessed_mutex; //shouldn't need to be locked alongside anything else...
  GCond* toBeProcessed_cv;

};


}

#endif /*_FRSM_H_*/
