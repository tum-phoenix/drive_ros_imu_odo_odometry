#include "drive_ros_imu_odo_odometry/imu_odo_odometry.h"

// constructor (initialize everything)
ImuOdoOdometry::ImuOdoOdometry(ros::NodeHandle& nh, ros::NodeHandle& pnh, bool use_bag):
  nh(nh), pnh(pnh), use_bag(use_bag)
{
  // queue for subscribers and sync policy
  int queue_size;

  // file path
  std::string bag_file_path, debug_out_file_path;

  // ros parameters
  pnh.param<int>("queue_size", queue_size, 5);
  pnh.param<std::string>("static_frame", static_frame, "odometry");
  pnh.param<std::string>("moving_frame", moving_frame, "rear_axis_middle_ground");
  pnh.param<bool>("ignore_acc_values", ignore_acc_values, false);
  pnh.param<bool>("use_sensor_time_for_pub", use_sensor_time_for_pub, false);

  pnh.param<std::string>("odo_topic_name", odo_topic_name, "/odo");
  pnh.param<std::string>("imu_topic_name", imu_topic_name, "/imu");

  pnh.param<std::string>("debug_out_file_path", debug_out_file_path, "/tmp/odom_debug.csv");
  pnh.param<bool>("debug_out", debug_out_file, false);

  float max_time_between_meas_fl;
  pnh.param<float>("max_time_between_meas", max_time_between_meas_fl, 0.5);
  max_time_between_meas = ros::Duration(max_time_between_meas_fl);

  // odometry publisher
  odo_pub = nh.advertise<nav_msgs::Odometry>(static_frame, 0);

  // debug file
  if(debug_out_file){
    writeOutputHeader(debug_out_file_path);
  }

  // input of messages (read from bag file or create real subscribers)
  if(use_bag){

    // fake subscriber
    odo_sub = new message_filters::Subscriber<nav_msgs::Odometry>();
    imu_sub = new message_filters::Subscriber<sensor_msgs::Imu>();


  }else{

    // real subscribers
    odo_sub = new message_filters::Subscriber<nav_msgs::Odometry>(pnh, odo_topic_name, queue_size);
    imu_sub = new message_filters::Subscriber<sensor_msgs::Imu>(pnh, imu_topic_name, queue_size);

  }

  // initialize policy and register sync callback
  policy = new SyncPolicy(queue_size);
  sync = new message_filters::Synchronizer<SyncPolicy>(static_cast<SyncPolicy>(*policy), *odo_sub, *imu_sub);
  sync->registerCallback(boost::bind(&ImuOdoOdometry::syncCallback, this, _1, _2));

  // parameters can be found here: http://wiki.ros.org/message_filters/ApproximateTime
  double age_penalty, odo_topic_rate, imu_topic_rate, max_time_between_imu_odo;
  pnh.param<double>("age_penalty", age_penalty, 300);
  pnh.param<double>("odo_topic_rate", odo_topic_rate, 300);
  pnh.param<double>("imu_topic_rate", imu_topic_rate, 300);
  pnh.param<double>("max_time_between_imu_odo", max_time_between_imu_odo, 0.1);

  policy->setAgePenalty(age_penalty);
  policy->setMaxIntervalDuration(ros::Duration(max_time_between_imu_odo));

  // lower bound should be half of the time period (= double the rate) for each topic
  policy->setInterMessageLowerBound(0, ros::Rate(odo_topic_rate*2).expectedCycleTime());
  policy->setInterMessageLowerBound(1, ros::Rate(imu_topic_rate*2).expectedCycleTime());

  // initialize Kalman filter state & covariances
  initFilterState();
  initFilterProcessCov();

  // init services
  reload_proc_cov = pnh.advertiseService("reload_proc_cov", &ImuOdoOdometry::svrReloadProcCov, this);
  reinit_state =    pnh.advertiseService("reinit_state",    &ImuOdoOdometry::svrReinitState,   this);

}

