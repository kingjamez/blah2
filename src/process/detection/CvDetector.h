/// @file CvDetector.h
/// @class CvDetector
/// @brief A class to implement ML-based detection using CenterNet via ONNX Runtime.
/// @details Replaces the three-stage CFAR pipeline (CfarDetector1D + Centroid +
/// Interpolate) with a single CenterNet neural network inference pass.
/// The model is a MobileNetV3-Small backbone with CenterNet detection heads,
/// trained on delay-Doppler maps with ADS-B ground truth labels (see PCRCV).
///
/// Input: Map object (same as CfarDetector1D receives)
/// Output: Detection object (same format — delay bins, Doppler Hz, SNR dB)
///
/// The model is loaded from an ONNX file. When model_path is "auto", the
/// detector reads capture.type and capture.fs from config to select the
/// matching model from the models/ directory.
///
/// Build requires ONNX Runtime C++ library. This is optional — when
/// USE_CVDETECTOR is not defined, this class is not compiled and CFAR
/// remains the only detection method.
///
/// @author kingjamez (PCRCV project)

#ifndef CVDETECTOR_H
#define CVDETECTOR_H

#include "data/Map.h"
#include "data/Detection.h"

#include <string>
#include <vector>
#include <complex>
#include <memory>

#ifdef USE_CVDETECTOR
#include <onnxruntime_cxx_api.h>
#endif

class CvDetector
{
private:
  /// @brief Path to the ONNX model file.
  std::string modelPath;

  /// @brief Detection confidence threshold [0, 1].
  double confidenceThreshold;

  /// @brief Model input height (pixels).
  int inputHeight;

  /// @brief Model input width (pixels).
  int inputWidth;

  /// @brief NMS kernel size for local maximum filtering.
  int nmsKernel;

  /// @brief Maximum detections per frame.
  int maxDetections;

  /// @brief Minimum delay to report detections (bins).
  int8_t minDelay;

  /// @brief Minimum absolute Doppler to report detections (Hz).
  double minDoppler;

  /// @brief Whether the model is loaded and ready.
  bool isReady;

#ifdef USE_CVDETECTOR
  /// @brief ONNX Runtime environment.
  std::unique_ptr<Ort::Env> env;

  /// @brief ONNX Runtime session options.
  std::unique_ptr<Ort::SessionOptions> sessionOptions;

  /// @brief ONNX Runtime inference session.
  std::unique_ptr<Ort::Session> session;

  /// @brief ONNX Runtime memory info.
  Ort::MemoryInfo memoryInfo;
#endif

  /// @brief Normalize the map data to [0, 1].
  /// @param mapDb 2D map in dB.
  /// @param noisePower Noise floor in dB.
  /// @param maxPower Dynamic range in dB.
  /// @return Flattened normalized float vector (H, W) in row-major order.
  std::vector<float> normalize(
    const std::vector<std::vector<double>>& mapDb,
    double noisePower, double maxPower);

  /// @brief Resize a normalized map to model input dimensions.
  /// @param input Normalized map (nRows x nCols).
  /// @param nRows Original number of rows (Doppler).
  /// @param nCols Original number of columns (delay).
  /// @param scaleY Output: vertical scale factor.
  /// @param scaleX Output: horizontal scale factor.
  /// @param padTop Output: top padding in pixels.
  /// @param padLeft Output: left padding in pixels.
  /// @return Flattened resized map (inputHeight x inputWidth).
  std::vector<float> resize(
    const std::vector<float>& input,
    int nRows, int nCols,
    float& scaleY, float& scaleX,
    int& padTop, int& padLeft);

  /// @brief Extract detections from CenterNet output tensors.
  /// @param heatmap Output heatmap (1, 1, H', W').
  /// @param offsets Output offsets (1, 2, H', W').
  /// @param sizes Output sizes (1, 2, H', W').
  /// @param outH Output height.
  /// @param outW Output width.
  /// @param scaleY Scale from resize.
  /// @param scaleX Scale from resize.
  /// @param padTop Padding from resize.
  /// @param padLeft Padding from resize.
  /// @param origRows Original map rows.
  /// @param origCols Original map cols.
  /// @param delayAxis Delay bin values.
  /// @param dopplerAxis Doppler Hz values.
  /// @param mapDb Map in dB (for SNR extraction).
  /// @param noisePower Noise floor in dB.
  /// @return Detection object with delay (bins), Doppler (Hz), SNR (dB).
  std::unique_ptr<Detection> extractDetections(
    const float* heatmap, const float* offsets, const float* sizes,
    int outH, int outW,
    float scaleY, float scaleX, int padTop, int padLeft,
    int origRows, int origCols,
    const std::deque<int>& delayAxis,
    const std::deque<double>& dopplerAxis,
    const std::vector<std::vector<double>>& mapDb,
    double noisePower);

public:
  /// @brief Constructor.
  /// @param modelPath Path to ONNX model file.
  /// @param confidenceThreshold Detection confidence threshold [0, 1].
  /// @param inputHeight Model input height (default 256).
  /// @param inputWidth Model input width (default 256).
  /// @param minDelay Minimum delay to report (bins).
  /// @param minDoppler Minimum absolute Doppler to report (Hz).
  CvDetector(
    const std::string& modelPath,
    double confidenceThreshold = 0.5,
    int inputHeight = 256,
    int inputWidth = 256,
    int8_t minDelay = 5,
    double minDoppler = 15.0);

  /// @brief Destructor.
  ~CvDetector();

  /// @brief Run ML detection on a delay-Doppler map.
  /// @details Normalizes the map, resizes to model input, runs ONNX inference,
  /// extracts peaks from the heatmap, maps back to native coordinates,
  /// and returns a Detection object identical in format to the CFAR pipeline.
  /// @param x Ambiguity map data.
  /// @return Detections from the CenterNet model.
  std::unique_ptr<Detection> process(Map<std::complex<double>> *x);

  /// @brief Check if the model is loaded and ready for inference.
  /// @return True if ready.
  bool ready() const;
};

#endif
