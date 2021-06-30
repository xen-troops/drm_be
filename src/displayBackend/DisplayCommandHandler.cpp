/*
 *  Command handler
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
 */

#include "DisplayCommandHandler.hpp"

#include <iomanip>

#include <cassert>
#include <xen/be/Exception.hpp>

using std::dec;
using std::hex;
using std::setfill;
using std::setw;
using std::unordered_map;

using DisplayItf::ConnectorPtr;
using DisplayItf::DisplayPtr;

/*******************************************************************************
 * Protocol differences between its versions and their implications
 *******************************************************************************
 * Version 2
 *******************************************************************************
 * - XENDISPL_OP_DBUF_CREATE introduces data offset field: if frontend doesn't
 *   support data offset field, e.g. it uses protocol version 1, then
 *   according to the protocol it sees this field as "reserved" which must
 *   be set to 0, thus effectively meaning for the backend zero offset is used.
 * - XENDISPL_OP_GET_EDID is an optional request, so if frontend uses protocol
 *   version 1 it is not known to it, thus it is not going to be issued.
 ******************************************************************************/

unordered_map<int, DisplayCommandHandler::CommandFn>
	DisplayCommandHandler::sCmdTable =
{
	{XENDISPL_OP_PG_FLIP,		&DisplayCommandHandler::pageFlip},
	{XENDISPL_OP_FB_ATTACH,		&DisplayCommandHandler::attachFrameBuffer},
	{XENDISPL_OP_FB_DETACH,		&DisplayCommandHandler::detachFrameBuffer},
	{XENDISPL_OP_DBUF_CREATE,	&DisplayCommandHandler::createDisplayBuffer},
	{XENDISPL_OP_DBUF_DESTROY,	&DisplayCommandHandler::destroyDisplayBuffer},
	{XENDISPL_OP_SET_CONFIG,	&DisplayCommandHandler::setConfig},
	{XENDISPL_OP_GET_EDID,		&DisplayCommandHandler::getEDID}
};

/*******************************************************************************
 * ConEventRingBuffer
 ******************************************************************************/

EventRingBuffer::EventRingBuffer(int conIndex, domid_t domId,
								 evtchn_port_t port, grant_ref_t ref,
								 int offset, size_t size) :
	RingBufferOutBase<xendispl_event_page, xendispl_evt>(domId, port, ref,
														 offset, size),
	mConIndex(conIndex),
	mLog("ConEventRing")
{
	LOG(mLog, DEBUG) << "Create event ring buffer, index: " << mConIndex;
}

/*******************************************************************************
 * CommandHandler
 ******************************************************************************/

DisplayCommandHandler::DisplayCommandHandler(
		DisplayPtr display,
		ConnectorPtr connector,
		BuffersStoragePtr buffersStorage,
		EventRingBufferPtr eventBuffer) :
	mDisplay(display),
	mConnector(connector),
	mBuffersStorage(buffersStorage),
	mEventBuffer(eventBuffer),
	mEventId(0),
	mLog("CommandHandler")
{
	assert(mConnector);
	assert(mDisplay);
	assert(mBuffersStorage);
	assert(mEventBuffer);

	LOG(mLog, DEBUG) << "Create command handler, connector name: "
					 << mConnector->getName();
}

