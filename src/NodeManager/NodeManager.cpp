#include <stdlib.h>
#include <iostream>
#include <vector>
#include <boost/bind.hpp>
#include "NodeManager/NodeManager.h"
#include "Tasks/Tasks.h"
#include "Messages/DataATCMsg.h"
#include "Messages/StartDATCMsg.h"
#include "Messages/CoopInfoMsg.h"
#include "Messages/ACKsliceMsg.h"
#include "Messages/AddCameraMsg.h"
#include "RadioSystem/OffloadingManager.h"
#include "RadioSystem/ProcessingManager.h"

#define NUM_COOP 2 //CHANGE number of Cooperators
#define NUM_CAMERAS 2 //CHANGE number of Cameras
using namespace std;

//todo: deadline timer for keeping the system active even if the start/stop message is not received


NodeManager::NodeManager(NodeType nt, string ID){
	node_type = nt;
	node_id = atoi(ID.c_str()); //ALEXIS
	
	switch(node_type){
	case SINK:{
		break;
	}
	case CAMERA:{
		imgAcq = new ImageAcquisition(0);
		//imgAcq = new ImageAcquisition(0,1024,768);

		count_frame=0;


		BRISK_detParams detPrms(60,4);
		BRISK_descParams dscPrms;

		extractor = new VisualFeatureExtraction();
		extractor->setDetector("BRISK", &detPrms);
		extractor->setDescriptor("BRISK",&dscPrms);

		encoder = new VisualFeatureEncoding();
		decoder = new VisualFeatureDecoding();

		offloading_manager = new OffloadingManager(this);

		break;
	}
	case COOPERATOR:{
		countsubslices.reserve(NUM_CAMERAS);
		firstprocess.reserve(NUM_CAMERAS);
		pixels_to_process.reserve(NUM_CAMERAS);

		cameras_finished.reserve(NUM_CAMERAS); //ALEXIS Cameras SYNC 19/02

		BRISK_detParams detPrms(60,4);
		
		BRISK_descParams dscPrms;
		extractor = new VisualFeatureExtraction();
		extractor->setDetector("BRISK", &detPrms);
		extractor->setDescriptor("BRISK",&dscPrms);

		encoder = new VisualFeatureEncoding();
		
		for(int i=0;i<NUM_CAMERAS;i++){

			processing_manager.push_back(new ProcessingManager(this,i));

			countsubslices[i]=0;
			firstprocess[i]=true;
			cameras_finished[i]=false;
			pixels_to_process[i]=0;
			
		}

		break;
	}
	default:
		break;
	}
	cur_state = IDLE;
	datc_param_camera.reserve(NUM_CAMERAS);
	received_notifications = 0;
	outgoing_msg_seq_num = 255;
	frame_id = -1;

	simulate=0; //ALEXIS SIMULATE 01/02
}

NodeType NodeManager::getNodeType(){
	return node_type;
}

void NodeManager::set_radioSystem(RadioSystem *rs){
	radioSystem_ptr = rs;
}
void NodeManager::set_taskManager(TaskManager *tm){
	taskManager_ptr = tm;
}

void NodeManager::set_s2gInterface(S2GInterface *s2g){
	s2gInterface_ptr = s2g;
}

void NodeManager::deleteMsg(Message *msg){
	delete(msg);
}

