#include <csignal>
#include <execinfo.h>
#include <getopt.h>
#include <unistd.h>

#include <xen/be/Log.hpp>

#include "wayland/Display.hpp"
#include "input/WlInput.hpp"

using XenBackend::Log;
/*******************************************************************************
 *
 ******************************************************************************/

static void touchDown(int32_t id, int32_t x, int32_t y)
{
	LOG("TOUCH", DEBUG) << "=== DOWN id: " << id << ", x " << x << ", y: " << y;
}

static void touchUp(int32_t id)
{
	LOG("TOUCH", DEBUG) << "=== UP id: " << id;
}

static void touchMotion(int32_t id, int32_t x, int32_t y)
{
	LOG("TOUCH", DEBUG) << "=== MOTION id: " << id << ", x " << x << ", y: " << y;
}

static void touchFrame(int32_t id)
{
	LOG("TOUCH", DEBUG) << "=== FRAME id: " << id;
}

void segmentationHandler(int sig)
{
	void *array[20];
	size_t size;

	LOG("Main", ERROR) << "Segmentation fault!";

	size = backtrace(array, 20);

	backtrace_symbols_fd(array, size, STDERR_FILENO);

	exit(1);
}

void registerSignals()
{
	signal(SIGSEGV, segmentationHandler);
}

void waitSignals()
{
	sigset_t set;
	int signal;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigprocmask(SIG_BLOCK, &set, nullptr);

	sigwait(&set,&signal);
}

int main(int argc, char *argv[])
{
	Log::setLogLevel("Debug");

	LOG("Main", DEBUG) << "Start";

	try
	{
		registerSignals();

		auto display = Wayland::DisplayPtr(new Wayland::Display());

		display->start();

		auto con1 = display->createConnector("1000");
		auto con2 = display->createConnector("1001");

		auto db1 = display->createDisplayBuffer(800, 600, 32);
		auto fb1 = display->createFrameBuffer(db1, 800, 600, 1);

		auto db2 = display->createDisplayBuffer(300, 200, 32);
		auto fb2 = display->createFrameBuffer(db2, 300, 200, 1);

		uint32_t *uiBegin = static_cast<uint32_t*>(db1->getBuffer());
		uint32_t *uiEnd = uiBegin + db1->getSize() / sizeof(uint32_t);
		std::for_each(uiBegin, uiEnd, [](uint32_t &v){v = 0xFFAACC;});

		memset(db1->getBuffer(), db1->getSize(), 0x11);
		memset(db2->getBuffer(), db2->getSize(), 0xAA);

		con1->init(800, 600, fb1);
		con2->init(300, 200, fb2);

		display->flush();

		WlTouch touch(display, "1000");
		touch.setCallbacks({touchDown, touchUp, touchMotion, touchFrame});

		waitSignals();

		display->stop();
	}
	catch(const std::exception& e)
	{
		LOG("Main", ERROR) << e.what();
	}

	LOG("Main", DEBUG) << "Stop";

	return 0;
}