// deinitilize
ImuOdoOdometry::~ImuOdoOdometry()
{
  file_out_log.close();
}



// initialize Filter State
void ImuOdoOdometry::initFilterState()
{
  ROS_INFO("Reset Kalman State.");

  // Init kalman
  State s;
  s.setZero();
  filter.init(s);

  // Set initial state covariance
  Kalman::Covariance<State> stateCov;
  stateCov.setZero();

  if( pnh.getParam("kalman_cov/filter_init_var_x", stateCov(State::X, State::X)) &&
      pnh.getParam("kalman_cov/filter_init_var_y", stateCov(State::Y, State::Y)) &&
      pnh.getParam("kalman_cov/filter_init_var_a", stateCov(State::A, State::A)) &&
      pnh.getParam("kalman_cov/filter_init_var_v", stateCov(State::V, State::V)) &&
      pnh.getParam("kalman_cov/filter_init_var_theta", stateCov(State::THETA, State::THETA)) &&
      pnh.getParam("kalman_cov/filter_init_var_omega", stateCov(State::OMEGA, State::OMEGA)))
  {
    ROS_INFO("Kalman initial state covariance loaded successfully");
  }else{
    ROS_ERROR("Error loading Kalman initial state covariance!");
    throw std::runtime_error("Error loading parameters");
  }

  filter.setCovariance(stateCov);


  // reset initial times
  last_timestamp        = ros::Time(0);
  current_delta         = ros::Duration(0);
  last_delta            = ros::Duration(0);

}

// initialize Filter covariances
void ImuOdoOdometry::initFilterProcessCov()
{

  // Set process noise covariance
  Kalman::Covariance<State> cov;
  cov.setZero();

  if( pnh.getParam("kalman_cov/sys_var_x", cov(State::X, State::X)) &&
      pnh.getParam("kalman_cov/sys_var_y", cov(State::Y, State::Y)) &&
      pnh.getParam("kalman_cov/sys_var_a", cov(State::A, State::A)) &&
      pnh.getParam("kalman_cov/sys_var_v", cov(State::V, State::V)) &&
      pnh.getParam("kalman_cov/sys_var_theta", cov(State::THETA, State::THETA)) &&
      pnh.getParam("kalman_cov/sys_var_omega", cov(State::OMEGA, State::OMEGA)))
  {
    ROS_INFO("Kalman process covariances loaded successfully");
  }else{
    ROS_ERROR("Error loading Kalman process covariance!");
    throw std::runtime_error("Error loading parameters");
  }

  sys.setCovariance(cov);
}



// callback if both odo and imu messages with same timestamp have arrived
void ImuOdoOdometry::syncCallback(const nav_msgs::OdometryConstPtr &msg_odo,
                                  const sensor_msgs::ImuConstPtr &msg_imu)
{

  ROS_DEBUG_STREAM("Got new callback with times. IMU: " << msg_imu->header.stamp
                                           << " Odom: " << msg_odo->header.stamp
                                           << " Diff: " << msg_imu->header.stamp.toSec() - msg_odo->header.stamp.toSec());
  computeOdometry(msg_odo, msg_imu);

}


// calculates the odometry
void ImuOdoOdometry::computeOdometry(const nav_msgs::OdometryConstPtr &msg_odo,
                                     const sensor_msgs::ImuConstPtr &msg_imu)
{

  // check if timestamps are 0
  if(ros::Time(0) == msg_imu->header.stamp || ros::Time(0) == msg_odo->header.stamp)
  {
    ROS_WARN("A timestamp is 0. Skipping messages.");
    return;
  }

  // set timestamp
  current_timestamp = ros::Time((msg_imu->header.stamp.toSec() + msg_odo->header.stamp.toSec())/2);


  // check if this is first loop or reinitialized
  if(ros::Time(0) == last_timestamp){
    last_timestamp = current_timestamp;
    current_delta = ros::Duration(0);
  }else{
    current_delta = ( current_timestamp - last_timestamp );
  }


   // 1. compute measurement update
   computeMeasurement(msg_odo, msg_imu) &&

   // 2. compute Kalman filter step
   computeFilterStep() &&

   // 3. publish car state
   publishCarState();



  // save timestamp
  last_timestamp = current_timestamp;

}

