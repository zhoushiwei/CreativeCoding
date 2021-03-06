#ifndef FEATURE_ESTIMATION_H
#define FEATURE_ESTIMATION_H

#include "pcl.h"

#include "pcl/io/io.h"
#include "pcl/features/normal_3d.h"
#include "pcl/keypoints/sift_keypoint.h"
#include "pcl/features/fpfh.h"
#include "pcl/features/vfh.h"
#include "pcl/search/kdtree.h"

static pcl::search::KdTree<PointRgb>::Ptr search_method (new pcl::search::KdTree<PointRgb>);

/* Use NormalEstimation to estimate a cloud's surface normals 
* Inputs:
*   input
*     The input point cloud
*   radius
*     The size of the local neighborhood used to estimate the surface
* Return: A pointer to a SurfaceNormals point cloud
*/
SurfaceNormalsPtr
estimateSurfaceNormals (const PointCloudRgbPtr & input, float radius)
{ 
	pcl::NormalEstimation<PointRgb, NormalT> normal_estimation; 
	normal_estimation.setSearchMethod (search_method);
	normal_estimation.setRadiusSearch (radius);
	normal_estimation.setInputCloud (input);
	SurfaceNormalsPtr normals (new SurfaceNormals);
	normal_estimation.compute (*normals);

	return (normals);
}

/* Use SIFTKeypoint to detect a set of keypoints
* Inputs:
*   points
*     The input point cloud
*   normals
*     The input surface normals
*   min_scale
*     The smallest scale in the difference-of-Gaussians (DoG) scale-space
*   nr_octaves
*     The number of times the scale doubles in the DoG scale-space
*   nr_scales_per_octave
*     The number of scales computed for each doubling
*   min_contrast
*     The minimum local contrast that must be present for a keypoint to be detected
* Return: A pointer to a point cloud of keypoints
*/
PointCloudRgbPtr
detectKeypoints (const PointCloudRgbPtr & points, const SurfaceNormalsPtr & normals,
				 float min_scale, int nr_octaves, int nr_scales_per_octave, float min_contrast)
{
	pcl::SIFTKeypoint<PointRgb, pcl::PointWithScale> sift_detect;
	sift_detect.setSearchMethod (search_method);
	sift_detect.setScales (min_scale, nr_octaves, nr_scales_per_octave);
	sift_detect.setMinimumContrast (min_contrast);
	sift_detect.setInputCloud (points);
	pcl::PointCloud<pcl::PointWithScale> keypoints_temp;
	sift_detect.compute (keypoints_temp);
	PointCloudRgbPtr keypoints (new PointCloudRgb);
	pcl::copyPointCloud (keypoints_temp, *keypoints);

	return (keypoints);
}

/* Use FPFHEstimation to compute local feature descriptors around each keypoint
* Inputs:
*   points
*     The input point cloud
*   normals
*     The input surface normals
*   keypoints
*     A cloud of keypoints specifying the positions at which the descriptors should be computed
*   feature_radius
*     The size of the neighborhood from which the local descriptors will be computed 
* Return: A pointer to a LocalDescriptors (a cloud of LocalDescriptorT points)
*/
LocalDescriptorsPtr
computeLocalDescriptors (const PointCloudRgbPtr & points, const SurfaceNormalsPtr & normals, 
						 const PointCloudRgbPtr & keypoints, float feature_radius)
{
	pcl::FPFHEstimation<PointRgb, NormalT, LocalDescriptorT> fpfh_estimation;
	fpfh_estimation.setSearchMethod (search_method);
	fpfh_estimation.setRadiusSearch (feature_radius);
	fpfh_estimation.setSearchSurface (points);  
	fpfh_estimation.setInputNormals (normals);
	fpfh_estimation.setInputCloud (keypoints);
	LocalDescriptorsPtr local_descriptors (new LocalDescriptors);
	fpfh_estimation.compute (*local_descriptors);

	return (local_descriptors);
}

/* Use VFHEstimation to compute a single global descriptor for the entire input cloud
* Inputs:
*   points
*     The input point cloud
*   normals
*     The input surface normals
* Return: A pointer to a GlobalDescriptors point cloud (a cloud containing a single GlobalDescriptorT point)
*/
GlobalDescriptorsPtr
computeGlobalDescriptor (const PointCloudRgbPtr & points, const SurfaceNormalsPtr & normals)
{
	pcl::VFHEstimation<PointRgb, NormalT, GlobalDescriptorT> vfh_estimation;
	vfh_estimation.setSearchMethod(search_method);
	vfh_estimation.setInputCloud (points);
	vfh_estimation.setInputNormals (normals);
	GlobalDescriptorsPtr global_descriptor (new GlobalDescriptors);
	vfh_estimation.compute (*global_descriptor);

	return (global_descriptor);
}

/* A simple structure for storing all of a cloud's features */
struct ObjectFeatures
{
	PointCloudRgbPtr points;
	SurfaceNormalsPtr normals;
	PointCloudRgbPtr keypoints;
	LocalDescriptorsPtr local_descriptors;
	GlobalDescriptorsPtr global_descriptor;
};

/* Estimate normals, detect keypoints, and compute local and global descriptors 
* Return: An ObjectFeatures struct containing all the features
*/
ObjectFeatures
computeFeatures (const PointCloudRgbPtr & input)
{
	ObjectFeatures features;
	features.points = input;
	features.normals = estimateSurfaceNormals (input, 0.05);
	features.keypoints = detectKeypoints (input, features.normals, 0.005f, 10, 8, 1.5f);
	features.local_descriptors = computeLocalDescriptors (input, features.normals, features.keypoints, 0.1f);
	features.global_descriptor = computeGlobalDescriptor (input, features.normals);

	return (features);
}

#endif