void NodeManager::notify_msg(Message *msg){
	cout << "NM: a message notification has been received:" << msg->getMessageType() << endl;

	boost::thread m_thread;
	switch(msg->getMessageType()){
	case START_CTA_MESSAGE:
	{
		switch(node_type){
		case SINK:
		{
			//just forward it (sendTestPacket will take care of deleting the msg)
			sendMessage(msg);
			break;
		}
		case CAMERA:
		{	
			//update system state and start cta processing
			cta_param.quality_factor = ((StartCTAMsg*)msg)->getQualityFactor();
			cta_param.num_slices = ((StartCTAMsg*)msg)->getNumSlices();

			//m_thread = boost::thread(&NodeManager::CTA_processing_thread, this);

			if(cur_state==IDLE){
				cur_state = ACTIVE;
				CTA_processing_thread();
			}
			delete(msg);
			break;
		}


		default:
			break;
		}

		break;
	}

	case START_ATC_MESSAGE:
	{
		//if sink, forward to destination camera
		switch(node_type){
		case SINK:
		{
			//just forward it (sendTestPacket will take care of deleting the msg)
			sendMessage(msg);
			break;
		}
		case CAMERA:
		{
			// copy ATC params
			atc_param.max_features =  ((StartATCMsg*)msg)->getMaxNumFeat();
			atc_param.det = ((StartATCMsg*)msg)->getDetectorType();
			atc_param.detection_threshold = ((StartATCMsg*)msg)->getDetectorThreshold();
			atc_param.desc = ((StartATCMsg*)msg)->getDescriptorType();
			atc_param.desc_length = ((StartATCMsg*)msg)->getDescriptorLength();

			atc_param.coding = ((StartATCMsg*)msg)->getCoding();
			atc_param.transmit_keypoints = ((StartATCMsg*)msg)->getTransferKpt();
			atc_param.transmit_orientation = ((StartATCMsg*)msg)->getTransferOrientation();
			atc_param.transmit_scale = ((StartATCMsg*)msg)->getTransferScale();

			atc_param.num_feat_per_block = ((StartATCMsg*)msg)->getNumFeatPerBlock();
						
			//if camera, start ATC processing with ATC params
			//m_thread = boost::thread(&NodeManager::ATC_processing_thread, this);
			if(cur_state == IDLE){
				cur_state = ACTIVE;
				ATC_processing_thread();
			}
			delete(msg);
			break;
		}
		default:
			break;
		}

		break;
	}

	case START_DATC_MESSAGE:
	{
		switch(node_type){
		case SINK:
		{
			//forward it
			sendMessage(msg);
			break;
		}
		case CAMERA:
		{
			
			//ALEXIS SIMULATION 03/02
			datc_param.max_features = 400;
			datc_param.det = 5;
			datc_param.detection_threshold = 40;
			datc_param.desc = 3;
			datc_param.desc_length = 512;

			datc_param.coding = 0;
			datc_param.transmit_keypoints = 1;
			datc_param.transmit_orientation = 1;
			datc_param.transmit_scale = 1;

			datc_param.num_feat_per_block = 20;
			datc_param.num_cooperators = NUM_COOP;

			//
			/* ORIGINAL 
			datc_param.max_features =  ((StartDATCMsg*)msg)->getMaxNumFeat();
			datc_param.det = ((StartDATCMsg*)msg)->getDetectorType();
			datc_param.detection_threshold = ((StartDATCMsg*)msg)->getDetectorThreshold();
			datc_param.desc = ((StartDATCMsg*)msg)->getDescriptorType();
			datc_param.desc_length = ((StartDATCMsg*)msg)->getDescriptorLength();

			datc_param.coding = ((StartDATCMsg*)msg)->getCoding();
			datc_param.transmit_keypoints = ((StartDATCMsg*)msg)->getTransferKpt();
			datc_param.transmit_orientation = ((StartDATCMsg*)msg)->getTransferOrientation();
			datc_param.transmit_scale = ((StartDATCMsg*)msg)->getTransferScale();

			datc_param.num_feat_per_block = ((StartDATCMsg*)msg)->getNumFeatPerBlock();
			datc_param.num_cooperators = ((StartDATCMsg*)msg)->getNumCooperators();
			*/
			
			int num_available_coop = offloading_manager->getNumAvailableCoop();
			if(cur_state==IDLE && num_available_coop >= ((StartDATCMsg*)msg)->getNumCooperators()){
				cur_state = ACTIVE;
				offloading_manager->transmitStartDATC((StartDATCMsg*)msg);
				DATC_processing_thread();
			}
			break;
		}
		case COOPERATOR:
		{
			int i = msg->getSource();//ALEXIS 14/12
			i--; //ALEXIS 16/12 VECTOR only 2 spaces, so it has to be 0 (camera1) or 1 (camera2)

			//ALEXIS adding vector: _camera[i]
			cout << "NM: Start saving DATC Parameters from Camera " << msg->getSource() << endl;
			
			//ALEXIS SIMULATION 03/02
			datc_param_camera[i].max_features = ((StartDATCMsg*)msg)->getMaxNumFeat();
			datc_param_camera[i].det = 5;
			datc_param_camera[i].detection_threshold = ((StartDATCMsg*)msg)->getDetectorThreshold();
			datc_param_camera[i].desc = 3;
			datc_param_camera[i].desc_length = 512;

			datc_param_camera[i].coding = 0;
			datc_param_camera[i].transmit_keypoints = 1;
			datc_param_camera[i].transmit_orientation = 1;
			datc_param_camera[i].transmit_scale = 1;

			datc_param_camera[i].num_feat_per_block = 20;
			datc_param_camera[i].num_cooperators = NUM_COOP;
			//
			
			/* ORIGINAL
			datc_param_camera[i].max_features = ((StartDATCMsg*)msg)->getMaxNumFeat();
			datc_param_camera[i].det = ((StartDATCMsg*)msg)->getDetectorType();
			datc_param_camera[i].detection_threshold = ((StartDATCMsg*)msg)->getDetectorThreshold();
			datc_param_camera[i].desc = ((StartDATCMsg*)msg)->getDescriptorType();
			datc_param_camera[i].desc_length = ((StartDATCMsg*)msg)->getDescriptorLength();

			datc_param_camera[i].coding = ((StartDATCMsg*)msg)->getCoding();
			datc_param_camera[i].transmit_keypoints = ((StartDATCMsg*)msg)->getTransferKpt();
			datc_param_camera[i].transmit_orientation = ((StartDATCMsg*)msg)->getTransferOrientation();
			datc_param_camera[i].transmit_scale = ((StartDATCMsg*)msg)->getTransferScale();

			datc_param_camera[i].num_feat_per_block = ((StartDATCMsg*)msg)->getNumFeatPerBlock();
			datc_param_camera[i].num_cooperators = ((StartDATCMsg*)msg)->getNumCooperators();
			*/
			cout << "NM: Saved DATC Parameters in Coop " << (msg)->getDestination() << endl; //ALEXIS 17/12 COUT
			//
			delete(msg);
		}
		}
		break;
	}

	case DATA_CTA_MESSAGE:
	{
		switch(node_type){
		case SINK:
		{
			s2gInterface_ptr->writeMsg(msg);
			break;
		}
		case COOPERATOR:{
			cout << "it's a DATA CTA message" << endl;

				std::set<Connection*> connections = radioSystem_ptr->getWiFiConnections();
				std::set<Connection*>::iterator it = connections.begin();
				int tmp = connections.size();
				int pos_camera = msg->getSource()-1;
				if(tmp>1){
				std::advance(it, pos_camera);
				}
				Connection* cn = *it;
				if(firstprocess[pos_camera]) processing_manager[pos_camera]->start();
				if(countsubslices[pos_camera]==0){
					processing_manager[pos_camera]->addCameraData(&datc_param_camera[pos_camera],(DataCTAMsg*)msg,cn);
					firstprocess[pos_camera]=false;
				}
				countsubslices[pos_camera]++;
				processing_manager[pos_camera]->addSubSliceData((DataCTAMsg*)msg);

				if(((DataCTAMsg*)msg)->getSliceNumber()==countsubslices[pos_camera]){
					countsubslices[pos_camera]=0;
				}	
				
			delete(msg);
			break;
		}
		}
		break;
	}

	case DATA_ATC_MESSAGE:
	{
		switch(node_type){
		case SINK:{
			s2gInterface_ptr->writeMsg(msg);
			break;
		}
		case CAMERA:{
			DATC_store_features((DataATCMsg*)msg);
			delete(msg);

			//radioSystem_ptr->startTelosbMsg(); //ALEXIS SIMULATION 03/02
			break;
		}
		}
		break;
	}

	case STOP_MESSAGE:
	{
		switch(node_type){
		case SINK:
		{
			//just forward it (sendTestPacket will take care of deleting the msg)
			sendMessage(msg);
			break;
		}
		case CAMERA:
		{
			cout << "NM: Stopping..." << endl;
			cur_state = IDLE;
			break;
		}
		}
		break;
	}
	case COOP_INFO_MESSAGE:
	{
		switch(node_type){
		case SINK:{
			s2gInterface_ptr->writeMsg(msg);
			break;
		}
		}
		break;
	}
	case ACK_SLICE_MESSAGE:
	{
		switch(node_type){
		case CAMERA:{
			offloading_manager->notifyACKslice(((ACKsliceMsg*)msg)->getFrameID(),msg->getTcpConnection());
			break;
		}
		}
		break;
	}

	/*//ALEXIS 11/01 ADD CAMERA MESSAGE
	case ADD_CAMERA_MESSAGE:
	{
		switch(node_type){
		case SINK:{
			s2gInterface_ptr->writeMsg(msg);
			break;
		}
		}
		break;
	}
	//*/
	//ALEXIS 19/02 Cameras SYNC
		case ADD_CAMERA_MESSAGE:
		{
			switch(node_type){
			case SINK:{
				s2gInterface_ptr->writeMsg(msg);
				break;
			}

			case CAMERA:{ //wake to do a StartDATCmsg again
				radioSystem_ptr->startTelosbMsg();
				delete(msg);
				break;
			}
			case COOPERATOR:{ //waits until arrive N cameras and sends a addcameramsg to the cameras to start again
				cameras_finished[msg->getSource()-1]=true;
				for(int j=0;j<NUM_CAMERAS;j++){
					if(cameras_finished[j]==false){
						delete(msg);
						return;
					}
				}
				for(int j=0;j<NUM_CAMERAS;j++){
					cameras_finished[j]=false;
				}
				AddCameraMessage(msg->getDestination());
				delete(msg);
				break;
			}
			}
			break;
		}
	//

	default:
	
		break;
		
	}
}


