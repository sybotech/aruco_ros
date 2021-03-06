/*****************************
Copyright 2011 Rafael Muñoz Salinas. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this list of
      conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice, this list
      of conditions and the following disclaimer in the documentation and/or other materials
      provided with the distribution.

THIS SOFTWARE IS PROVIDED BY Rafael Muñoz Salinas ''AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Rafael Muñoz Salinas OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those of the
authors and should not be interpreted as representing official policies, either expressed
or implied, of Rafael Muñoz Salinas.
********************************/
/**
* @file simple_single.cpp
* @author Bence Magyar
* @date June 2012
* @version 0.1
* @brief ROS version of the example named "simple" in the Aruco software package.
*/

#include <iostream>
#include <aruco/aruco.h>
#include <aruco/cvdrawingutils.h>
#include <aruco_msgs/MarkerArray.h>

#include <opencv2/core/core.hpp>
#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include <aruco_ros/aruco_ros_utils.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_listener.h>
#include <visualization_msgs/Marker.h>

using namespace aruco;

class ArucoSimple
{
private:
  cv::Mat inImage;
  aruco::CameraParameters camParam;
  tf::StampedTransform rightToLeft;
  bool useRectifiedImages;
  MarkerDetector mDetector;
  vector<Marker> markers;
  image_transport::Publisher image_pub;
  image_transport::Publisher debug_pub;
  ros::Publisher viz_pub; //rviz visualization marker
  ros::Publisher markers_pub;
  std::string reference_frame;
  tf::TransformBroadcaster br;

  /// Expected marker size
  double marker_size;

  image_transport::ImageTransport it;
  image_transport::CameraSubscriber camera_sub;

  tf::TransformListener _tfListener;

public:
  ArucoSimple(ros::NodeHandle & nh, ros::NodeHandle & private_nh)
    : it(nh)
  {

    std::string refinementMethod;
    private_nh.param<std::string>("corner_refinement", refinementMethod, "LINES");
    if ( refinementMethod == "SUBPIX" )
      mDetector.setCornerRefinementMethod(aruco::MarkerDetector::SUBPIX);
    else if ( refinementMethod == "HARRIS" )
      mDetector.setCornerRefinementMethod(aruco::MarkerDetector::HARRIS);
    else if ( refinementMethod == "NONE" )
      mDetector.setCornerRefinementMethod(aruco::MarkerDetector::NONE); 
    else      
      mDetector.setCornerRefinementMethod(aruco::MarkerDetector::LINES); 

    //Print parameters of aruco marker detector:
    ROS_INFO_STREAM("Corner refinement method: " << mDetector.getCornerRefinementMethod());
    ROS_INFO_STREAM("Threshold method: " << mDetector.getThresholdMethod());
    double th1, th2;
    mDetector.getThresholdParams(th1, th2);
    ROS_INFO_STREAM("Threshold method: " << " th1: " << th1 << " th2: " << th2);
    float mins, maxs;
    mDetector.getMinMaxSize(mins, maxs);
    ROS_INFO_STREAM("Marker size min: " << mins << "  max: " << maxs);
    ROS_INFO_STREAM("Desired speed: " << mDetector.getDesiredSpeed());

    private_nh.param<double>("marker_size", marker_size, 0.05);
    private_nh.param<std::string>("reference_frame", reference_frame, "");
    private_nh.param<bool>("image_is_rectified", useRectifiedImages, true);

    image_pub = it.advertise("result", 1);
    debug_pub = it.advertise("debug", 1);
    //pose_pub = nh.advertise<geometry_msgs::PoseStamped>("pose", 100);
    //transform_pub = nh.advertise<geometry_msgs::TransformStamped>("transform", 100);
    //position_pub = nh.advertise<geometry_msgs::Vector3Stamped>("position", 100);
    markers_pub = nh.advertise<aruco_msgs::MarkerArray>("aruco_markers", 100);
    viz_pub = nh.advertise<visualization_msgs::Marker>("visualization_markers", 10);
    //pixel_pub = nh.advertise<geometry_msgs::PointStamped>("pixel", 10);

    camera_sub = it.subscribeCamera("camera", 2, &ArucoSimple::camera_callback, this);

    ROS_INFO("Aruco node started with marker size of %f m", marker_size);
    //ROS_INFO("Aruco node will publish pose to TF with %s as parent and %s as child.", reference_frame.c_str(), marker_frame.c_str());
  }

