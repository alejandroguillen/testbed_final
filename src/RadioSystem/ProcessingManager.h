/*
 * ProcessingManager.h
 *
 *  Created on: 17/jan/2015
 *      Author: Alejandro Guillen
 */

#ifndef PROCESSINGMANAGER_H_
#define PROCESSINGMANAGER_H_
#include <opencv2/opencv.hpp>
#include <vector>
#include "NodeManager/NodeManager.h"
#include "ProcessingSpeedEstimator.h"
#include "TxSpeedEstimator.h"
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <RadioSystem/LoadBalancing.h>

using namespace std;
using namespace cv;

#define COMPRESS_IMAGE 0
#define INITIAL_DETECTION_THRESHOLD 50

class Connection;
//class NodeManager;

typedef struct camera{
	Connection* connection;
	double bandwidth;
	double Pdpx;
	double Pdip;
	double Pe;
	double detTime;
	double descTime;
	double mergeTime;
	int kptsSize;

	double detection_threshold;
	int max_features;
	int destination;
	int id; 
	int slice_id;
	int slices_total;
	int sub_slices_total;
	
}camera;

typedef struct subslice{

	vector<uchar> data;
	int sub_slice_id;
	int sub_slices_total;
	Coordinate sub_slice_topleft;
	bool last_subslice_received;
	
}subslice;


typedef struct slices{

	int id; 
	bool last_subslices_iteration;
	int kpts_size;
	int features_size;
	int col_offset;
	double mergeTime;
	
}slices;

class ProcessingManager{
public:
	ProcessingManager(NodeManager* nm, int i);
	camera cameraList;
	void start();
	void addCameraData(DATC_param_t* datc_param_camera, DataCTAMsg* msg, Connection* c);
	void addSubSliceData(DataCTAMsg* msg);
	void Processing_thread_cooperator(int i);
	void storeKeypointsAndFeatures(int subslices_iteration,vector<KeyPoint>& kpts,Mat& features, double detTime, double descTime);

private:
	Mat mergeSubSlices(int subslices_iteration, vector<subslice> subsliceList);
	vector<subslice> getData(int subslices_iteration);
	void setData();

	int count_subslices;

	int frame_id;
	double next_detection_threshold;
	
	vector<subslice> subsliceList;
	vector<slices> sliceList;

	NodeManager* node_manager;
	
	VisualFeatureExtraction *extractor;
	VisualFeatureEncoding *encoder;
	VisualFeatureDecoding *decoder;
	
	vector<KeyPoint> keypoint_buffer;
	Mat features_buffer;
	
	int subslices_iteration;

	double processingTime;
	
	boost::thread p_thread;

	
	bool processcond;

	boost::mutex thread_mutex;
	boost::condition thread_condition;
	
	boost::mutex mutex;
	int dataavailable;
	bool last_subslice_received;
	
	boost::mutex second_mutex;
	boost::condition second_condition;

	double mergeTime;
		
};

#endif /* PROCESSINGMANAGER_H_ */