int NodeManager::notify_endTask(){
	cout << "NM: a end-task notification has been received" << endl;
	cur_task_finished.notify_all();

	return 0;
}

void NodeManager::sendMessage(Message* msg){
	Task *cur_task;
	outgoing_msg_seq_num++;
	msg->setSeqNum(outgoing_msg_seq_num);
	cur_task = new SendMessageTask(msg,radioSystem_ptr->getTelosb());
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the send_message_task" << endl;
	{
		boost::mutex::scoped_lock lk(cur_task->task_monitor);
		while(!cur_task->completed){
			cur_task_finished.wait(lk);
		}
	}
	cout << "NM: ended send_message_task!" << endl;
	delete((SendMessageTask*)cur_task);
}


void NodeManager::CTA_processing_thread(){

	cout << "NM: I'm entering the CTA_processing thread" << endl;
	Task *cur_task;

	// Acquire the image
	cur_task = new AcquireImageTask(imgAcq);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the acquire_image_task" << cur_task << endl;
	{
		boost::mutex::scoped_lock lk(cur_task->task_monitor);
		while(!cur_task->completed){
			//cout << "NM: entering in wait state. Status " << cur_task << " " << cur_task->completed << endl;
			cur_task_finished.wait(lk);
			received_notifications++;
		}
	}
	cout << "NM: ended acquire_image_task" << endl;
	Mat image = ((AcquireImageTask*)cur_task)->getImage();
	delete((AcquireImageTask*)cur_task);
	frame_id++;


	// Convert to gray-scale
	cout << "NM: Creating the convert_colorspace_task"  << endl;
	cur_task = new ConvertColorspaceTask(image,0);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the convert_colorspace_task " << cur_task << endl;
	{
		boost::mutex::scoped_lock lk(cur_task->task_monitor);
		while(!cur_task->completed){
			//cout << "NM: entering in wait state. Status " << cur_task << " " << cur_task->completed << endl;
			cur_task_finished.wait(lk);
			received_notifications++;
		}
	}
	cout << "NM: ended convert_colorspace_task" << endl;
	image = ((ConvertColorspaceTask*)cur_task)->getConvertedImage();
	delete((ConvertColorspaceTask*)cur_task);

	for(int i=0;i<cta_param.num_slices;i++){
		cout << "NM: Creating the slice_image_task" << endl;
		cur_task = new SliceImageTask(image,Point(0,i*480/cta_param.num_slices),Point(640,(i+1)*480/cta_param.num_slices));
		taskManager_ptr->addTask(cur_task);
		cout << "NM: Waiting the end of the slice_image_task" << cur_task << endl;
		{
			boost::mutex::scoped_lock lk(cur_task->task_monitor);
			while(!cur_task->completed){
				//cout << "NM: entering in wait state. Status " << cur_task << " " << cur_task->completed << endl;
				cur_task_finished.wait(lk);
				received_notifications++;
			}
		}
		cout << "NM: ended slice_image_task" << endl;
		Mat slice = ((SliceImageTask*)cur_task)->getSlice();
		delete((SliceImageTask*)cur_task);


		//Encode with jpeg
		cout << "NM: encoding the slice with jpeg" << endl;
		cur_task = new EncodeSliceTask(slice,cta_param.quality_factor);
		taskManager_ptr->addTask(cur_task);
		cout << "NM: Waiting the end of the encode_image_task" << endl;
		{
			boost::mutex::scoped_lock lk(cur_task->task_monitor);
			while(!cur_task->completed){
				//cout << "NM: entering in wait state" << endl;
				cur_task_finished.wait(lk);
				received_notifications++;
			}
		}
		cout << "NM: ended encode_slice_task" << endl;
		vector<uchar> slice_bitstream;
		slice_bitstream = ((EncodeSliceTask*)cur_task)->getJpegBitstream();
		double enc_time = ((EncodeSliceTask*)cur_task)->getEncodingTime();
		delete((EncodeSliceTask*)cur_task);
		cout << "bitstream size of slice is " << slice_bitstream.size() << endl;


		Coordinate_t top_left;
		top_left.xCoordinate = 0;
		top_left.yCoordinate = (480/cta_param.num_slices)*i;
		DataCTAMsg *msg = new DataCTAMsg(frame_id,i,top_left,slice_bitstream.size(),enc_time,0,slice_bitstream);
		
		//ALEXIS Source from camera
		msg->setSource(node_id);
		
		msg->setDestination(0);
		sendMessage(msg);
		cur_state = IDLE;

	}
	cout << "NM: exiting the CTA thread TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT" << endl;
	cout << "Rec notifications: " << received_notifications << ", Sent notifications: " << taskManager_ptr->sent_notifications << endl;

}

