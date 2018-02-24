#ifndef IMUODOODOMETRY_H
#define IMUODOODOMETRY_H

// system
#include <cmath>
#include <mutex>
#include <thread>
#include <fstream>

// ros
#include <ros/ros.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <boost/foreach.hpp>
#include <tf/tf.h>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2/LinearMath/Quaternion.h>
#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

// ros messages
#include <geometry_msgs/TransformStamped.h>
#include <sensor_msgs/Imu.h>
#include <nav_msgs/Odometry.h>
#include "drive_ros_msgs/VehicleEncoder.h"
#include "drive_ros_msgs/TimeCompare.h"

// ros services
#include <std_srvs/Trigger.h>

// kalman
#include <kalman/ExtendedKalmanFilter.hpp>
#include "measurement_model.h"
#include "system_model.h"
#include "cov_elements.h"



class ImuOdoOdometry
{
public:
  // constructor
  ImuOdoOdometry(ros::NodeHandle& nh, ros::NodeHandle& pnh, bool use_bag);

  // destructor
  ~ImuOdoOdometry();

  // directly read from bag file
  bool processBag(std::string bag_file_path);

  // some typedefs
  typedef float T;

  typedef drive_ros_msgs::VehicleEncoder VeEnc;

  typedef CTRA::State<T> State;
  typedef CTRA::Control<T> Control;
  typedef CTRA::Measurement<T> Measurement;

  typedef CTRA::MeasurementModel<T> MeasurementModel;
  typedef CTRA::SystemModel<T> SystemModel;
  typedef Kalman::ExtendedKalmanFilter<State> Filter;

  typedef message_filters::sync_policies::ApproximateTime<nav_msgs::Odometry,
                                                          sensor_msgs::Imu> SyncPolicy;
private:

  // initialize Kalman Filter
  void initFilterState();
  void initFilterProcessCov();

  // computes the actual odometry
  void computeOdometry(const nav_msgs::OdometryConstPtr &msg_odo,
                       const sensor_msgs::ImuConstPtr &msg_imu);

  // collect data and prepare computing
  bool computeMeasurement(const nav_msgs::OdometryConstPtr &odo_msg,
                          const sensor_msgs::ImuConstPtr &imu_msg);

  // compute one kalman step
  bool computeFilterStep();

  // publish data
  bool publishCarState();

  // callback function for subscriber
  void syncCallback(const nav_msgs::OdometryConstPtr &msg_odo,
                    const sensor_msgs::ImuConstPtr &msg_imu);

  // services
  bool svrReloadProcCov(std_srvs::Trigger::Request  &req,
                           std_srvs::Trigger::Response &res);
  bool svrReinitState(std_srvs::Trigger::Request  &req,
                        std_srvs::Trigger::Response &res);

  // debug file operations
  void writeOutputHeader(std::string filename);
  void writeOutputResult(const nav_msgs::Odometry* msg);

  // ROS subscriber + synchronizer
  message_filters::Subscriber<sensor_msgs::Imu> *imu_sub;
  message_filters::Subscriber<nav_msgs::Odometry> *odo_sub;
  message_filters::Synchronizer<SyncPolicy> *sync;
  SyncPolicy* policy;

  // ROS publisher or broadcaster
  tf2_ros::TransformBroadcaster br;
  ros::Publisher odo_pub;
  ros::NodeHandle nh;
  ros::NodeHandle pnh;

  // services
  ros::ServiceServer reload_proc_cov;
  ros::ServiceServer reinit_state;

  // kalman filter stuff
  Control u;
  Measurement z;
  SystemModel sys;
  MeasurementModel mm;
  Filter filter;

  // ROS times and durations
  ros::Time last_timestamp;
  ros::Time current_timestamp;
  ros::Duration current_delta;
  ros::Duration last_delta;

  // parameter
  ros::Duration max_time_between_meas;
  bool ignore_acc_values;
  bool use_sensor_time_for_pub;
  std::string static_frame;
  std::string moving_frame;
  std::string odo_topic_name;
  std::string imu_topic_name;

  // debug to file
  bool debug_out_file;
  std::ofstream file_out_log;

  // bag file
  bool use_bag;

};

#endif
