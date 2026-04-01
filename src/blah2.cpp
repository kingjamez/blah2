/// @file blah2.cpp
/// @brief A real-time passive radar — headless remote node.
/// @details Captures IQ data, runs the full DSP pipeline, and forwards
///          all six data streams to a remote aggregator via TCP/JSON.
///          No web UI, no Node.js API middleware.
/// @author 30hours

#include "capture/Capture.h"
#include "data/IqData.h"
#include "data/Map.h"
#include "data/Detection.h"
#include "data/Track.h"
#include "data/meta/Timing.h"
#include "process/ambiguity/Ambiguity.h"
#include "process/clutter/WienerHopf.h"
#include "process/detection/CfarDetector1D.h"
#include "process/detection/Centroid.h"
#include "process/detection/Interpolate.h"
#include "process/tracker/Tracker.h"
#include "process/utility/Socket.h"
#include "data/meta/Constants.h"

#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>
#include <c4/format.hpp>
#include <sys/types.h>
#include <sys/time.h>
#include <getopt.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <atomic>
#include <memory>
#include <iostream>

Capture *CAPTURE_POINTER = NULL;
std::unique_ptr<Socket> socket_map;
std::unique_ptr<Socket> socket_detection;
std::unique_ptr<Socket> socket_track;
std::unique_ptr<Socket> socket_timestamp;
std::unique_ptr<Socket> socket_timing;
std::unique_ptr<Socket> socket_iqdata;

void signal_callback_handler(int signum);
void getopt_print_help();
std::string getopt_process(int argc, char **argv);
std::string ryml_get_file(const char *filename);
uint64_t current_time_ms();
uint64_t current_time_us();
void timing_helper(std::vector<std::string>& timing_name,
  std::vector<double>& timing_time, std::vector<uint64_t>& time_us,
  std::string name);