  bool getTransform(const std::string& refFrame,
                    const std::string& childFrame,
                    tf::StampedTransform& transform)
  {
    std::string errMsg;

    if ( !_tfListener.waitForTransform(refFrame,
                                       childFrame,
                                       ros::Time(0),
                                       ros::Duration(0.5),
                                       ros::Duration(0.01),
                                       &errMsg)
         )
    {
      ROS_ERROR_STREAM("Unable to get pose from TF: " << errMsg);
      return false;
    }
    else
    {
      try
      {
        _tfListener.lookupTransform( refFrame, childFrame,
                                     ros::Time(0),                  //get latest available
                                     transform);
      }
      catch ( const tf::TransformException& e)
      {
        ROS_ERROR_STREAM("Error in lookupTransform of " << childFrame << " in " << refFrame);
        return false;
      }

    }
    return true;
  }

  // Publish a single marker
  void publish_marker(const Marker & marker, tf::StampedTransform cameraToReference)
  {
    ros::Time curr_stamp(ros::Time::now());
    tf::Transform transform = aruco_ros::arucoMarker2Tf(marker);

    /*
    tf::StampedTransform cameraToReference;
    cameraToReference.setIdentity();

    if ( reference_frame != camera_frame )
    {
      getTransform(reference_frame, camera_frame, cameraToReference);
    }*/

    transform = static_cast<tf::Transform>(cameraToReference)
      * static_cast<tf::Transform>(rightToLeft) * transform;

    char marker_name[255];
    sprintf(marker_name, "aruco_marker_%d", marker.id);

    tf::StampedTransform stampedTransform(transform, curr_stamp, reference_frame, marker_name);
    br.sendTransform(stampedTransform);
    geometry_msgs::PoseStamped poseMsg;
    tf::poseTFToMsg(transform, poseMsg.pose);
    poseMsg.header.frame_id = reference_frame;
    poseMsg.header.stamp = curr_stamp;
    /*
    pose_pub.publish(poseMsg);

    geometry_msgs::TransformStamped transformMsg;
    tf::transformStampedTFToMsg(stampedTransform, transformMsg);
    transform_pub.publish(transformMsg);

    geometry_msgs::Vector3Stamped positionMsg;
    positionMsg.header = transformMsg.header;
    positionMsg.vector = transformMsg.transform.translation;
    position_pub.publish(positionMsg);

    geometry_msgs::PointStamped pixelMsg;
    pixelMsg.header = transformMsg.header;
    pixelMsg.point.x = marker.getCenter().x;
    pixelMsg.point.y = marker.getCenter().y;
    pixelMsg.point.z = 0;
    pixel_pub.publish(pixelMsg);*/

    //Publish rviz marker representing the ArUco marker patch
    visualization_msgs::Marker visMarker;
    visMarker.header = poseMsg.header;
    visMarker.pose = poseMsg.pose;
    visMarker.id = marker.id;
    visMarker.type   = visualization_msgs::Marker::CUBE;
    visMarker.action = visualization_msgs::Marker::ADD;
    visMarker.pose = poseMsg.pose;
    visMarker.scale.x = marker_size;
    visMarker.scale.y = 0.001;
    visMarker.scale.z = marker_size;
    visMarker.color.r = 1.0;
    visMarker.color.g = 0;
    visMarker.color.b = 0;
    visMarker.color.a = 1.0;
    visMarker.lifetime = ros::Duration(3.0);
    viz_pub.publish(visMarker);
  }

