/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2011 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */

 /** @file Protonect.cpp Main application file. */

#include <iostream>
#include <cstdlib>
#include <signal.h>
#include <thread>

/// [headers]
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/registration.h>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/logger.h>
/// [headers]
#ifdef EXAMPLES_WITH_OPENGL_SUPPORT
#include "viewer.h"
#endif

static bool protonect_shutdown = false; ///< Whether the running application should shut down.

void running(libfreenect2::Freenect2Device * dev);

bool viewer_enabled = true;
bool enable_rgb = true;
bool enable_depth = true;

static size_t framemax = -1;

void sigint_handler(int s) {
	protonect_shutdown = true;
}

bool protonect_paused = false;
libfreenect2::Freenect2Device *devtopause_1;
libfreenect2::Freenect2Device *devtopause_2;

//Doing non-trivial things in signal handler is bad. If you want to pause,
//do it in another thread.
//Though libusb operations are generally thread safe, I cannot guarantee
//everything above is thread safe when calling start()/stop() while
//waitForNewFrame().
void sigusr1_handler(int s) {
	if (devtopause_1 == 0 || devtopause_2 == 0) {
		return;
	}
	/// [pause]
	if (protonect_paused) {
		devtopause_1->start();
		devtopause_2->start();
	}
	else {
		devtopause_1->stop();
		devtopause_2->stop();
	}

	protonect_paused = !protonect_paused;
	/// [pause]
}

//create a custom logger
/// [logger]
#include <fstream>
#include <cstdlib>
class MyFileLogger : public libfreenect2::Logger {

private:
	std::ofstream logfile_;
public:
	MyFileLogger(const char *filename) {
		if (filename) {
			logfile_.open(filename);
		}
		level_ = Debug;
	}

	bool good() {
		return logfile_.is_open() && logfile_.good();
	}

	virtual void log(Level level, const std::string &message) {
		logfile_ << "[" << libfreenect2::Logger::level2str(level) << "] " << message << std::endl;
	}
};
/// [logger]

/// [main]
/**
 * Main application entry point.
 *
 * Accepted argumemnts:
 * - cpu Perform depth processing with the CPU.
 * - gl  Perform depth processing with OpenGL.
 * - cl  Perform depth processing with OpenCL.
 * - <number> Serial number of the device to open.
 * - -noviewer Disable viewer window.
 */