int main(int argc, char **argv)
{
  // input handling
  signal(SIGTERM, signal_callback_handler);
  std::string file = getopt_process(argc, argv);
  std::ifstream filePath(file);
  if (!filePath.is_open())
  {
    std::cout << "Error: Config file does not exist." << "\n";
    exit(1);
  }

  // config handling
  std::string contents = ryml_get_file(file.c_str());
  ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(contents));

  // set up capture
  uint32_t fs, fc;
  std::string type, replayFile;
  bool state, loop;
  tree["capture"]["fs"] >> fs;
  tree["capture"]["fc"] >> fc;
  tree["capture"]["device"]["type"] >> type;
  tree["capture"]["replay"]["state"] >> state;
  tree["capture"]["replay"]["loop"] >> loop;
  tree["capture"]["replay"]["file"] >> replayFile;

  // set up sockets — all six data streams
  sleep(2);
  uint16_t port_map, port_detection, port_track,
           port_timestamp, port_timing, port_iqdata;
  std::string ip;
  tree["network"]["ports"]["map"]       >> port_map;
  tree["network"]["ports"]["detection"] >> port_detection;
  tree["network"]["ports"]["track"]     >> port_track;
  tree["network"]["ports"]["timestamp"] >> port_timestamp;
  tree["network"]["ports"]["timing"]    >> port_timing;
  tree["network"]["ports"]["iqdata"]    >> port_iqdata;
  tree["network"]["ip"] >> ip;

  try {
    socket_map       = std::make_unique<Socket>(ip, port_map);
    socket_detection = std::make_unique<Socket>(ip, port_detection);
    socket_track     = std::make_unique<Socket>(ip, port_track);
    socket_timestamp = std::make_unique<Socket>(ip, port_timestamp);
    socket_timing    = std::make_unique<Socket>(ip, port_timing);
    socket_iqdata    = std::make_unique<Socket>(ip, port_iqdata);
  } catch (const std::exception& e) {
    std::cerr << "Failed to connect to aggregator at " << ip << ": "
              << e.what() << "\n";
    return 1;
  }

  // set up fftw multithread
  if (fftw_init_threads() == 0)
  {
    std::cout << "Error in FFTW multithreading." << "\n";
    return -1;
  }
  fftw_plan_with_nthreads(4);

  Capture *capture = new Capture(type, fs, fc);
  CAPTURE_POINTER = capture;
  if (state)
  {
    capture->set_replay(loop, replayFile);
  }

  // create shared queue
  double tCpi, tBuffer;
  tree["process"]["data"]["cpi"] >> tCpi;
  tree["process"]["data"]["buffer"] >> tBuffer;
  IqData *buffer1 = new IqData((int) (tCpi*tBuffer*fs));
  IqData *buffer2 = new IqData((int) (tCpi*tBuffer*fs));

  // run capture
  std::thread t1([&]{capture->process(buffer1, buffer2,
    tree["capture"]["device"]);
  });

  // set up process CPI
  uint32_t nSamples = fs * tCpi;
  IqData *x = new IqData(nSamples);
  IqData *y = new IqData(nSamples);
  Map<std::complex<double>> *map;
  std::unique_ptr<Detection> detection;
  std::unique_ptr<Detection> detection1;
  std::unique_ptr<Detection> detection2;
  std::unique_ptr<Track> track;

  // set up process ambiguity
  int32_t delayMin, delayMax;
  int32_t dopplerMin, dopplerMax;
  bool roundHamming = true;
  tree["process"]["ambiguity"]["delayMin"] >> delayMin;
  tree["process"]["ambiguity"]["delayMax"] >> delayMax;
  tree["process"]["ambiguity"]["dopplerMin"] >> dopplerMin;
  tree["process"]["ambiguity"]["dopplerMax"] >> dopplerMax;
  Ambiguity *ambiguity = new Ambiguity(delayMin, delayMax,
    dopplerMin, dopplerMax, fs, nSamples, roundHamming);

  // set up process clutter
  int32_t delayMinClutter, delayMaxClutter;
  tree["process"]["clutter"]["delayMin"] >> delayMinClutter;
  tree["process"]["clutter"]["delayMax"] >> delayMaxClutter;
  WienerHopf *filter = new WienerHopf(delayMinClutter, delayMaxClutter, nSamples);

  // set up process detection
  double pfa, minDoppler;
  int8_t nGuard, nTrain;
  int8_t minDelay;
  tree["process"]["detection"]["pfa"] >> pfa;
  tree["process"]["detection"]["nGuard"] >> nGuard;
  tree["process"]["detection"]["nTrain"] >> nTrain;
  tree["process"]["detection"]["minDelay"] >> minDelay;
  tree["process"]["detection"]["minDoppler"] >> minDoppler;
  CfarDetector1D *cfarDetector1D = new CfarDetector1D(pfa, nGuard, nTrain, minDelay, minDoppler);
  Interpolate *interpolate = new Interpolate(true, true);

  // set up process centroid
  uint16_t nCentroid;
  tree["process"]["detection"]["nCentroid"] >> nCentroid;
  Centroid *centroid = new Centroid(nCentroid, nCentroid, 1/tCpi);

  // set up process tracker
  uint8_t m, n, nDelete;
  double maxAcc, rangeRes, lambda;
  tree["process"]["tracker"]["initiate"]["M"] >> m;
  tree["process"]["tracker"]["initiate"]["N"] >> n;
  tree["process"]["tracker"]["delete"] >> nDelete;
  tree["process"]["tracker"]["initiate"]["maxAcc"] >> maxAcc;
  rangeRes = (double)Constants::c/fs;
  lambda = (double)Constants::c/fc;
  Tracker *tracker = new Tracker(m, n, nDelete, ambiguity->get_cpi(), maxAcc, rangeRes, lambda);

  // process options
  bool isClutter, isDetection, isTracker;
  tree["process"]["clutter"]["enable"] >> isClutter;
  tree["process"]["detection"]["enable"] >> isDetection;
  tree["process"]["tracker"]["enable"] >> isTracker;
  if (!isDetection)
  {
    isTracker = false;
  }

  // set up timing
  uint64_t tStart = current_time_ms();
  Timing *timing = new Timing(tStart);
  std::vector<std::string> timing_name;
  std::vector<double> timing_time;
  std::vector<uint64_t> time;

  // output json
  std::string mapJson, detectionJson, jsonTracker, jsonTiming, jsonIqData;

  // run process
  std::thread t2([&]{
      while (true)
      {
        buffer1->lock();
        buffer2->lock();
        if ((buffer1->get_length() > nSamples) && (buffer2->get_length() > nSamples))
        {
          time.push_back(current_time_us());

          // extract data from buffer
          for (uint32_t i = 0; i < nSamples; i++)
          {
            x->push_back(buffer1->pop_front());
            y->push_back(buffer2->pop_front());
          }
          buffer1->unlock();
          buffer2->unlock();
          timing_helper(timing_name, timing_time, time, "extract_buffer");

          // clutter filter
          if (isClutter)
          {
            if (!filter->process(x, y))
            {
              continue;
            }
            timing_helper(timing_name, timing_time, time, "clutter_filter");
          }

          // ambiguity process
          map = ambiguity->process(x, y);
          map->set_metrics();
          timing_helper(timing_name, timing_time, time, "ambiguity_processing");

          // detection process
          if (isDetection)
          {
            detection1 = cfarDetector1D->process(map);
            detection2 = centroid->process(detection1.get());
            detection = interpolate->process(detection2.get(), map);
            timing_helper(timing_name, timing_time, time, "detector");
          }

          // tracker process
          if (isTracker)
          {
            track = tracker->process(detection.get(), time[0]/1000);
            timing_helper(timing_name, timing_time, time, "tracker");
          }

          // output IqData metadata
          jsonIqData = x->to_json(time[0]/1000);
          socket_iqdata->sendData(jsonIqData);

          // output map data
          mapJson = map->to_json(time[0]/1000);
          mapJson = map->delay_bin_to_km(mapJson, fs);
          socket_map->sendData(mapJson);

          // output detection data
          if (isDetection)
          {
            detectionJson = detection->to_json(time[0]/1000);
            detectionJson = detection->delay_bin_to_km(detectionJson, fs);
            socket_detection->sendData(detectionJson);
          }

          // output tracker data
          if (isTracker)
          {
            jsonTracker = track->to_json(time[0]/1000);
            socket_track->sendData(jsonTracker);
          }

          timing_helper(timing_name, timing_time, time, "output_radar_data");

          // CPI timer
          time.push_back(current_time_us());
          double delta_ms = (double)(time.back()-time[0]) / 1000;
          timing_name.push_back("cpi");
          timing_time.push_back(delta_ms);
          std::cout << "CPI time (ms): " << delta_ms << "\n";

          // output timing data
          timing->update(time[0]/1000, timing_time, timing_name);
          jsonTiming = timing->to_json();
          socket_timing->sendData(jsonTiming);
          timing_time.clear();
          timing_name.clear();

          // output CPI timestamp
          std::string t0_string = std::to_string(time[0]/1000);
          socket_timestamp->sendData(t0_string);
          time.clear();
        }
        else
        {
          buffer1->unlock();
          buffer2->unlock();
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
    });
  t2.join();
  t1.join();

  return 0;
}

void signal_callback_handler(int signum) {
  std::cout << "Caught signal " << signum << "\n";
  if (CAPTURE_POINTER != nullptr)
  {
    CAPTURE_POINTER->device->kill();
  }
  else
  {
    exit(0);
  }
}

void getopt_print_help()
{
  std::cout << "--config <file.yml>: 	Set number of program\n"
               "--help:              	Show help\n";
  exit(1);
}

std::string getopt_process(int argc, char **argv)
{
  const char *const short_opts = "c:h";
  const option long_opts[] = {
      {"config", required_argument, nullptr, 'c'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, no_argument, nullptr, 0}};

  if (argc == 1)
  {
    std::cout << "Error: No arguments provided." << "\n";
    exit(1);
  }

  std::string file;

  while (true)
  {
    const auto opt = getopt_long(argc, argv, short_opts, long_opts, nullptr);

    if ((argc == 2) && (-1 == opt))
    {
      std::cout << "Error: No arguments provided." << "\n";
      exit(1);
    }

    if (-1 == opt)
      break;

    switch (opt)
    {
    case 'c':
      file = std::string(optarg);
      break;

    case 'h':
      getopt_print_help();

    case '?':
      exit(1);

    default:
      break;
    }
  }

  return file;
}

std::string ryml_get_file(const char *filename)
{
  std::ifstream in(filename, std::ios::in | std::ios::binary);
  if (!in)
  {
    std::cerr << "could not open " << filename << "\n";
    exit(1);
  }
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

uint64_t current_time_ms()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>
  (std::chrono::system_clock::now().time_since_epoch()).count();
}

uint64_t current_time_us()
{
  return std::chrono::duration_cast<std::chrono::microseconds>
  (std::chrono::system_clock::now().time_since_epoch()).count();
}

void timing_helper(std::vector<std::string>& timing_name,
  std::vector<double>& timing_time, std::vector<uint64_t>& time_us,
  std::string name)
{
  time_us.push_back(current_time_us());
  double delta_ms = (double)(time_us.back()-time_us[time_us.size()-2]) / 1000;
  timing_name.push_back(name);
  timing_time.push_back(delta_ms);
}