DisplayCommandHandler::~DisplayCommandHandler()
{
	LOG(mLog, DEBUG) << "Delete command handler, connector name: "
					 << mConnector->getName();

	mConnector.reset();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

int DisplayCommandHandler::processCommand(const xendispl_req& req,
										  xendispl_resp& rsp)
{
	int status = 0;

	try
	{
		(this->*sCmdTable.at(req.operation))(req, rsp);

		mDisplay->flush();
	}
	catch(const XenBackend::Exception& e)
	{
		LOG(mLog, ERROR) << e.what();

		status = -e.getErrno();

		if (status >= 0)
		{
			DLOG(mLog, WARNING) << "Positive error code: "
								<< static_cast<signed int>(status);

			status = -EINVAL;
		}
	}
	catch(const std::out_of_range& e)
	{
		LOG(mLog, ERROR) << e.what();

		status = -EINVAL;
	}
	catch(const std::exception& e)
	{
		LOG(mLog, ERROR) << e.what();

		status = -EIO;
	}

	DLOG(mLog, DEBUG) << "Return status: ["
					  << static_cast<signed int>(status) << "]";

	return status;
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void DisplayCommandHandler::pageFlip(const xendispl_req& req,
									 xendispl_resp& rsp)
{
	xendispl_page_flip_req flipReq = req.op.pg_flip;

	auto cookie = flipReq.fb_cookie;

	DLOG(mLog, DEBUG) << "Handle command [PAGE FLIP], fb ID: "
					  << hex << setfill('0') << setw(16)
					  << cookie;

	mConnector->pageFlip(mBuffersStorage->getFrameBufferAndCopy(cookie),
						 [cookie, this] () { sendFlipEvent(cookie); });
}

void DisplayCommandHandler::createDisplayBuffer(const xendispl_req& req,
												xendispl_resp& rsp)
{
	const xendispl_dbuf_create_req* dbufReq = &req.op.dbuf_create;

	bool beAllocRefs = dbufReq->flags & XENDISPL_DBUF_FLG_REQ_ALLOC;

	/**
	 * Special handling is required for be_alloc == 1 mode which
	 * is not supported with a non-zero offsets: do not let us down to
	 * creating any resources and terminate the command here.
	 */
	size_t data_ofs = dbufReq->data_ofs;

	DLOG(mLog, DEBUG) << "Handle command [CREATE DBUF], cookie: "
					  << hex << setfill('0') << setw(16)
					  << dbufReq->dbuf_cookie
					  << ", offset: " << dec << data_ofs;

	if (beAllocRefs && data_ofs)
	{
		throw XenBackend::Exception("Can't create buffer with non-zero offset in this mode",
									EINVAL);
	}

	mBuffersStorage->createDisplayBuffer(dbufReq->dbuf_cookie,
										 beAllocRefs,
										 dbufReq->gref_directory,
										 data_ofs,
										 dbufReq->buffer_sz,
										 dbufReq->width, dbufReq->height,
										 dbufReq->bpp);
}

void DisplayCommandHandler::destroyDisplayBuffer(const xendispl_req& req,
												 xendispl_resp& rsp)
{
	const xendispl_dbuf_destroy_req* dbufReq = &req.op.dbuf_destroy;

	DLOG(mLog, DEBUG) << "Handle command [DESTROY DBUF], cookie: "
					  << hex << setfill('0') << setw(16)
					  << dbufReq->dbuf_cookie;

	mBuffersStorage->destroyDisplayBuffer(dbufReq->dbuf_cookie);
}

void DisplayCommandHandler::attachFrameBuffer(const xendispl_req& req,
											  xendispl_resp& rsp)
{
	const xendispl_fb_attach_req* fbReq = &req.op.fb_attach;

	DLOG(mLog, DEBUG) << "Handle command [ATTACH FB], DB cookie: "
					  << hex << setfill('0') << setw(16)
					  << fbReq->dbuf_cookie << ", FB cookie: "
					  << setw(16) << fbReq->fb_cookie;

	mBuffersStorage->createFrameBuffer(fbReq->dbuf_cookie, fbReq->fb_cookie,
									   fbReq->width, fbReq->height,
									   fbReq->pixel_format);

}

void DisplayCommandHandler::detachFrameBuffer(const xendispl_req& req,
											  xendispl_resp& rsp)
{
	const xendispl_fb_detach_req* fbReq = &req.op.fb_detach;

	DLOG(mLog, DEBUG) << "Handle command [DETACH FB], cookie: "
					  << hex << setfill('0') << setw(16)
					  << fbReq->fb_cookie;

	mBuffersStorage->destroyFrameBuffer(fbReq->fb_cookie);
}

void DisplayCommandHandler::setConfig(const xendispl_req& req,
									  xendispl_resp& rsp)
{
	const xendispl_set_config_req* configReq = &req.op.set_config;

	DLOG(mLog, DEBUG) << "Handle command [SET CONFIG], FB cookie: "
					  << hex << setfill('0') << setw(16)
					  << configReq->fb_cookie;

	if (configReq->fb_cookie != 0)
	{
		if (mConnector->isInitialized())
		{
			LOG(mLog, DEBUG) << "Connector " << mConnector->getName()
							 << " is reinitialized";

			mConnector->release();
		}

		mConnector->init(configReq->width, configReq->height,
				mBuffersStorage->getFrameBufferAndCopy(configReq->fb_cookie));
	}
	else
	{
		mConnector->release();
	}
}

void DisplayCommandHandler::getEDID(const xendispl_req& req,
									xendispl_resp& rsp)
{
	xendispl_get_edid_req edidReq = req.op.get_edid;
	xendispl_get_edid_resp& edidResp = rsp.op.get_edid;

	DLOG(mLog, DEBUG) << "Handle command [GET EDID], buffer size: "
					  << edidReq.buffer_sz;

	edidResp.edid_sz = static_cast<uint32_t>(mConnector->getEDID(
			edidReq.gref_directory,
			edidReq.buffer_sz));
}

void DisplayCommandHandler::sendFlipEvent(uint64_t fbCookie)
{
	DLOG(mLog, DEBUG) << "Event [PAGE FLIP], conn name: "
					  << mConnector->getName() << ", fb ID: "
					  << hex << setfill('0') << setw(16) << fbCookie;

	xendispl_evt event {};

	event.type = XENDISPL_EVT_PG_FLIP;
	event.op.pg_flip.fb_cookie = fbCookie;
	event.id = mEventId++;

	mEventBuffer->sendEvent(event);
}