int main(int argc, char *argv[]) {
/// [main]
	std::string program_path(argv[0]);
	std::cerr << "Version: " << LIBFREENECT2_VERSION << std::endl;
	std::cerr << "Environment variables: LOGFILE=<protonect.log>" << std::endl;
	std::cerr << "Usage: " << program_path << " [-gpu=<id>] [gl | cl | clkde | cuda | cudakde | cpu] [<device serial>]" << std::endl;
	std::cerr << "        [-noviewer] [-norgb | -nodepth] [-help] [-version]" << std::endl;
	std::cerr << "        [-frames <number of frames to process>]" << std::endl;
	std::cerr << "To pause and unpause: pkill -USR1 Protonect" << std::endl;
	size_t executable_name_idx = program_path.rfind("Protonect");

	std::string binpath = "/";

	if (executable_name_idx != std::string::npos) {
		binpath = program_path.substr(0, executable_name_idx);
	}

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
	// avoid flooing the very slow Windows console with debug messages
	libfreenect2::setGlobalLogger(libfreenect2::createConsoleLogger(libfreenect2::Logger::Info));
#else
	// create a console logger with debug level (default is console logger with info level)
  /// [logging]
	libfreenect2::setGlobalLogger(libfreenect2::createConsoleLogger(libfreenect2::Logger::Debug));
	/// [logging]
#endif
/// [file logging]
	MyFileLogger *filelogger = new MyFileLogger(getenv("LOGFILE"));
	if (filelogger->good()) {
		libfreenect2::setGlobalLogger(filelogger);
	}
	else {
		delete filelogger;
	}
	/// [file logging]

	////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////

	libfreenect2::Freenect2 freenect2;
	
	libfreenect2::PacketPipeline *pipeline_1 = NULL;
	libfreenect2::PacketPipeline *pipeline_2 = NULL;
	
	std::string serial_1 = "";
	std::string serial_2 = "";
	
	libfreenect2::Freenect2Device *dev_1 = 0;
	libfreenect2::Freenect2Device *dev_2 = 0;

	int deviceId = -1;

	for (int argI = 1; argI < argc; ++argI) {
		const std::string arg(argv[argI]);

		if (arg == "-help" || arg == "--help" || arg == "-h" || arg == "-v" || arg == "--version" || arg == "-version") {
			return 0; // Just let the initial lines display at the beginning of main
		}
		else if (arg.find("-gpu=") == 0) {
			if (pipeline_1 || pipeline_2) {
				std::cerr << "-gpu must be specified before pipeline argument" << std::endl;
				return -1;
			}
			deviceId = atoi(argv[argI] + 5);
		}
		else if (arg == "cpu") {
			if (!pipeline_1 || !pipeline_2) {
				pipeline_1 = new libfreenect2::CpuPacketPipeline();
				pipeline_2 = new libfreenect2::CpuPacketPipeline();
			}
		}
		else if (arg == "gl") {
#ifdef LIBFREENECT2_WITH_OPENGL_SUPPORT
			if (!pipeline_1 || !pipeline_2) {
				pipeline_1 = new libfreenect2::OpenGLPacketPipeline();
				pipeline_2 = new libfreenect2::OpenGLPacketPipeline();
			}
#else
			std::cout << "OpenGL pipeline is not supported!" << std::endl;
#endif
		}
		else if (arg == "cl") {
#ifdef LIBFREENECT2_WITH_OPENCL_SUPPORT
			if (!pipeline_1 || !pipeline_2)
				pipeline = new libfreenect2::OpenCLPacketPipeline(deviceId);
#else
			std::cout << "OpenCL pipeline is not supported!" << std::endl;
#endif
		}
		else if (arg == "clkde") {
#ifdef LIBFREENECT2_WITH_OPENCL_SUPPORT
			if (!pipeline)
				pipeline = new libfreenect2::OpenCLKdePacketPipeline(deviceId);
#else
			std::cout << "OpenCL pipeline is not supported!" << std::endl;
#endif
		}
		else if (arg == "cuda") {
#ifdef LIBFREENECT2_WITH_CUDA_SUPPORT
			if (!pipeline_1 || !pipeline_2) {
				pipeline_1 = new libfreenect2::CudaPacketPipeline(deviceId);
				pipeline_2 = new libfreenect2::CudaPacketPipeline(deviceId);
			}
#else
			std::cout << "CUDA pipeline is not supported!" << std::endl;
#endif
		}
		else if (arg == "cudakde") {
#ifdef LIBFREENECT2_WITH_CUDA_SUPPORT
			if (!pipeline_1 || !pipeline_2) {
				pipeline_1 = new libfreenect2::CudaKdePacketPipeline(deviceId);
				pipeline_2 = new libfreenect2::CudaKdePacketPipeline(deviceId);
			}
#else
			std::cout << "CUDA pipeline is not supported!" << std::endl;
#endif
		}
		else if (arg.find_first_not_of("0123456789") == std::string::npos) { //check if parameter could be a serial number
			//serial = arg;
		}
		else if (arg == "-noviewer" || arg == "--noviewer") {
			viewer_enabled = false;
		}
		else if (arg == "-norgb" || arg == "--norgb") {
			enable_rgb = false;
		}
		else if (arg == "-nodepth" || arg == "--nodepth") {
			enable_depth = false;
		}
		else if (arg == "-frames") {
			++argI;
			framemax = strtol(argv[argI], NULL, 0);
			if (framemax <= 0) {
				std::cerr << "invalid frame count '" << argv[argI] << "'" << std::endl;
				return -1;
			}
		}
		else {
			std::cout << "Unknown argument: " << arg << std::endl;
		}
	} //end for

	//////////////////////////////////////////////////
	if (!enable_rgb && !enable_depth) {
		std::cerr << "Disabling both streams is not allowed!" << std::endl;
		return -1;
	}

	/// [discovery]
	if (freenect2.enumerateDevices() == 0) {
		std::cout << "no device connected!" << std::endl;
		return -1;
	}
	else if (freenect2.enumerateDevices() == 1) {
		std::cout << "only one device is connected" << std::endl;
		return -1;
	}

	if (serial_1 == "" || serial_2 == "") {
		serial_1 = freenect2.getDeviceSerialNumber(0);
		serial_2 = freenect2.getDeviceSerialNumber(1);
	}
	
	/// [open]
	if (pipeline_1 && pipeline_2) {	
		dev_1 = freenect2.openDevice(serial_1, pipeline_1);
		dev_2 = freenect2.openDevice(serial_2, pipeline_2);
	}
	else {
		dev_1 = freenect2.openDevice(serial_1);
		dev_2 = freenect2.openDevice(serial_2);
	}

	if (dev_1 == 0 || dev_2 == 0) {
		std::cout << "failure opening device or devices!" << std::endl;
		return -1;
	}
	std::cout << "Successfully opened the devices" << std::endl;

	devtopause_1 = dev_1;
	devtopause_2 = dev_2;

	signal(SIGINT, sigint_handler);
#ifdef SIGUSR1
	signal(SIGUSR1, sigusr1_handler);
#endif
	protonect_shutdown = false;

	//libfreenect2::Frame undistorted(512, 424, 4), registered(512, 424, 4);
	//todo

	/// [loop start]
	std::thread t1(running, dev_1);
	std::thread t2(running, dev_2);
	/// [loop end]

	  // TODO: restarting ir stream doesn't work!
	  // TODO: bad things will happen, if frame listeners are freed before dev->stop() :(
	/// [stop]
	t1.join();
	t2.join();

	dev_1->stop();
	dev_2->stop();
	dev_1->close();
	dev_2->close();

	return 0;
}

