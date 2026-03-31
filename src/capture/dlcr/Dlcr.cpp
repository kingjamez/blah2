#include "Dlcr.h"

#include <iostream>
#include <complex>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Dlcr::Dlcr(std::string _type, uint32_t _fc, uint32_t _fs,
            std::string _path, bool *_saveIq,
            std::string _serial, int _refChannel, int _surChannel,
            int _gainLna, int _gainMixer, int _gainVga, bool _externalClock)
    : Source(_type, _fc, static_cast<uint32_t>(DLCR_SAMPLE_RATE), _path, _saveIq)
{
  // The CR-8 has a fixed 25 MHz sample rate; warn if caller requested otherwise
  if (_fs != static_cast<uint32_t>(DLCR_SAMPLE_RATE))
  {
    std::cerr << "[Dlcr] Warning: CR-8 has a fixed sample rate of "
              << static_cast<uint32_t>(DLCR_SAMPLE_RATE)
              << " Hz. Ignoring requested fs=" << _fs << std::endl;
  }

  // Validate channel indices
  if (_refChannel < 0 || _refChannel >= DLCR_CHANNEL_COUNT ||
      _surChannel < 0 || _surChannel >= DLCR_CHANNEL_COUNT)
  {
    throw std::invalid_argument(
        "[Dlcr] Channel indices must be in range 0-" +
        std::to_string(DLCR_CHANNEL_COUNT - 1) + ".");
  }
  if (_refChannel == _surChannel)
  {
    throw std::invalid_argument(
        "[Dlcr] Reference and surveillance channels must be different.");
  }

  // Validate gain ranges
  if (_gainLna < 0 || _gainLna > 14)
    throw std::invalid_argument("[Dlcr] LNA gain must be 0-14.");
  if (_gainMixer < 0 || _gainMixer > 15)
    throw std::invalid_argument("[Dlcr] Mixer gain must be 0-15.");
  if (_gainVga < 0 || _gainVga > 15)
    throw std::invalid_argument("[Dlcr] VGA gain must be 0-15.");

  serial        = _serial;
  refChannel    = _refChannel;
  surChannel    = _surChannel;
  gainLna       = _gainLna;
  gainMixer     = _gainMixer;
  gainVga       = _gainVga;
  externalClock = _externalClock;
  dev           = nullptr;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
void Dlcr::check_status(int status, const std::string &message)
{
  if (status != 0)
  {
    throw std::runtime_error("[Dlcr] " + message +
                             " (error code " + std::to_string(status) + ")");
  }
}

// ---------------------------------------------------------------------------
// start() – open and configure hardware
// ---------------------------------------------------------------------------
void Dlcr::start()
{
  int status;

  // Open device; passing an empty string opens the first device found
  status = dlcr_open(&dev, serial.empty() ? "" : serial.c_str());
  check_status(status, "Failed to open device.");

  // Log hardware/firmware versions
  dlcr_dev_info_t info;
  dlcr_get_dev_info(dev, &info);
  std::cout << "[Dlcr] Connected – HW v"
            << static_cast<int>(info.hw_ver_major) << "."
            << static_cast<int>(info.hw_ver_minor)
            << "  FW v"
            << static_cast<int>(info.fw_ver_major) << "."
            << static_cast<int>(info.fw_ver_minor) << "."
            << static_cast<int>(info.fw_ver_build) << std::endl;

  // Select clock reference
  dlcr_clock_t clkSrc = externalClock ? DLCR_CLOCK_EXTERNAL : DLCR_CLOCK_INTERNAL;
  status = dlcr_set_clock_source(dev, clkSrc);
  check_status(status, "Failed to set clock source.");
  std::cout << "[Dlcr] Clock source: "
            << (externalClock ? "external 10 MHz" : "internal TXCO")
            << std::endl;

  // Build channel bitmask for only the two channels we need
  dlcr_channel_t chanMask = static_cast<dlcr_channel_t>(
      (1 << refChannel) | (1 << surChannel));

  // Start from a clean state: disable all, then enable ours
  status = dlcr_disable_channel(dev, DLCR_CHAN_ALL);
  check_status(status, "Failed to disable all channels.");
  status = dlcr_enable_channel(dev, chanMask);
  check_status(status, "Failed to enable channels.");

  // Tune both channels to fc; calibrate=true is mandatory for coherent PCR
  status = dlcr_set_freq(dev, chanMask, static_cast<double>(fc), true);
  check_status(status, "Failed to set frequency.");

  // Apply gains uniformly to both channels
  status = dlcr_set_lna_gain(dev, chanMask, gainLna);
  check_status(status, "Failed to set LNA gain.");
  status = dlcr_set_mixer_gain(dev, chanMask, gainMixer);
  check_status(status, "Failed to set mixer gain.");
  status = dlcr_set_vga_gain(dev, chanMask, gainVga);
  check_status(status, "Failed to set VGA gain.");

  std::cout << "[Dlcr] Configured:"
            << " fc="       << fc        << " Hz"
            << " refCh="    << refChannel
            << " surCh="    << surChannel
            << " gainLna="  << gainLna
            << " gainMixer="<< gainMixer
            << " gainVga="  << gainVga   << std::endl;
}

// ---------------------------------------------------------------------------
// process() – start async streaming (non-blocking; callbacks run in libdlcr
//             background thread)
// ---------------------------------------------------------------------------
void Dlcr::process(IqData *buffer1, IqData *buffer2)
{
  cbCtx.buffer1     = buffer1;
  cbCtx.buffer2     = buffer2;
  cbCtx.refChannel  = refChannel;
  cbCtx.surChannel  = surChannel;
  cbCtx.saveIq      = saveIq;
  cbCtx.saveIqFile  = &saveIqFile;

  // 128 k samples per callback is a reasonable balance between latency and
  // CPU overhead. Adjust via config if needed.
  const size_t BUFFER_SAMPLES = 131072;

  int status = dlcr_start(dev, BUFFER_SAMPLES, rx_callback, &cbCtx);
  check_status(status, "Failed to start streaming.");
  std::cout << "[Dlcr] Streaming started." << std::endl;
}

// ---------------------------------------------------------------------------
// stop() – halt streaming and release the device
// ---------------------------------------------------------------------------
void Dlcr::stop()
{
  if (dev != nullptr)
  {
    dlcr_stop(dev);
    dlcr_close(dev);
    dev = nullptr;
    std::cout << "[Dlcr] Device closed." << std::endl;
  }
}

// ---------------------------------------------------------------------------
// rx_callback() – called by libdlcr each time a buffer of samples is ready
// ---------------------------------------------------------------------------
void Dlcr::rx_callback(dlcr_complex_t *samples[DLCR_CHANNEL_COUNT],
                        size_t count, size_t drops, void *user_ctx)
{
  CallbackContext *ctx = static_cast<CallbackContext *>(user_ctx);

  if (drops > 0)
  {
    std::cerr << "[Dlcr] Warning: " << drops << " samples dropped." << std::endl;
  }

  // --- Reference channel -> buffer1 (Blah2 reference signal) ---
  if (samples[ctx->refChannel] != nullptr)
  {
    ctx->buffer1->lock();
    for (size_t i = 0; i < count; ++i)
    {
      ctx->buffer1->push_back({
          static_cast<double>(samples[ctx->refChannel][i].re),
          static_cast<double>(samples[ctx->refChannel][i].im)
      });
    }
    ctx->buffer1->unlock();
  }

  // --- Surveillance channel -> buffer2 (Blah2 surveillance signal) ---
  if (samples[ctx->surChannel] != nullptr)
  {
    ctx->buffer2->lock();
    for (size_t i = 0; i < count; ++i)
    {
      ctx->buffer2->push_back({
          static_cast<double>(samples[ctx->surChannel][i].re),
          static_cast<double>(samples[ctx->surChannel][i].im)
      });
    }
    ctx->buffer2->unlock();
  }

  // --- Optional IQ file recording ---
  // Writes reference channel samples followed by surveillance channel samples.
  // Format: raw IEEE-754 float pairs (re, im) matching dlcr_complex_t layout.
  if (*ctx->saveIq && ctx->saveIqFile->is_open())
  {
    if (samples[ctx->refChannel] != nullptr)
    {
      ctx->saveIqFile->write(
          reinterpret_cast<const char *>(samples[ctx->refChannel]),
          static_cast<std::streamsize>(count * sizeof(dlcr_complex_t)));
    }
    if (samples[ctx->surChannel] != nullptr)
    {
      ctx->saveIqFile->write(
          reinterpret_cast<const char *>(samples[ctx->surChannel]),
          static_cast<std::streamsize>(count * sizeof(dlcr_complex_t)));
    }
  }
}

// ---------------------------------------------------------------------------
// replay() – not implemented for the CR-8
// ---------------------------------------------------------------------------
void Dlcr::replay(IqData * /*buffer1*/, IqData * /*buffer2*/,
                   std::string /*file*/, bool /*loop*/)
{
  std::cerr << "[Dlcr] Replay is not implemented for the Dragon Labs CR-8."
            << std::endl;
}
