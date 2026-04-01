#include "Capture.h"
#include "rspduo/RspDuo.h"
#include "usrp/Usrp.h"
#include "hackrf/HackRf.h"
#include "kraken/Kraken.h"
#include <iostream>
#include <thread>

// constants
const std::string Capture::VALID_TYPE[4] = {"RspDuo", "Usrp", "HackRF", "Kraken"};

// constructor — headless: no path or saveIq needed
Capture::Capture(std::string _type, uint32_t _fs, uint32_t _fc)
{
  type = _type;
  fs = _fs;
  fc = _fc;
  replay = false;
}

void Capture::process(IqData *buffer1, IqData *buffer2, c4::yml::NodeRef config)
{
  std::cout << "Setting up device " + type << std::endl;

  device = factory_source(type, config);

  if (!replay)
  {
    device->start();
    device->process(buffer1, buffer2);
  }
  else
  {
    device->replay(buffer1, buffer2, file, loop);
  }
}

std::unique_ptr<Source> Capture::factory_source(const std::string& type, c4::yml::NodeRef config)
{
    // IQ saving is disabled in headless mode — pass a static false flag.
    static bool saveIqDisabled = false;

    // SDRplay RSPduo
    if (type == VALID_TYPE[0])
    {
        int agcSetPoint, bandwidthNumber, gainReductionA, gainReductionB, lnaState;
        bool dabNotch, rfNotch;
        config["agcSetPoint"] >> agcSetPoint;
        config["bandwidthNumber"] >> bandwidthNumber;
        config["gainReduction"][0] >> gainReductionA;
        config["gainReduction"][1] >> gainReductionB;
        config["lnaState"] >> lnaState;
        config["dabNotch"] >> dabNotch;
        config["rfNotch"] >> rfNotch;
        return std::make_unique<RspDuo>(type, fc, fs, "", &saveIqDisabled,
          agcSetPoint, bandwidthNumber, gainReductionA, gainReductionB, lnaState,
          dabNotch, rfNotch);
    }
    // Usrp
    else if (type == VALID_TYPE[1])
    {
        std::string address, subdev;
        std::vector<std::string> antenna;
        std::vector<double> gain;
        std::string _antenna;
        double _gain;
        config["address"] >> address;
        config["subdev"] >> subdev;
        config["antenna"][0] >> _antenna;
        antenna.push_back(_antenna);
        config["antenna"][1] >> _antenna;
        antenna.push_back(_antenna);
        config["gain"][0] >> _gain;
        gain.push_back(_gain);
        config["gain"][1] >> _gain;
        gain.push_back(_gain);
        return std::make_unique<Usrp>(type, fc, fs, "", &saveIqDisabled,
          address, subdev, antenna, gain);
    }
    // HackRF
    else if (type == VALID_TYPE[2])
    {
      std::vector<std::string> serial;
      std::vector<uint32_t> gainLna, gainVga;
      std::vector<bool> ampEnable;
      std::string _serial;
      uint32_t gain;
      int _gain;
      bool _ampEnable;
      config["serial"][0] >> _serial;
      serial.push_back(_serial);
      config["serial"][1] >> _serial;
      serial.push_back(_serial);
      config["gain_lna"][0] >> _gain;
      gain = static_cast<uint32_t> (_gain);
      gainLna.push_back(gain);
      config["gain_lna"][1] >> _gain;
      gain = static_cast<uint32_t>(_gain);
      gainLna.push_back(gain);
      config["gain_vga"][0] >> _gain;
      gain = static_cast<uint32_t>(_gain);
      gainVga.push_back(gain);
      config["gain_vga"][1] >> _gain;
      gain = static_cast<uint32_t>(_gain);
      gainVga.push_back(gain);
      config["amp_enable"][0] >> _ampEnable;
      ampEnable.push_back(_ampEnable);
      config["amp_enable"][1] >> _ampEnable;
      ampEnable.push_back(_ampEnable);
      return std::make_unique<HackRf>(type, fc, fs, "", &saveIqDisabled,
        serial, gainLna, gainVga, ampEnable);
    }
    // Kraken
    else if (type == VALID_TYPE[3])
    {
      std::vector<double> gain;
      float _gain;
      for (auto child : config["gain"].children())
      {
        c4::atof(child.val(), &_gain);
        gain.push_back(static_cast<double>(_gain));
      }
      return std::make_unique<Kraken>(type, fc, fs, "", &saveIqDisabled, gain);
    }
    // handle unknown type
    std::cerr << "Error: Source type does not exist." << std::endl;
    return nullptr;
}

void Capture::set_replay(bool _loop, std::string _file)
{
  replay = true;
  loop = _loop;
  file = _file;
}