void running(libfreenect2::Freenect2Device * dev) {
	
	int types = 0;
	if (enable_rgb) {
		types |= libfreenect2::Frame::Color;
	}
	if (enable_depth) {
		types |= libfreenect2::Frame::Ir | libfreenect2::Frame::Depth;
	}

	/// [start]
	if (enable_rgb && enable_depth) {
		if ( !dev->start() ) {
			return;
		}
	}
	else {
		if (!dev->startStreams(enable_rgb, enable_depth) ) {
			return;
		}
	}

	/// [listeners]
	libfreenect2::SyncMultiFrameListener listener(types);
	libfreenect2::FrameMap frames;

	dev->setColorFrameListener(&listener);
	dev->setIrAndDepthFrameListener(&listener);

	std::cout << "device serial : " << dev->getSerialNumber() << std::endl;
	std::cout << "device firmware : " << dev->getFirmwareVersion() << std::endl;

	/// [registration setup]
	libfreenect2::Registration * registration =
		new libfreenect2::Registration(dev->getIrCameraParams(), dev->getColorCameraParams());

#ifdef EXAMPLES_WITH_OPENGL_SUPPORT
	Viewer viewer;
	if (viewer_enabled) {
		viewer.initialize();
	}
#else
	viewer_enabled = false;
#endif

	size_t framecount = 0;
	libfreenect2::Frame undistorted(512, 424, 4), registered(512, 424, 4);

	while (!protonect_shutdown && (framemax == (size_t)-1 || framecount < framemax)) {
		if (!listener.waitForNewFrame(frames, 10 * 1000)) { // 10 sconds
			std::cout << "timeout!" << std::endl;
			return;
		}
		libfreenect2::Frame *rgb = frames[libfreenect2::Frame::Color];
		libfreenect2::Frame *ir = frames[libfreenect2::Frame::Ir];
		libfreenect2::Frame *depth = frames[libfreenect2::Frame::Depth];
		/// [loop start]

		if (enable_rgb && enable_depth) {
			/// [registration]
			registration->apply(rgb, depth, &undistorted, &registered);
		}

		framecount++;
		if (!viewer_enabled) {
			if (framecount % 100 == 0) {
				std::cout << "The viewer is turned off. Received " << framecount << " frames. Ctrl-C to stop." << std::endl;
			}
			listener.release(frames);
			continue;
		}

#ifdef EXAMPLES_WITH_OPENGL_SUPPORT
		if (enable_rgb) {
			viewer.addFrame("RGB", rgb);
		}
		if (enable_depth) {
			viewer.addFrame("ir", ir);
			viewer.addFrame("depth", depth);
		}
		if (enable_rgb && enable_depth) {
			viewer.addFrame("registered", &registered);
		}

		protonect_shutdown = protonect_shutdown || viewer.render();
#endif

		/// [loop end]
		listener.release(frames);

	}//end while

	delete registration;

} //end running()