  void publishMarkers(const tf::StampedTransform & cameraToReference)
  {
    // marker array publish
    aruco_msgs::MarkerArray msg;
    msg.markers.resize(markers.size());
    msg.header.stamp = cameraToReference.stamp_;
    msg.header.frame_id = cameraToReference.frame_id_;
    msg.header.seq++;

    for(size_t i=0; i<markers.size(); ++i)
    {
      aruco_msgs::Marker & marker_i = msg.markers[i];
      marker_i.header.stamp = cameraToReference.stamp_;
      marker_i.id = markers[i].id;
      marker_i.confidence = 1.0;
      tf::Transform transform = aruco_ros::arucoMarker2Tf(markers[i]);
      transform = static_cast<tf::Transform>(cameraToReference) * transform;
      tf::poseTFToMsg(transform, marker_i.pose.pose);
      marker_i.header.frame_id = reference_frame;
    }

    //publish marker array
    if (msg.markers.size() > 0)
      markers_pub.publish(msg);
  }

  void camera_callback(const sensor_msgs::ImageConstPtr& msg, const sensor_msgs::CameraInfoConstPtr& info_msg)
  {
    cam_info_callback(*info_msg);

    if(reference_frame.empty())
      reference_frame = msg->header.frame_id;

    tf::StampedTransform cameraToReference;
    cameraToReference.setIdentity();
    // Calculating transfrom from reference frame to camera frame
    if ( reference_frame != msg->header.frame_id )
    {
      getTransform(reference_frame, msg->header.frame_id, cameraToReference);
    }

    cameraToReference.stamp_ = msg->header.stamp;
    cameraToReference.frame_id_ = reference_frame;

    ros::Time curr_stamp(ros::Time::now());
    cv_bridge::CvImagePtr cv_ptr;
    try
    {
      cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::RGB8);
      inImage = cv_ptr->image;

      std::string camera_frame = msg->header.frame_id;

      //detection results will go into "markers"
      markers.clear();
      //Ok, let's detect
      mDetector.detect(inImage, markers, camParam, marker_size, false);
      //for each marker, draw info and its boundaries in the image
      for(size_t i=0; i<markers.size(); ++i)
      {
        publish_marker(markers[i], cameraToReference);
        // but drawing all the detected markers
        markers[i].draw(inImage,cv::Scalar(0,0,255),2);
      }

      this->publishMarkers(cameraToReference);

      //draw a 3d cube in each marker if there is 3d info
      if(camParam.isValid() && marker_size!=-1)
      {
        for(size_t i=0; i<markers.size(); ++i)
        {
          CvDrawingUtils::draw3dAxis(inImage, markers[i], camParam);
        }
      }

      if(image_pub.getNumSubscribers() > 0)
      {
        //show input with augmented information
        cv_bridge::CvImage out_msg;
        out_msg.header = msg->header;
        out_msg.encoding = sensor_msgs::image_encodings::RGB8;
        out_msg.image = inImage;
        image_pub.publish(out_msg.toImageMsg());
      }

      if(debug_pub.getNumSubscribers() > 0)
      {
        //show also the internal image resulting from the threshold operation
        cv_bridge::CvImage debug_msg;
        debug_msg.header = msg->header;
        debug_msg.encoding = sensor_msgs::image_encodings::MONO8;
        debug_msg.image = mDetector.getThresholdedImage();
        debug_pub.publish(debug_msg.toImageMsg());
      }
    }
    catch (cv_bridge::Exception& e)
    {
      ROS_ERROR("cv_bridge exception: %s", e.what());
      return;
    }
  }

  // wait for one camerainfo, then shut down that subscriber
  void cam_info_callback(const sensor_msgs::CameraInfo &msg)
  {
    camParam = aruco_ros::rosCameraInfo2ArucoCamParams(msg, useRectifiedImages);

    // handle cartesian offset between stereo pairs
    // see the sensor_msgs/CamaraInfo documentation for details
    rightToLeft.setIdentity();
    double ox = -msg.P[3]/msg.P[0];
    double oy = -msg.P[7]/msg.P[5];
    rightToLeft.setOrigin( tf::Vector3( ox, oy, 0.0));
  }
};


int main(int argc,char **argv)
{
  ros::init(argc, argv, "aruco_simple");

  ros::NodeHandle nh;
  ros::NodeHandle private_nh("~");
  ArucoSimple node(nh, private_nh);

  ros::spin();
}