bool ImuOdoOdometry::computeMeasurement(const nav_msgs::OdometryConstPtr &odo_msg,
                                        const sensor_msgs::ImuConstPtr &imu_msg)
{

  // time jump to big -> reset filter
  if(current_delta > max_time_between_meas){

    ROS_ERROR_STREAM("Delta Time Threshold exceeded. Reinit Filter."
        << " delta = " << current_delta
        << " thres = " << max_time_between_meas);

   // reset covariances
   initFilterProcessCov();
   return false;

  // jumping back in time
  }else if(current_delta < ros::Duration(0)) {
      ROS_WARN_STREAM("Jumping back in time."
          << " delta = " << current_delta.toSec());

      // reset times
      current_timestamp = last_timestamp;
      current_delta = ros::Duration(0);

      // go on and hope that the rest of the data is ok
  }

  // Set measurement covariances
  Kalman::Covariance<Measurement> cov;
  cov.setZero();
  cov(Measurement::AX,    Measurement::AX)    = imu_msg->linear_acceleration_covariance[CovElem::lin::linX_linX];
  cov(Measurement::AY,    Measurement::AY)    = imu_msg->linear_acceleration_covariance[CovElem::lin::linY_linY];
  cov(Measurement::OMEGA, Measurement::OMEGA) = imu_msg->angular_velocity_covariance[CovElem::ang::angZ_angZ];
  cov(Measurement::V,     Measurement::V)     = odo_msg->twist.covariance[CovElem::lin_ang::linX_linX];
  mm.setCovariance(cov);


  // set measurements vector z
  z.v()     = odo_msg->twist.twist.linear.x;
  z.omega() = imu_msg->angular_velocity.z;

  if(ignore_acc_values){
    z.ax()    = 0;
    z.ay()    = 0;
  }else{
    z.ax()    = imu_msg->linear_acceleration.x;
    z.ay()    = imu_msg->linear_acceleration.y;
  }


  ROS_DEBUG_STREAM("delta current: " << current_delta);
  ROS_DEBUG_STREAM("measurementVector: " << z);

  // check if there is something wrong
  if( std::isnan(cov(Measurement::AX,    Measurement::AX)   ) ||
      std::isnan(cov(Measurement::AY,    Measurement::AY)   ) ||
      std::isnan(cov(Measurement::V,     Measurement::V)    ) ||
      std::isnan(cov(Measurement::OMEGA, Measurement::OMEGA)) ||
      std::isnan(z.v()                                      ) ||
      std::isnan(z.ax()                                     ) ||
      std::isnan(z.ay()                                     ) ||
      std::isnan(z.omega()) )
  {
    ROS_ERROR("Measurement is NAN! Reinit Kalman.");
    // reset covariances and filter
    initFilterProcessCov();
    initFilterState();
    return false;
  }

  return true;
}

bool ImuOdoOdometry::computeFilterStep()
{
  // no new data avialable
  if(ros::Duration(0) == current_delta) {

      // use last delta
      u.dt() = last_delta.toSec();

      ROS_DEBUG_STREAM("Time delta is zero. Using old delta: " << last_delta);

  // new data available
  } else {

      // get current time delta
      u.dt() = current_delta.toSec();

      ROS_DEBUG_STREAM("Use Time delta of: " << current_delta);

      // save current delta
      last_delta = current_delta;

  }

  // predict state for current time-step using the kalman filter
  filter.predict(sys, u);

  // perform measurement update
  filter.update(mm, z);

  return true;
}

