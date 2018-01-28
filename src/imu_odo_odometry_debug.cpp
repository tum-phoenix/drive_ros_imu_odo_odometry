#include "drive_ros_imu_odo_odometry/imu_odo_odometry.h"

void ImuOdoOdometry::write_output_header(std::string filename)
{
  file_out_log.open( filename );

  file_out_log << "timestamp,";

  file_out_log << "pose_posX,"
               << "pose_posY,"
               << "pose_posZ,"
               << "pose_oriW,"
               << "pose_oriX,"
               << "pose_oriY,"
               << "pose_oriZ,";

  for(int i=0; i<36; i++)
    file_out_log << "pose_cov_[" << i << "],";

  file_out_log << "twist_linX,"
               << "twist_linY,"
               << "twist_linZ,"
               << "twist_angX,"
               << "twist_angY,"
               << "twist_angZ,";

  for(int i=0; i<36; i++)
    file_out_log << "twist_cov_[" << i << "],";

   file_out_log << std::endl;
}


void ImuOdoOdometry::write_output_result(const nav_msgs::Odometry *msg)
{
  file_out_log << msg->header.stamp.toSec() << ",";

  file_out_log << msg->pose.pose.position.x << ","
               << msg->pose.pose.position.y << ","
               << msg->pose.pose.position.z << ","
               << msg->pose.pose.orientation.w << ","
               << msg->pose.pose.orientation.x << ","
               << msg->pose.pose.orientation.y << ","
               << msg->pose.pose.orientation.z << ",";

  for(int i=0; i<36; i++)
    file_out_log << msg->pose.covariance.at(i) << ",";

  file_out_log << msg->twist.twist.linear.x << ","
               << msg->twist.twist.linear.y << ","
               << msg->twist.twist.linear.z << ","
               << msg->twist.twist.angular.x << ","
               << msg->twist.twist.angular.y << ","
               << msg->twist.twist.angular.z << ",";

  for(int i=0; i<36; i++)
    file_out_log << msg->twist.covariance.at(i) << ",";

  file_out_log << std::endl;
}


bool ImuOdoOdometry::processBag(std::string bag_file_path)
{
  // open bag
  rosbag::Bag bag;
  bag.open(bag_file_path, rosbag::bagmode::Read);

  // topics to load
  std::vector<std::string> bag_topics;
  bag_topics.push_back(odo_topic_name);
  bag_topics.push_back(imu_topic_name);

  // create bag view
  rosbag::View bag_view(bag, rosbag::TopicQuery(bag_topics));

  // loop over all messages
  BOOST_FOREACH(rosbag::MessageInstance const m, bag_view)
  {

    // odometer msg
    if(m.getTopic() == odo_topic_name || ("/" + m.getTopic() == odo_topic_name))
    {
      drive_ros_msgs::VehicleEncoderConstPtr odo = m.instantiate<drive_ros_msgs::VehicleEncoder>();
      if (odo != NULL){
        sync->add<0>(odo);
      }
    }

    // imu msg
    if(m.getTopic() == imu_topic_name || ("/" + m.getTopic() == imu_topic_name))
    {
      sensor_msgs::ImuConstPtr imu = m.instantiate<sensor_msgs::Imu>();
      if (imu != NULL){
        sync->add<1>(imu);
      }
    }

  }

  // close bag
  bag.close();
  return true;
}