void NodeManager::ATC_processing_thread(){


	cout << "NM: I'm entering the ATC_processing thread " << endl;

	boost::mutex monitor;
	boost::mutex::scoped_lock lk(monitor);
	Task *cur_task;

	// Acquire the image
	cur_task = new AcquireImageTask(imgAcq);
	taskManager_ptr->addTask(cur_task);
	//cout << "NM: Waiting the end of the acquire_image_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended acquire_image_task" << endl;
	Mat image = ((AcquireImageTask*)cur_task)->getImage();
	delete((AcquireImageTask*)cur_task);
	frame_id++;

	// Convert to gray-scale
	cur_task = new ConvertColorspaceTask(image,0);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the convert_colorspace_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended convert_colorspace_task" << endl;
	image = ((ConvertColorspaceTask*)cur_task)->getConvertedImage();
	delete((ConvertColorspaceTask*)cur_task);

	// Extract the keypoints
	cur_task = new ExtractKeypointsTask(extractor,image,atc_param.detection_threshold);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the extract_keypoints_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended extract_keypoints_task" << endl;

	vector<KeyPoint> kpts = ((ExtractKeypointsTask*)cur_task)->getKeypoints();
	double detTime = ((ExtractKeypointsTask*)cur_task)->getDetTime();
	cout << "extracted " << (int)kpts.size() << "keypoints" << endl;

	delete((ExtractKeypointsTask*)cur_task);

	//Extract features
	std::cout<<std::dec;
	cur_task = new ExtractFeaturesTask(extractor,image,kpts,atc_param.max_features);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the extract_features_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended extract_features_task" << endl;
	Mat features = ((ExtractFeaturesTask*)cur_task)->getFeatures();
	double descTime = ((ExtractFeaturesTask*)cur_task)->getDescTime();
	kpts = ((ExtractFeaturesTask*)cur_task)->getKeypoints();
	cout << "now extracted " << (int)kpts.size() << "keypoints" << endl;
	delete((ExtractFeaturesTask*)cur_task);

	if(kpts.size()>0){

		int num_blocks = ceil((float)kpts.size() / (float)atc_param.num_feat_per_block);
		int beg,end;

		cout << "features per block: " << (int)atc_param.num_feat_per_block << endl;

		for(int i=0; i<num_blocks; i++){

			vector<uchar> block_ft_bitstream;
			vector<uchar> block_kp_bitstream;

			beg = i*atc_param.num_feat_per_block;
			end = min((int)(unsigned int)kpts.size(), (int)(beg+atc_param.num_feat_per_block));

			Mat features_sub = features.rowRange(beg,end);
			double fencTime;
			if(atc_param.coding == CodingChoices_none){
				cur_task = new EncodeFeaturesTask(encoder,"BRISK",features_sub,0);
				taskManager_ptr->addTask(cur_task);
				//cout << "NM: Waiting the end of the encode_features_task" << endl;
				while(!cur_task->completed){
					cur_task_finished.wait(lk);
				}
				//cout << "NM: ended encode_features_task" << endl;
				block_ft_bitstream = ((EncodeFeaturesTask*)cur_task)->getFeatsBitstream();
				fencTime = ((EncodeFeaturesTask*)cur_task)->getEncodingTime();
				delete((EncodeFeaturesTask*)cur_task);
			}
			if(atc_param.coding == CodingChoices_entropyCoding){
				cur_task = new EncodeFeaturesTask(encoder,"BRISK",features_sub,1);
				taskManager_ptr->addTask(cur_task);
				//cout << "NM: Waiting the end of the encode_features_task" << endl;
				while(!cur_task->completed){
					cur_task_finished.wait(lk);
				}
				//cout << "NM: ended encode_features_task" << endl;
				block_ft_bitstream = ((EncodeFeaturesTask*)cur_task)->getFeatsBitstream();
				fencTime = ((EncodeFeaturesTask*)cur_task)->getEncodingTime();
				delete((EncodeFeaturesTask*)cur_task);
			}

			vector<KeyPoint> sub_kpts(&kpts[beg],&kpts[end]);
			cur_task = new EncodeKeypointsTask(encoder,sub_kpts,640,480,true,1);
			taskManager_ptr->addTask(cur_task);
			//cout << "NM: Waiting the end of the encode_kpts_task" << endl;
			while(!cur_task->completed){
				cur_task_finished.wait(lk);
			}
			//cout << "NM: ended encode_kpts_task" << endl;
			block_kp_bitstream = ((EncodeKeypointsTask*)cur_task)->getKptsBitstream();
			double kencTime = ((EncodeKeypointsTask*)cur_task)->getEncodingTime();
			delete((EncodeKeypointsTask*)cur_task);
			cout << "sending " << (int)(sub_kpts.size()) << "keypoints" << endl;
			cout << "and " << (int)(features_sub.rows) << "features" << endl;


			DataATCMsg *msg = new DataATCMsg(frame_id, i, num_blocks, detTime, descTime, kencTime, fencTime, 0, features_sub.rows, sub_kpts.size(), block_ft_bitstream, block_kp_bitstream);
			
			//ALEXIS Source from camera
			msg->setSource(node_id);
			
			msg->setDestination(0);

			sendMessage(msg);

			cout << "first keypoint: " << sub_kpts[0].pt.x << " " << sub_kpts[0].pt.y << endl;

		}
	}
	cur_state = IDLE;
	cout << "NM: exiting the ATC thread QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ" << endl;


}