bool ImuOdoOdometry::publishCarState()
{
  // get new filter state
  const auto& state = filter.getState();
  ROS_DEBUG_STREAM("newState: " << state);

  // get new filter covariances
  const auto& cov_ft = filter.getCovariance();
  ROS_DEBUG_STREAM("FilterCovariance: " << cov_ft);

  // transform euler to quaternion angles
  tf2::Quaternion q1;
  q1.setRPY(0, 0, state.theta());

  // check if nan
  if( std::isnan(state.x())       ||
      std::isnan(state.y())       ||
      std::isnan(state.theta())   ||
      std::isnan(state.v())       ||
      std::isnan(state.a())       ||
      std::isnan(state.omega()))
  {
    ROS_ERROR("State is NAN! Reinit Kalman.");
    // reset covariances and filter
    initFilterProcessCov();
    initFilterState();
    return false;

  }

  // output time
  ros::Time out_time = ros::Time::now();
  if(use_sensor_time_for_pub)
  {
    out_time = current_timestamp;
  }

  // publish tf
  geometry_msgs::TransformStamped transformStamped;
  transformStamped.header.stamp = out_time;
  transformStamped.header.frame_id = static_frame;
  transformStamped.child_frame_id = moving_frame;
  transformStamped.transform.translation.x = state.x();
  transformStamped.transform.translation.y = state.y();
  transformStamped.transform.translation.z = 0;
  transformStamped.transform.rotation.x =  q1.x();
  transformStamped.transform.rotation.y =  q1.y();
  transformStamped.transform.rotation.z =  q1.z();
  transformStamped.transform.rotation.w =  q1.w();
  br.sendTransform(transformStamped);

  // publish odometry message
  nav_msgs::Odometry odom;
  odom.header.stamp = out_time;
  odom.header.frame_id = static_frame;
  odom.child_frame_id = moving_frame;

  // pose
  odom.pose.pose.position.x = state.x();
  odom.pose.pose.position.y = state.y();
  odom.pose.pose.position.z = 0;
  odom.pose.pose.orientation.x = q1.x();
  odom.pose.pose.orientation.y = q1.y();
  odom.pose.pose.orientation.z = q1.z();
  odom.pose.pose.orientation.w = q1.w();
  odom.pose.covariance[CovElem::lin_ang::linX_linX] = cov_ft(State::X,      State::X);
  odom.pose.covariance[CovElem::lin_ang::linX_linY] = cov_ft(State::X,      State::Y);
  odom.pose.covariance[CovElem::lin_ang::linX_angZ] = cov_ft(State::X,      State::THETA);
  odom.pose.covariance[CovElem::lin_ang::linY_linY] = cov_ft(State::Y,      State::Y);
  odom.pose.covariance[CovElem::lin_ang::linY_linX] = cov_ft(State::Y,      State::X);
  odom.pose.covariance[CovElem::lin_ang::linY_angZ] = cov_ft(State::Y,      State::THETA);
  odom.pose.covariance[CovElem::lin_ang::angZ_angZ] = cov_ft(State::THETA,  State::THETA);
  odom.pose.covariance[CovElem::lin_ang::angZ_linX] = cov_ft(State::THETA,  State::X);
  odom.pose.covariance[CovElem::lin_ang::angZ_linY] = cov_ft(State::THETA,  State::Y);

  // twist
  odom.twist.twist.linear.x = state.v();
  odom.twist.twist.angular.z = state.omega();
  odom.twist.covariance[CovElem::lin_ang::linX_linX] = cov_ft(State::V,      State::V);
  odom.twist.covariance[CovElem::lin_ang::linX_angZ] = cov_ft(State::V,      State::OMEGA);
  odom.twist.covariance[CovElem::lin_ang::angZ_angZ] = cov_ft(State::OMEGA,  State::OMEGA);
  odom.twist.covariance[CovElem::lin_ang::linX_angZ] = cov_ft(State::V,      State::OMEGA);

  odo_pub.publish(odom);


  // debug to file
  if(debug_out_file)
  {
    writeOutputResult(&odom);
  }

  return true;

}

