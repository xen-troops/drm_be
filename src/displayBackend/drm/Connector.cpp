/*
 *  Connector class
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (C) 2016 EPAM Systems Inc.
 *
 */

#include "Connector.hpp"
#include <cassert>
#include "Display.hpp"

using std::chrono::milliseconds;
using std::list;
using std::lock_guard;
using std::mutex;
using std::string;
using std::this_thread::sleep_for;
using std::to_string;

using DisplayItf::FrameBufferPtr;

namespace Drm {

mutex Connector::sMutex;
list<uint32_t> Connector::sCrtcIds;

/*******************************************************************************
 * Connector
 ******************************************************************************/

Connector::Connector(domid_t domId, const string& name, int fd, int conId,
					 uint32_t width, uint32_t height) :
	ConnectorBase(domId, width, height),
	mName(name),
	mFd(fd),
	mCrtcId(cInvalidId),
	mConnector(mFd, conId),
	mSavedCrtc(nullptr),
	mFlipPending(false),
	mFlipCallback(nullptr)
{
	LOG(mLog, DEBUG) << "Create, name: " << mName
					 << ", id: " << mConnector->connector_id
					 << ", connected: " << isConnected();
}

Connector::~Connector()
{
	if (mFlipPending)
	{
		sleep_for(milliseconds(100));
	}

	if (mFlipPending)
	{
		LOG("FrameBuffer", ERROR) << "Delete frame buffer on pending flip";
	}

	release();

	LOG(mLog, DEBUG) << "Delete, id: " << mConnector->connector_id;
}

/*******************************************************************************
 * Public
 ******************************************************************************/

void Connector::init(uint32_t width, uint32_t height,
					 FrameBufferPtr frameBuffer)
{
	assert(frameBuffer);
	
	lock_guard<mutex> lock(sMutex);

	if (mConnector->connection != DRM_MODE_CONNECTED)
	{
		throw Exception("Connector is not connected", EINVAL);
	}

	if (mCrtcId != cInvalidId)
	{
		throw Exception("Already initialized", EINVAL);
	}

	auto fBuffer= dynamic_cast<Drm::FrameBuffer*>(frameBuffer.get());
	// type of buffer must be Drm::FrameBuffer
	assert(fBuffer);
    
	auto fbId = fBuffer->getID();

	LOG(mLog, DEBUG) << "Init, con id:" << mConnector->connector_id
					 << ", w: " << width << ", h: " << height
					 << ", fb id: " << fbId;

	mCrtcId = findCrtcId();

	if (mCrtcId == cInvalidId)
	{
		throw Exception("Cannot find CRTC for connector", EINVAL);
	}

	auto mode = findMode(width, height);

	if (!mode)
	{
		throw Exception("Unsupported mode", EINVAL);
	}

	mSavedCrtc = drmModeGetCrtc(mFd, mCrtcId);

	if (drmModeSetCrtc(mFd, mCrtcId, fbId, 0, 0,
					   &mConnector->connector_id, 1, mode))
	{
		throw Exception("Cannot set CRTC for connector", errno);
	}

	sCrtcIds.push_back(mCrtcId);
}

void Connector::release()
{
	lock_guard<mutex> lock(sMutex);

	sCrtcIds.remove(mCrtcId);

	mCrtcId = cInvalidId;

	if (mSavedCrtc)
	{
		drmModeSetCrtc(mFd, mSavedCrtc->crtc_id, mSavedCrtc->buffer_id,
					   mSavedCrtc->x, mSavedCrtc->y, &mConnector->connector_id,
					   1, &mSavedCrtc->mode);

		drmModeFreeCrtc(mSavedCrtc);

		mSavedCrtc = nullptr;
	}
}

void Connector::pageFlip(FrameBufferPtr frameBuffer, FlipCallback cbk)
{
	assert(frameBuffer);

	if (!isInitialized())
	{
		throw Exception("Connector is not initialized", EINVAL);
	}

	mFlipPending = true;
	mFlipCallback = cbk;

	auto fBuffer= dynamic_cast<Drm::FrameBuffer*>(frameBuffer.get());
	// type of buffer must be Drm::FrameBuffer
	assert(fBuffer);
    
	auto fbId = fBuffer->getID();

	auto ret = drmModePageFlip(mFd, mCrtcId, fbId,
							   DRM_MODE_PAGE_FLIP_EVENT, this);

	if (ret)
	{
		throw Exception("Cannot flip CRTC: " + to_string(fbId), errno);
	}


	DLOG(mLog, DEBUG) << "Page flip, fb id: " << fbId;
}

/*******************************************************************************
 * Private
 ******************************************************************************/

uint32_t Connector::findCrtcId()
{
	auto crtcId = getAssignedCrtcId();

	if (crtcId != cInvalidId)
	{
		return crtcId;
	}

	return findMatchingCrtcId();
}

bool Connector::isCrtcIdUsedByOther(uint32_t crtcId)
{
	if (find(sCrtcIds.begin(), sCrtcIds.end(), crtcId) != sCrtcIds.end())
	{
		return true;
	}

	return false;
}

uint32_t Connector::getAssignedCrtcId()
{
	auto encoderId = mConnector->encoder_id;

	if (encoderId != cInvalidId)
	{
		ModeEncoder encoder(mFd, encoderId);

		auto crtcId = encoder->crtc_id;

		if (crtcId != cInvalidId)
		{

			if (isCrtcIdUsedByOther(crtcId))
			{
				return cInvalidId;
			}

			LOG(mLog, DEBUG) << "Assigned crtc id: " << crtcId
							 << ", con id: " << mConnector->connector_id;

			return crtcId;
		}
	}

	return cInvalidId;
}

uint32_t Connector::findMatchingCrtcId()
{
	ModeResource resource(mFd);

	for (int encIndex = 0; encIndex < mConnector->count_encoders; encIndex++)
	{
		ModeEncoder encoder(mFd, mConnector->encoders[encIndex]);

		for (int crtcIndex = 0; crtcIndex < resource->count_crtcs; crtcIndex++)
		{
			if (!(encoder->possible_crtcs & (1 << crtcIndex)))
			{
				continue;
			}

			auto crtcId = resource->crtcs[crtcIndex];

			if (isCrtcIdUsedByOther(crtcId))
			{
				continue;
			}

			LOG(mLog, DEBUG) << "Matched crtc found: " << crtcId
							 << ", con id: " << mConnector->connector_id;

			return crtcId;
		}
	}

	return cInvalidId;
}

drmModeModeInfoPtr Connector::findMode(uint32_t width, uint32_t height)
{
	for (int i = 0; i < mConnector->count_modes; i++)
	{
		if (mConnector->modes[i].hdisplay == width &&
			mConnector->modes[i].vdisplay == height)
		{
			LOG(mLog, DEBUG) << "Found mode: " << mConnector->modes[i].name
							 << ", con id: " << mConnector->connector_id;

			return &mConnector->modes[i];
		}
	}

	return nullptr;
}

void Connector::flipFinished()
{
	if (!mFlipPending)
	{
		DLOG(mLog, ERROR) << "Not expected flip event";

		return;
	}

	DLOG(mLog, DEBUG) << "Flip done";

	mFlipPending = false;

	if (mFlipCallback)
	{
		mFlipCallback();
	}
}

}
