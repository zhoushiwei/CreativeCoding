#include "LedMatrixApp.h"
#include "OscMessage.h"

using namespace ci;

void LedMatrixApp::onKinect( const osc::Message* msg )
{
	if (msg->getAddress() == "/start")
	{
		int dev_id = msg->getArgAsInt32(0);
		if (single_session[dev_id].empty())
			return;
		lock_guard<mutex> lock(mtx_kinect_queues[dev_id]);
		kinect_queues[dev_id].push_back(single_session[dev_id]);
		single_session[dev_id].clear();
	}
	else
	if (msg->getAddress() == "/contour")
	{
		Vec3f pos(msg->getArgAsFloat(2), msg->getArgAsFloat(3),
			msg->getArgAsFloat(5));
 		int dev_id = msg->getArgAsInt32(7); 
		single_session[dev_id].push_back(pos);
	}
}


bool LedMatrixApp::getNewCenter( Vec3i& center, int dev )
{
	bool updated = !centers[dev].empty();
	if (updated)
	{//TODO: add support to more centers
		center = centers[dev].front();
	}
	return updated;
}