void NodeManager::DATC_processing_thread(){
	cout << "NM: I'm entering the DATC_processing thread " << endl;

	boost::mutex monitor;
	boost::mutex::scoped_lock lk(monitor);
	Task *cur_task;

	count_frame++;
	// Acquire the image
	//cur_task = new AcquireFrameTask(imgAcq,count_frame);
	cur_task = new AcquireImageTask(imgAcq);
	taskManager_ptr->addTask(cur_task);
	//cout << "NM: Waiting the end of the acquire_image_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended acquire_image_task" << endl;
	Mat image = ((AcquireFrameTask*)cur_task)->getImage();
	delete((AcquireFrameTask*)cur_task);

	// Convert to gray-scale
	cur_task = new ConvertColorspaceTask(image,0);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the convert_colorspace_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended convert_colorspace_task" << endl;
	image = ((ConvertColorspaceTask*)cur_task)->getConvertedImage();
	delete((ConvertColorspaceTask*)cur_task);

	//create offloading task
	offloading_manager->createOffloadingTask(datc_param.num_cooperators, datc_param.max_features);

	//compute loads (should become a task)
	//here one should check what kind of offloading algorithm should be called
	cur_task = new ComputeLoadsTask(offloading_manager,image);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the compute_loads_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended compute_loads_task" << endl;
	Mat myLoad = ((ComputeLoadsTask*)cur_task)->getMyLoad();
	delete((ComputeLoadsTask*)cur_task);

	//transmit loads (should become a task)
	cur_task = new TransmitLoadsTask(offloading_manager);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the transmit_loads_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended transmit_loads_task" << endl;
	delete((TransmitLoadsTask*)cur_task);

	// Extract the keypoints of own load
	cur_task = new ExtractKeypointsTask(extractor,myLoad,atc_param.detection_threshold);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the extract_keypoints_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended extract_keypoints_task" << endl;

	vector<KeyPoint> kpts = ((ExtractKeypointsTask*)cur_task)->getKeypoints();
	double detTime = ((ExtractKeypointsTask*)cur_task)->getDetTime();
	cout << "extracted " << (int)kpts.size() << "keypoints" << endl;

	delete((ExtractKeypointsTask*)cur_task);

	//Extract features
	cur_task = new ExtractFeaturesTask(extractor,myLoad,kpts,atc_param.max_features);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the extract_features_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended extract_features_task" << endl;
	Mat features = ((ExtractFeaturesTask*)cur_task)->getFeatures();
	kpts = ((ExtractFeaturesTask*)cur_task)->getKeypoints();
	double descTime =  ((ExtractFeaturesTask*)cur_task)->getDescTime();
	cout << "now extracted " << (int)kpts.size() << " keypoints" << endl;
	delete((ExtractFeaturesTask*)cur_task);

	//put the unencoded features somewhere...
	//todo: understand what to do with encoding times

	offloading_manager->addKeypointsAndFeatures(kpts,features,null,detTime,descTime,0,0,0);
}



void NodeManager::DATC_processing_thread_cooperator(int i, vector<uchar> data, Connection* c, double detection_threshold, int max_features){
	cout << "NM: I'm entering the DATC_processing thread " << endl;

	
	boost::mutex monitor;
	boost::mutex::scoped_lock lk(monitor);
	Task *cur_task;
	//decode the image (should become a task)
	cv::Mat slice;
	/*
	OCTET_STRING_t oct_data = msg->getData();
	uint8_t* imbuf = oct_data.buf;
	int data_size = oct_data.size;
	vector<uchar> jpeg_bitstream;
	for(int i=0;i<data_size;i++){
		jpeg_bitstream.push_back(imbuf[i]);
	}
	*/
	slice = imdecode(data,CV_LOAD_IMAGE_GRAYSCALE);

	//send ACK_SLICE_MESSAGE
	cout << "Sending ACK_SLICE_MESSAGE" << endl;
	ACKsliceMsg *ackslice_msg = new ACKsliceMsg(frame_id);
	ackslice_msg->setTcpConnection(c);
	ackslice_msg->setSource(node_id);
	ackslice_msg->setDestination(i+1);
	//
	cur_task = new SendWiFiMessageTask(ackslice_msg);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the send_wifi_message_task" << endl;
	{
		boost::mutex::scoped_lock lk(cur_task->task_monitor);
		while(!cur_task->completed){
			cur_task_finished.wait(lk);
		}
	}
	cout << "NM: exiting the wifi tx thread" << endl;
	delete((SendWiFiMessageTask*)cur_task);
	
	double processingTime = getTickCount();

	// Extract the keypoints
	cur_task = new ExtractKeypointsTask(extractor,slice,detection_threshold);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the extract_keypoints_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended extract_keypoints_task" << endl;

	vector<KeyPoint> kpts = ((ExtractKeypointsTask*)cur_task)->getKeypoints();
	double detTime = ((ExtractKeypointsTask*)cur_task)->getDetTime();
	cout << "extracted " << (int)kpts.size() << "keypoints" << endl;
cerr << "extracted " << (int)kpts.size() << "keypoints\tDetThreshold=" << detection_threshold << endl;

	delete((ExtractKeypointsTask*)cur_task);

	//Extract features
	std::cout<<std::dec;
	cur_task = new ExtractFeaturesTask(extractor,slice,kpts,max_features);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the extract_features_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended extract_features_task" << endl;
	Mat features = ((ExtractFeaturesTask*)cur_task)->getFeatures();
	kpts = ((ExtractFeaturesTask*)cur_task)->getKeypoints();
	double descTime = ((ExtractFeaturesTask*)cur_task)->getDescTime();
	cout << "now extracted " << (int)kpts.size() << " keypoints" << endl;
	delete((ExtractFeaturesTask*)cur_task);

	//features serialization
	vector<uchar> ft_bitstream;
	vector<uchar> kp_bitstream;
	cur_task = new EncodeFeaturesTask(encoder,"BRISK",features,0);
	taskManager_ptr->addTask(cur_task);
	//cout << "NM: Waiting the end of the encode_features_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	//cout << "NM: ended encode_features_task" << endl;
	ft_bitstream = ((EncodeFeaturesTask*)cur_task)->getFeatsBitstream();
	double fencTime = ((EncodeFeaturesTask*)cur_task)->getEncodingTime();
	delete((EncodeFeaturesTask*)cur_task);

	cur_task = new EncodeKeypointsTask(encoder,kpts,640,480,true,0);
	taskManager_ptr->addTask(cur_task);
	//cout << "NM: Waiting the end of the encode_kpts_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	//cout << "NM: ended encode_kpts_task" << endl;
	kp_bitstream = ((EncodeKeypointsTask*)cur_task)->getKptsBitstream();
	double kencTime = ((EncodeKeypointsTask*)cur_task)->getEncodingTime();
	delete((EncodeKeypointsTask*)cur_task);
	cout << "sending " << (int)(kpts.size()) << "keypoints" << endl;
	cout << "and " << (int)(features.rows) << "features" << endl;
	
	processingTime = (getTickCount()-processingTime)/getTickFrequency();
	
	DataATCMsg *atc_msg = new DataATCMsg(frame_id, 0, 1, detTime, descTime, kencTime, processingTime, 0, features.rows, kpts.size(), ft_bitstream, kp_bitstream);
	atc_msg->setTcpConnection(c);
	atc_msg->setSource(node_id);
	atc_msg->setDestination(i+1);
	cur_task = new SendWiFiMessageTask(atc_msg);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the send_wifi_message_task" << endl;
	{
		boost::mutex::scoped_lock lk(cur_task->task_monitor);
		while(!cur_task->completed){
			cur_task_finished.wait(lk);
		}
	}
	cout << "NM: exiting the wifi tx thread" << endl;
	delete((SendWiFiMessageTask*)cur_task);

	//cur_state = IDLE;
}

