/*
 * DevInput.cpp
 *
 *  Created on: Jan 13, 2017
 *      Author: al1
 */

#include <iomanip>

#include <fcntl.h>

#include <xen/be/Exception.hpp>

#include "DevInput.hpp"

using std::lock_guard;
using std::mutex;
using std::setfill;
using std::setw;
using std::string;
using std::thread;

using XenBackend::PollFd;

using InputItf::KeyboardCallbacks;
using InputItf::PointerCallbacks;
using InputItf::TouchCallbacks;

/*******************************************************************************
 * InputBase
 ******************************************************************************/

DevInputBase::DevInputBase(const string& name) :
	mLog("DevInputDevice"),
	mName(name),
	mFd(-1)
{
	try
	{
		init();
	}
	catch(const std::exception& e)
	{
		release();

		throw;
	}
}

DevInputBase::~DevInputBase()
{
	stop();
	release();
}

void DevInputBase::start()
{
	if (!mThread.joinable())
	{
		mThread = thread(&DevInputBase::run, this);
	}
}

void DevInputBase::stop()
{
	if (mPollFd)
	{
		mPollFd->stop();
	}

	if (mThread.joinable())
	{
		mThread.join();
	}
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void DevInputBase::init()
{
	auto ioctl_call = [this](int value)->int
	{
		return ioctl(mFd, EVIOCGRAB, &value);
	};

	mFd = open(mName.c_str(), O_RDONLY);

	if (mFd < 0)
	{
		throw XenBackend::Exception("Can't open device: " + mName, errno);
	}

	if (ioctl_call(1))
	{
		throw XenBackend::Exception("Grabbed by another process", EBUSY);
	}

	ioctl_call(0);

	mPollFd.reset(new PollFd(mFd, POLLIN));

	LOG(mLog, DEBUG) << "Create: " << mName;
}

void DevInputBase::release()
{
	if (mFd >= 0)
	{
		close(mFd);

		LOG(mLog, DEBUG) << "Delete: " << mName;
	}
}

void DevInputBase::run()
{
	try
	{
		while(mPollFd->poll())
		{
			int readSize = 0;

			input_event events[64];

			readSize = read(mFd, events, sizeof(events));

			if (readSize < static_cast<int>(sizeof(struct input_event)))
			{
				throw XenBackend::Exception("Read error", errno);
			}

			for(size_t i = 0; i < readSize/sizeof(input_event); i++)
			{
				onEvent(events[i]);
			}
		}
	}
	catch(const std::exception& e)
	{
		LOG(mLog, ERROR) << e.what();
	}
}

/*******************************************************************************
 * InputKeyboard
 ******************************************************************************/
DevInput<KeyboardCallbacks>::DevInput(const string& name) : DevInputCbk(name)
{
	start();
}

DevInput<KeyboardCallbacks>::~DevInput()
{
	stop();
}

void DevInput<KeyboardCallbacks>::onEvent(const input_event& event)
{
	lock_guard<mutex> lock(mMutex);

	if (event.type == EV_KEY && mCallbacks.key)
	{
		LOG(mLog, DEBUG) << mName << ", key: " << event.code
						 << ", value: " << event.value;

		mCallbacks.key(event.code, event.value);
	}
}

/*******************************************************************************
 * InputPointer
 ******************************************************************************/

DevInput<PointerCallbacks>::DevInput(const string& name) : DevInputCbk(name)
{
	mRelX = mRelY = mRelZ = mAbsX = mAbsY = 0;
	mSendRel = mSendAbs = mSendWheel = false;

	start();
}

DevInput<PointerCallbacks>::~DevInput()
{
	stop();
}

void DevInput<PointerCallbacks>::onEvent(const input_event& event)
{
	switch(event.type)
	{
		case EV_REL:
			onRelEvent(event);
			break;

		case EV_ABS:
			onAbsEvent(event);
			break;

		case EV_KEY:
			onKeyEvent(event);
			break;

		case EV_SYN:
			onSynEvent(event);
			break;

		default:
			break;
	}
}

void DevInput<PointerCallbacks>::onRelEvent(const input_event& event)
{
	if (event.code == REL_X)
	{
		mRelX = event.value;

		mSendRel = true;
	}
	else if (event.code == REL_Y)
	{
		mRelY = event.value;

		mSendRel = true;
	}
	else if (event.code == REL_WHEEL)
	{
		mRelZ = event.value;

		mSendWheel = true;
	}
}

void DevInput<PointerCallbacks>::onAbsEvent(const input_event& event)
{
	if (event.code == ABS_X)
	{
		mAbsX = event.value;

		mSendAbs = true;
	}
	else if (event.code == ABS_Y)
	{
		mAbsY = event.value;

		mSendAbs = true;
	}
}

void DevInput<PointerCallbacks>::onKeyEvent(const input_event& event)
{
	lock_guard<mutex> lock(mMutex);

	if (mCallbacks.button)
	{
		LOG(mLog, DEBUG) << mName << ", key: " << event.code
						 << ", value: " << event.value;

		mCallbacks.button(event.code, event.value);
	}
}

void DevInput<PointerCallbacks>::onSynEvent(const input_event& event)
{
	lock_guard<mutex> lock(mMutex);

	if ((mSendRel || mSendWheel) && mCallbacks.moveRelative)
	{
		LOG(mLog, DEBUG) << mName
						 << ", rel x: " << mRelX
						 << ", rel y: " << mRelY
						 << ", rel z: " << mRelZ;

		mCallbacks.moveRelative(mRelX, mRelY, mRelZ);

		mRelX = mRelY = mRelZ = 0;
		mSendWheel = false;
		mSendRel = false;
	}

	if (mSendAbs && mCallbacks.moveAbsolute)
	{
		LOG(mLog, DEBUG) << mName
						 << ", abs x: " << mAbsX
						 << ", abs y: " << mAbsY
						 << ", rel z: " << mRelZ;

		mCallbacks.moveAbsolute(mAbsX, mAbsY, mRelZ);

		mRelZ = 0;
		mSendWheel = false;
		mSendAbs = false;
	}
}

/*******************************************************************************
 * InputTouch
 ******************************************************************************/

DevInput<TouchCallbacks>::DevInput(const string& name) : DevInputCbk(name)
{
	mKey = 0;
	mSendAbs = mSendKey = mMtReport = false;
	mLastMtReports = 0;

	setCurrentSlot(0);
	mContacts[mCurrentSlot].id = -1;

	start();
}

DevInput<TouchCallbacks>::~DevInput()
{
	stop();
}

void DevInput<TouchCallbacks>::onEvent(const input_event& event)
{
	switch(event.type)
	{
		case EV_ABS:
			onAbsEvent(event);
			break;

		case EV_KEY:
			onKeyEvent(event);
			break;

		case EV_SYN:
			onSynEvent(event);
			break;

		default:
			break;
	}
}

void DevInput<TouchCallbacks>::setCurrentSlot(uint32_t slot)
{
	if (slot >= mContacts.size())
	{
		mContacts.resize(slot + 1, {-1});
	}

	mCurrentSlot = slot;
}

void DevInput<TouchCallbacks>::onAbsEvent(const input_event& event)
{
	switch(event.code)
	{
		case ABS_X:
		case ABS_MT_POSITION_X:
			mContacts[mCurrentSlot].absX = event.value;
			mSendAbs = true;
			break;

		case ABS_Y:
		case ABS_MT_POSITION_Y:
			mContacts[mCurrentSlot].absY = event.value;
			mSendAbs = true;
			break;

		case ABS_MT_SLOT:
			flushEvents();
			setCurrentSlot(event.value);
			break;

		case ABS_MT_TRACKING_ID:
			if (event.value >= 0)
			{
				mContacts[mCurrentSlot].id = event.value;
				mKey = 1;
				mSendKey = true;
			}
			else
			{
				mKey = 0;
				mSendKey = true;

				flushEvents();
			}
			break;
	}
}

void DevInput<TouchCallbacks>::onKeyEvent(const input_event& event)
{
	mKey = event.value;
	mSendKey = true;
}

void DevInput<TouchCallbacks>::onSynEvent(const input_event& event)
{
	if (event.code == SYN_MT_REPORT)
	{
		int32_t nextSlot = mCurrentSlot;

		if (mSendAbs)
		{
			nextSlot = mCurrentSlot + 1;
		}

		if (mContacts[mCurrentSlot].id == -1)
		{
			mKey = 1;
			mSendKey = true;
		}

		flushEvents();

		setCurrentSlot(nextSlot);

		mMtReport = true;
	}

	if (event.code == SYN_REPORT)
	{
		if (mMtReport)
		{
			for(int32_t i = mCurrentSlot; i < mLastMtReports; i++)
			{
				setCurrentSlot(i);
				mKey = 0;
				mSendKey = true;

				flushEvents();
			}

			mLastMtReports = mCurrentSlot;

			setCurrentSlot(0);
			mMtReport = false;
		}

		lock_guard<mutex> lock(mMutex);

		flushEvents();

		if (mCallbacks.frame)
		{
			LOG(mLog, DEBUG) << mName << ", frame";

			mCallbacks.frame(mCurrentSlot);
		}
	}
}

void DevInput<TouchCallbacks>::flushEvents()
{
	if (mSendKey)
	{
		if ((mKey == 1) && mCallbacks.down)
		{
			LOG(mLog, DEBUG) << mName << ", down"
							 << ", id: " << mCurrentSlot
							 << ", abs x: " << mContacts[mCurrentSlot].absX
							 << ", abs y: " << mContacts[mCurrentSlot].absY;

			mCallbacks.down(mCurrentSlot,
							mContacts[mCurrentSlot].absX,
							mContacts[mCurrentSlot].absY);

			mSendAbs = false;
		}

		if ((mKey == 0) && mCallbacks.up)
		{
			LOG(mLog, DEBUG) << mName << ", up"
							 << ", id: " << mCurrentSlot;

			mCallbacks.up(mCurrentSlot);

			mContacts[mCurrentSlot].id = -1;
		}

		mSendKey = false;
	}

	if (mSendAbs && mCallbacks.motion)
	{
		LOG(mLog, DEBUG) << mName << ", motion"
						 << ", id: " << mCurrentSlot
						 << ", abs x: " << mContacts[mCurrentSlot].absX
						 << ", abs y: " << mContacts[mCurrentSlot].absY;

		mSendAbs = false;

		mCallbacks.motion(mCurrentSlot,
						  mContacts[mCurrentSlot].absX,
						  mContacts[mCurrentSlot].absY);
	}
}
