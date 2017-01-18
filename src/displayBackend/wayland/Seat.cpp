/*
 * Seat.cpp
 *
 *  Created on: Nov 30, 2016
 *      Author: al1
 */

#include "Seat.hpp"

#include "Exception.hpp"

using std::lock_guard;
using std::mutex;
using std::shared_ptr;
using std::string;

using InputItf::KeyboardCallbacks;
using InputItf::PointerCallbacks;
using InputItf::TouchCallbacks;

namespace Wayland {

/*******************************************************************************
 * Seat
 ******************************************************************************/

Seat::Seat(wl_registry* registry, uint32_t id, uint32_t version) :
	Registry(registry, id, version),
	mWlSeat(nullptr),
	mLog("Seat")
{
	try
	{
		init();
	}
	catch(const WlException& e)
	{
		release();

		throw;
	}
}

Seat::~Seat()
{
	release();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

std::shared_ptr<SeatKeyboard> Seat::getKeyboard()
{
	lock_guard<mutex> lock(mMutex);

	return mSeatKeyboard;
}

std::shared_ptr<SeatPointer> Seat::getPointer()
{
	lock_guard<mutex> lock(mMutex);

	return mSeatPointer;
}

std::shared_ptr<SeatTouch> Seat::getTouch()
{
	lock_guard<mutex> lock(mMutex);

	return mSeatTouch;
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void Seat::sReadCapabilities(void *data, wl_seat* seat,
							 uint32_t capabilities)
{
	static_cast<Seat*>(data)->readCapabilities(capabilities);
}

void Seat::sReadName(void *data, wl_seat* seat, const char *name)
{
	static_cast<Seat*>(data)->readName(string(name));
}

void Seat::readCapabilities(uint32_t capabilities)
{
	lock_guard<mutex> lock(mMutex);

	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD)
	{
		LOG(mLog, DEBUG) << "Display has a keyboard";

		if (!mSeatKeyboard)
		{
			mSeatKeyboard.reset(new SeatKeyboard(mWlSeat));
		}
	}

	if (capabilities & WL_SEAT_CAPABILITY_POINTER)
	{
		LOG(mLog, DEBUG) << "Display has a pointer";

		if (!mSeatPointer)
		{
			mSeatPointer.reset(new SeatPointer(mWlSeat));
		}
	}

	if (capabilities & WL_SEAT_CAPABILITY_TOUCH)
	{
		LOG(mLog, DEBUG) << "Display has a touch screen";

		if (!mSeatTouch)
		{
			mSeatTouch.reset(new SeatTouch(mWlSeat));
		}
	}
}

void Seat::readName(const std::string& name)
{
	LOG(mLog, DEBUG) << "Name: " << name;
}

void Seat::init()
{
	mWlSeat = static_cast<wl_seat*>(wl_registry_bind(getRegistry(), getId(),
									&wl_seat_interface, cVersion));

	if (!mWlSeat)
	{
		throw WlException("Can't bind seat");
	}

	mWlListener = {sReadCapabilities, sReadName};

	if (wl_seat_add_listener(mWlSeat, &mWlListener, this) < 0)
	{
		throw WlException("Can't add listener");
	}

	LOG(mLog, DEBUG) << "Create";
}

void Seat::release()
{
	if (mWlSeat)
	{
		wl_seat_destroy(mWlSeat);

		LOG(mLog, DEBUG) << "Delete";
	}
}

}