void NodeManager::DATC_store_features(DataATCMsg* msg){

	boost::mutex monitor;
	boost::mutex::scoped_lock lk(monitor);
	Task *cur_task;
	cv::Mat features;
	vector<cv::KeyPoint> keypoints;

	//get the features from the message
	//may become a method of the message....
	//get features
	OCTET_STRING_t oct_data_ft = ((DataATCMsg*)msg)->getFeaturesData();
	uint8_t* ftbuf = oct_data_ft.buf;
	int data_size = oct_data_ft.size;
	vector<uchar> ft_bitstream;
	for(int i=0;i<data_size;i++){
		ft_bitstream.push_back(ftbuf[i]);
	}

	//get keypoints
	OCTET_STRING_t oct_data_kp = ((DataATCMsg*)msg)->getKeypointsData();
	uint8_t* kpbuf = oct_data_kp.buf;
	data_size = oct_data_kp.size;
	vector<uchar> kp_bitstream;
	for(int i=0;i<data_size;i++){
		kp_bitstream.push_back(kpbuf[i]);
	}

	cur_task = new DecodeKeypointsTask(decoder,kp_bitstream,640,480,true,0);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the decode_kpts_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended decode_kpts_task" << endl;
	keypoints = ((DecodeKeypointsTask*)cur_task)->getKeypoints();
	delete((DecodeKeypointsTask*)cur_task);

	cur_task = new DecodeFeaturesTask(decoder,"BRISK",ft_bitstream,0,keypoints.size());
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the decode_features_task" << endl;
	while(!cur_task->completed){
		cur_task_finished.wait(lk);
	}
	cout << "NM: ended decode_features_task" << endl;
	features = ((DecodeFeaturesTask*)cur_task)->getFeatures();
	delete((DecodeFeaturesTask*)cur_task);

	//put the features somewhere
	offloading_manager->addKeypointsAndFeatures(keypoints,features,msg->getTcpConnection(),
			msg->getDetTime(),msg->getDescTime(),msg->getKptsEncodingTime(),msg->getFeatEncodingTime(),(int)msg->getTxTime());

}

void NodeManager::notifyCooperatorOnline(Connection* cn){
	offloading_manager->addCooperator(cn);
	//ALEXIS SIMULATE 01/02
	simulate++;
	if(simulate==NUM_COOP){ //simulate == number of Coops to wait to start msg

		radioSystem_ptr->startTelosbMsg();
	}
	//
	std::string ip_addr = cn->socket().remote_endpoint().address().to_string();
	int port = cn->socket().remote_endpoint().port();
	CoopInfoMsg *msg = new CoopInfoMsg(ip_addr,port,CoopStatus_online);
	
	msg->setSource(node_id);
	msg->setDestination(0);

	//sendMessage(msg);	//ALEXIS SIMULATE 01/02
}

void NodeManager::notifyCooperatorOffline(Connection* cn){
	offloading_manager->removeCooperator(cn);
	std::string ip_addr = cn->socket().remote_endpoint().address().to_string();
	int port = cn->socket().remote_endpoint().port();
	CoopInfoMsg *msg = new CoopInfoMsg(ip_addr,port,CoopStatus_offline);
	
	//ALEXIS 11/12
	msg->setSource(node_id);
	msg->setDestination(0);
	//
	sendMessage(msg);
}

