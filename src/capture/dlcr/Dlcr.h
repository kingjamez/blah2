/// @file Dlcr.h
/// @class Dlcr
/// @brief A class to capture data on the Dragon Labs CR-8 coherent receiver.
/// @details The CR-8 is an 8-channel coherent SDR with a fixed 25 MHz sample
///          rate. Two of its channels are mapped to the Blah2 reference and
///          surveillance buffers. All 8 channels share a common clock, so
///          phase coherence is guaranteed after calling dlcr_set_freq() with
///          calibrate=true (done automatically in start()).
/// @author blah2 contributors

#ifndef DLCR_H
#define DLCR_H

#include "capture/Source.h"
#include "data/IqData.h"

#include <stdint.h>
#include <string>
#include <fstream>
#include "dlcr_api.h"

class Dlcr : public Source
{
private:

  /// @brief Serial number of the CR-8 device. Empty string = first found.
  std::string serial;

  /// @brief Zero-based index of the reference channel (0-7).
  int refChannel;

  /// @brief Zero-based index of the surveillance channel (0-7).
  int surChannel;

  /// @brief LNA gain stage, valid range 0-14.
  int gainLna;

  /// @brief Mixer gain stage, valid range 0-15.
  int gainMixer;

  /// @brief VGA (baseband) gain stage, valid range 0-15.
  int gainVga;

  /// @brief True to lock the device to an external 10 MHz reference clock.
  bool externalClock;

  /// @brief Handle to the open CR-8 device.
  dlcr_t *dev;

  /// @brief Context structure forwarded to the static sample callback.
  struct CallbackContext {
    IqData *buffer1;
    IqData *buffer2;
    int refChannel;
    int surChannel;
    bool *saveIq;
    std::ofstream *saveIqFile;
  };

  /// @brief Callback context instance owned by this object.
  CallbackContext cbCtx;

  /// @brief Throw a runtime_error if status != 0.
  void check_status(int status, const std::string &message);

  /// @brief libdlcr sample callback.
  static void rx_callback(dlcr_complex_t *samples[DLCR_CHANNEL_COUNT],
                           size_t count, size_t drops, void *ctx);

public:

  Dlcr(std::string type, uint32_t fc, uint32_t fs, std::string path,
       bool *saveIq, std::string serial, int refChannel, int surChannel,
       int gainLna, int gainMixer, int gainVga, bool externalClock);

  void start() override;
  void process(IqData *buffer1, IqData *buffer2) override;
  void stop() override;
  void replay(IqData *buffer1, IqData *buffer2,
              std::string file, bool loop) override;

};

#endif // DLCR_H