void NodeManager::notifyOffloadingCompleted(vector<KeyPoint>& kpts,Mat& features, double camDetTime, double camDescTime){

	boost::mutex monitor;
	boost::mutex::scoped_lock lk(monitor);
	Task *cur_task;
	//prepare the ATC_DATA message and sends it to the SINK
	//ugly... put into a task!
	cout << "cutting: kpts size "<< kpts.size() << " feat size " << features.rows << endl;
	extractor->cutFeatures(kpts,features,datc_param.max_features);
	if(kpts.size()>0){

		int num_blocks = ceil((float)kpts.size() / (float)datc_param.num_feat_per_block);
		int beg,end;

		cout << "features per block: " << (int)datc_param.num_feat_per_block << endl;

		for(int i=0; i<num_blocks; i++){

			vector<uchar> block_ft_bitstream;
			vector<uchar> block_kp_bitstream;

			beg = i*datc_param.num_feat_per_block;
			end = min((int)(unsigned int)kpts.size(), (int)(beg+datc_param.num_feat_per_block));

			cout << "here" << beg << " "<< end << endl;
			Mat features_sub = features.rowRange(beg,end);

			if(datc_param.coding == CodingChoices_none){
				cur_task = new EncodeFeaturesTask(encoder,"BRISK",features_sub,0);
				taskManager_ptr->addTask(cur_task);
				//cout << "NM: Waiting the end of the encode_features_task" << endl;
				while(!cur_task->completed){
					cur_task_finished.wait(lk);
				}
				//cout << "NM: ended encode_features_task" << endl;
				block_ft_bitstream = ((EncodeFeaturesTask*)cur_task)->getFeatsBitstream();
				delete((EncodeFeaturesTask*)cur_task);
			}
			if(datc_param.coding == CodingChoices_entropyCoding){
				cur_task = new EncodeFeaturesTask(encoder,"BRISK",features_sub,1);
				taskManager_ptr->addTask(cur_task);
				//cout << "NM: Waiting the end of the encode_features_task" << endl;
				while(!cur_task->completed){
					cur_task_finished.wait(lk);
				}
				//cout << "NM: ended encode_features_task" << endl;
				block_ft_bitstream = ((EncodeFeaturesTask*)cur_task)->getFeatsBitstream();
				delete((EncodeFeaturesTask*)cur_task);
			}

			vector<KeyPoint> sub_kpts(&kpts[beg],&kpts[end]);
			cur_task = new EncodeKeypointsTask(encoder,sub_kpts,640,480,true,1);
			taskManager_ptr->addTask(cur_task);
			//cout << "NM: Waiting the end of the encode_kpts_task" << endl;
			while(!cur_task->completed){
				cur_task_finished.wait(lk);
			}
			//cout << "NM: ended encode_kpts_task" << endl;
			block_kp_bitstream = ((EncodeKeypointsTask*)cur_task)->getKptsBitstream();
			delete((EncodeKeypointsTask*)cur_task);
			cout << "sending " << (int)(sub_kpts.size()) << " keypoints" << endl;
			cout << "and " << (int)(features_sub.rows) << " features" << endl;

			//TODO: understand what to do with encoding times...
			DataATCMsg *msg = new DataATCMsg(frame_id, i, num_blocks, camDetTime, camDescTime, 0, 0, 0, features_sub.rows, sub_kpts.size(), block_ft_bitstream, block_kp_bitstream);
			msg->setSource(node_id);
			msg->setDestination(0);

			//sendMessage(msg); //ALEXIS SIMULATE 01/02

			cout << "first keypoint: " << sub_kpts[0].pt.x << " " << sub_kpts[0].pt.y << endl;

		}
	}
	cur_state = IDLE;
	//ALEXIS Messages SYNC 19/02
	AddCameraMessage(1);
}


/*void NodeManager::AddCameraMessage(int cameraid){

	AddCameraMsg *msg = new AddCameraMsg(cameraid);

	msg->setSource(node_id);
	msg->setDestination(0);
	sendMessage(msg);
}*/

//ALEXIS Cameras SYNC 19/02
void NodeManager::AddCameraMessage(int cameraid){

	switch(node_type){

	case CAMERA:
	{
		AddCameraMsg *msg = new AddCameraMsg(cameraid);

		std::set<Connection*> connections = radioSystem_ptr->getWiFiConnections();
		std::set<Connection*>::iterator it = connections.begin();
		Connection* cn = *it;
		Task *cur_task;
		cout << "Sending AddCameraMsg to Coop " << endl;

		msg->setTcpConnection(cn);
		msg->setSource(node_id);
		msg->setDestination(3);
		cur_task = new SendWiFiMessageTask(msg);
		taskManager_ptr->addTask(cur_task);
		cout << "NM: Waiting the end of the send_wifi_message_task " << endl;
		{
			boost::mutex::scoped_lock lk(cur_task->task_monitor);
			while(!cur_task->completed){
				cur_task_finished.wait(lk);
			}
		}
		cout << "NM: end AddCamera tx msg " << endl;
		delete((SendWiFiMessageTask*)cur_task);
		break;
	}

	case COOPERATOR:
	{

		AddCameraMsg *msg = new AddCameraMsg(cameraid);

		std::set<Connection*> connections = radioSystem_ptr->getWiFiConnections();
		std::set<Connection*>::iterator it = connections.begin();
		msg->setSource(node_id);

		Task *cur_task;
		cout << "Sending AddCameraMsg to Camera " << endl;
		double Timelow;
		sleep(0.5);
		int tmp = connections.size();
		for(int j=0;j<tmp;j++){

			msg->setDestination(j+1);
			std::advance(it, j);
			Connection* cn = *it;
			msg->setTcpConnection(cn);
			cur_task = new SendWiFiMessageTask(msg);
			if(j==0) Timelow=getTickCount();
			taskManager_ptr->addTask(cur_task);
			cout << "NM: Waiting the end of the send_wifi_message_task " << endl;
			{
				boost::mutex::scoped_lock lk(cur_task->task_monitor);
				while(!cur_task->completed){
					cur_task_finished.wait(lk);
				}
			}
			if(j==1){
				Timelow = (getTickCount()-Timelow)/getTickFrequency();
				cout<<"Timelow: "<<Timelow<<endl;
			}
			cout << "NM: end AddCameras tx msg " << endl;
			delete((SendWiFiMessageTask*)cur_task);
		}
		break;
	}
	}
}
//


void NodeManager::Processing_slice(int processingID, int subslices_iteration, Mat slice, double detection_threshold, int max_features){
	cout << "NM: I'm entering the ProcessingSlice thread " << processingID + 1 << endl;

	Task *cur_task;

	pixels_to_process[processingID] += slice.rows*slice.cols;

	// Extract the keypoints
	cur_task = new ExtractKeypointsTask(extractor,slice,detection_threshold);
	taskManager_ptr->addTask(cur_task);
	cerr << "NM: Waiting the end of the extract_keypoints_task " << processingID + 1<< endl;
	cerr << "1 Status " << cur_task << " " << cur_task->completed << endl;
	{
		boost::mutex::scoped_lock lk(cur_task->task_monitor);
		cerr << "1 Status " << cur_task << " " << cur_task->completed << endl;
		while(!cur_task->completed){
			cur_task_finished.wait(lk);
		}
	}
	cerr << "NM: ended extract_keypoints_task " << processingID + 1 << endl;
	vector<KeyPoint> kpts = ((ExtractKeypointsTask*)cur_task)->getKeypoints();
	double detTime = ((ExtractKeypointsTask*)cur_task)->getDetTime();
	cerr << "extracted " << (int)kpts.size() << "keypoints" << endl;
	cerr << "extracted " << (int)kpts.size() << "keypoints\tDetThreshold=" << detection_threshold << endl;
	delete((ExtractKeypointsTask*)cur_task);

	//Extract features
	std::cout<<std::dec;
	cur_task = new ExtractFeaturesTask(extractor,slice,kpts,max_features);
	taskManager_ptr->addTask(cur_task);
	{
		boost::mutex::scoped_lock lk(cur_task->task_monitor);
		while(!cur_task->completed){
			cur_task_finished.wait(lk);
		}	
	}
	cerr << "NM: ended extract_features_task " << processingID + 1 << endl;
	Mat features = ((ExtractFeaturesTask*)cur_task)->getFeatures();
	kpts = ((ExtractFeaturesTask*)cur_task)->getKeypoints();
	double descTime = ((ExtractFeaturesTask*)cur_task)->getDescTime();
	cerr << "now extracted " << (int)kpts.size() << " keypoints" << endl;
	delete((ExtractFeaturesTask*)cur_task);

	
	//save keypoints and features
	processing_manager[processingID]->storeKeypointsAndFeatures(subslices_iteration,kpts,features, detTime, descTime);

}


void NodeManager::TransmissionFinished(int i, Connection* c){

	Task *cur_task;
	cout << "Sending ACK_SLICE_MESSAGE to Camera " << i << endl;

	ACKsliceMsg *ackslice_msg = new ACKsliceMsg(frame_id);

	ackslice_msg->setTcpConnection(c);
	ackslice_msg->setSource(node_id);
	ackslice_msg->setDestination(i);
	cur_task = new SendWiFiMessageTask(ackslice_msg);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the send_wifi_message_task " << i << endl;
	{
		boost::mutex::scoped_lock lk(cur_task->task_monitor);
		while(!cur_task->completed){
			cur_task_finished.wait(lk);
		}
	}
	cout << "NM: end ACK SLICE tx msg " << i << endl;
	delete((SendWiFiMessageTask*)cur_task);
}


void NodeManager::notifyCooperatorCompleted(int i, vector<KeyPoint>& kpts,Mat& features, double detTime, double descTime, double mergeTime, double processingTime, Connection* c){

	Task *cur_task;
	cv::Mat slice;

	//features serialization
	vector<uchar> ft_bitstream;
	vector<uchar> kp_bitstream;
	cur_task = new EncodeFeaturesTask(encoder,"BRISK",features,0);
	taskManager_ptr->addTask(cur_task);
	//cout << "NM: Waiting the end of the encode_features_task" << endl;
	{
		boost::mutex::scoped_lock lk(cur_task->task_monitor);
		while(!cur_task->completed){
			cur_task_finished.wait(lk);
		}
	}	
	//cout << "NM: ended encode_features_task" << endl;
	ft_bitstream = ((EncodeFeaturesTask*)cur_task)->getFeatsBitstream();
	double fencTime = ((EncodeFeaturesTask*)cur_task)->getEncodingTime();
	delete((EncodeFeaturesTask*)cur_task);

	cur_task = new EncodeKeypointsTask(encoder,kpts,640,480,true,0);
	taskManager_ptr->addTask(cur_task);
	//cout << "NM: Waiting the end of the encode_kpts_task" << endl;
	{
		boost::mutex::scoped_lock lk(cur_task->task_monitor);
		while(!cur_task->completed){
			cur_task_finished.wait(lk);
		}
	}	
	//cout << "NM: ended encode_kpts_task" << endl;
	kp_bitstream = ((EncodeKeypointsTask*)cur_task)->getKptsBitstream();
	double kencTime = ((EncodeKeypointsTask*)cur_task)->getEncodingTime();
	delete((EncodeKeypointsTask*)cur_task);
	cout << "sending " << (int)(kpts.size()) << "keypoints" << endl;
	cout << "and " << (int)(features.rows) << "features" << endl;

	DataATCMsg *atc_msg = new DataATCMsg(frame_id, 0, 1, detTime, descTime, mergeTime, processingTime, pixels_to_process[i-1], features.rows, kpts.size(), ft_bitstream, kp_bitstream);

	atc_msg->setTcpConnection(c);
	atc_msg->setSource(node_id);
	atc_msg->setDestination(i);
	cur_task = new SendWiFiMessageTask(atc_msg);
	taskManager_ptr->addTask(cur_task);
	cout << "NM: Waiting the end of the send_wifi_message_task " << i << endl;
	{
		boost::mutex::scoped_lock lk(cur_task->task_monitor);
		while(!cur_task->completed){
			cur_task_finished.wait(lk);
		}
	}
	cout << "NM: finish to sent DataATC msg to Camera " << i << endl;
	delete((SendWiFiMessageTask*)cur_task);
	pixels_to_process[i-1]=0;
}
	
/*
void NodeManager::SIMULATION(int option){	
	
	switch(option){
	
		case 1:
		{
			if(simulate==1){//simulate == number of Coops to wait to start msg
				sleep(2);
				//StartDATCMsg (FramesPerSecond, DetectorType, DetectorThreshold, DescriptorType, DescriptorLength,
				//				 MaxFeatures, RotationInvariant, CodingChoice, TransmitKpts, TransmitScale, 
				//				  TransmitOrientation, NumFeaPerBlock, NumCooperators)
				StartDATCMsg *datc_msg = new StartDATCMsg(0, 5, 40, 3, 512, 50, 1, 0, 1,1,1, 20,1);
				//msg(msg_type,seq_num, src_addr, dest_addr, connection)
				datc_msg->setSource(0);
				datc_msg->setDestination(node_id);
				Task *cur_task;
				cur_task = new SIMULATIONTask(datc_msg);
				taskManager_ptr->addTask(cur_task);
			}
		}
		case 2:
		{	
			StartDATCMsg *datc_msg = new StartDATCMsg(0, 5, 40, 3, 512, 50, 1, 0, 1,1,1, 20,1);
			datc_msg->setSource(0);
			datc_msg->setDestination(node_id);
			//notify_msg(datc_msg);
			Task *cur_task;
			cur_task = new SIMULATIONTask(datc_msg);
			taskManager_ptr->addTask(cur_task);
			cout << "sending another DATCmsg" << endl;
		}
	}
}
*/