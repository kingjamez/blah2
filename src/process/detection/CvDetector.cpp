/// @file CvDetector.cpp
/// @brief Implementation of CenterNet ML detector for passive radar.
/// @details Drop-in replacement for CfarDetector1D + Centroid + Interpolate.
/// Uses ONNX Runtime for inference with a CenterNet model trained by PCRCV.
///
/// When USE_CVDETECTOR is not defined, all methods return empty detections
/// and log a warning. This allows the code to compile without ONNX Runtime.
///
/// @author kingjamez (PCRCV project)

#include "CvDetector.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <numeric>

// ============================================================================
// Constructor / Destructor
// ============================================================================

CvDetector::CvDetector(
  const std::string& modelPath,
  double confidenceThreshold,
  int inputHeight,
  int inputWidth,
  int8_t minDelay,
  double minDoppler)
  : modelPath(modelPath),
    confidenceThreshold(confidenceThreshold),
    inputHeight(inputHeight),
    inputWidth(inputWidth),
    nmsKernel(3),
    maxDetections(100),
    minDelay(minDelay),
    minDoppler(minDoppler),
    isReady(false)
#ifdef USE_CVDETECTOR
    , memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
#endif
{
#ifdef USE_CVDETECTOR
  try
  {
    env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "CvDetector");
    sessionOptions = std::make_unique<Ort::SessionOptions>();
    sessionOptions->SetIntraOpNumThreads(1);
    sessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    session = std::make_unique<Ort::Session>(*env, modelPath.c_str(), *sessionOptions);

    // Verify model has expected inputs/outputs
    size_t numInputs = session->GetInputCount();
    size_t numOutputs = session->GetOutputCount();

    if (numInputs < 1 || numOutputs < 3)
    {
      std::cerr << "CvDetector: Model has " << numInputs << " inputs and "
                << numOutputs << " outputs. Expected >=1 input and >=3 outputs." << std::endl;
      return;
    }

    isReady = true;
    std::cout << "CvDetector: Loaded model from " << modelPath << std::endl;
    std::cout << "CvDetector: " << numInputs << " inputs, " << numOutputs
              << " outputs, confidence threshold " << confidenceThreshold << std::endl;
  }
  catch (const Ort::Exception& e)
  {
    std::cerr << "CvDetector: Failed to load model: " << e.what() << std::endl;
    isReady = false;
  }
#else
  std::cerr << "CvDetector: Built without ONNX Runtime (USE_CVDETECTOR not defined). "
            << "ML detection unavailable, returning empty detections." << std::endl;
  isReady = false;
#endif
}

CvDetector::~CvDetector() = default;

bool CvDetector::ready() const
{
  return isReady;
}

// ============================================================================
// Main Processing
// ============================================================================

std::unique_ptr<Detection> CvDetector::process(Map<std::complex<double>> *x)
{
  // Get the dB map (same conversion the display uses)
  Map<double> *mapDb = x->get_map_db();
  uint32_t nRows = mapDb->get_nRows();   // Doppler bins
  uint32_t nCols = mapDb->get_nCols();   // Delay bins

  if (!isReady || nRows == 0 || nCols == 0)
  {
    // Return empty detection
    std::vector<double> emptyD, emptyDp, emptySn;
    delete mapDb;
    return std::make_unique<Detection>(emptyD, emptyDp, emptySn);
  }

#ifdef USE_CVDETECTOR
  // Step 1: Normalize the map to [0, 1]
  std::vector<float> normalized = normalize(mapDb->data, mapDb->noisePower, mapDb->maxPower);

  // Step 2: Resize/pad to model input dimensions
  float scaleY, scaleX;
  int padTop, padLeft;
  std::vector<float> resized = resize(normalized, nRows, nCols,
                                       scaleY, scaleX, padTop, padLeft);

  // Step 3: Run ONNX Runtime inference
  // Input tensor: (1, 1, inputHeight, inputWidth)
  std::array<int64_t, 4> inputShape = {1, 1, inputHeight, inputWidth};
  Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
    memoryInfo, resized.data(), resized.size(),
    inputShape.data(), inputShape.size());

  // Run inference
  const char* inputNames[] = {"input"};
  const char* outputNames[] = {"heatmap", "offsets", "sizes"};

  auto outputTensors = session->Run(
    Ort::RunOptions{nullptr},
    inputNames, &inputTensor, 1,
    outputNames, 3);

  // Extract output tensors
  // Heatmap: (1, 1, H/4, W/4)
  auto& heatmapTensor = outputTensors[0];
  auto heatmapShape = heatmapTensor.GetTensorTypeAndShapeInfo().GetShape();
  int outH = static_cast<int>(heatmapShape[2]);
  int outW = static_cast<int>(heatmapShape[3]);

  const float* heatmapData = heatmapTensor.GetTensorData<float>();
  const float* offsetsData = outputTensors[1].GetTensorData<float>();
  const float* sizesData = outputTensors[2].GetTensorData<float>();

  // Step 4: Extract detections from output
  auto detection = extractDetections(
    heatmapData, offsetsData, sizesData,
    outH, outW,
    scaleY, scaleX, padTop, padLeft,
    nRows, nCols,
    x->delay, x->doppler,
    mapDb->data, mapDb->noisePower);

  delete mapDb;
  return detection;

#else
  // No ONNX Runtime — return empty
  std::vector<double> emptyD, emptyDp, emptySn;
  delete mapDb;
  return std::make_unique<Detection>(emptyD, emptyDp, emptySn);
#endif
}

// ============================================================================
// Normalize
// ============================================================================

std::vector<float> CvDetector::normalize(
  const std::vector<std::vector<double>>& mapDb,
  double noisePower, double maxPower)
{
  int nRows = mapDb.size();
  int nCols = nRows > 0 ? mapDb[0].size() : 0;
  std::vector<float> output(nRows * nCols);

  double denom = maxPower - noisePower;
  if (std::abs(denom) < 1e-6) denom = 1.0;

  for (int r = 0; r < nRows; r++)
  {
    for (int c = 0; c < nCols; c++)
    {
      float val = static_cast<float>((mapDb[r][c] - noisePower) / denom);
      output[r * nCols + c] = std::clamp(val, 0.0f, 1.0f);
    }
  }

  return output;
}

// ============================================================================
// Resize (bilinear interpolation + zero-padding to preserve aspect ratio)
// ============================================================================

std::vector<float> CvDetector::resize(
  const std::vector<float>& input,
  int nRows, int nCols,
  float& scaleY, float& scaleX,
  int& padTop, int& padLeft)
{
  // Compute scale to fit within target while preserving aspect ratio
  float scaleH = static_cast<float>(inputHeight) / nRows;
  float scaleW = static_cast<float>(inputWidth) / nCols;
  float scale = std::min(scaleH, scaleW);

  int newH = static_cast<int>(std::round(nRows * scale));
  int newW = static_cast<int>(std::round(nCols * scale));

  // Padding to center the resized image
  padTop = (inputHeight - newH) / 2;
  padLeft = (inputWidth - newW) / 2;
  scaleY = scale;
  scaleX = scale;

  // Output: zero-initialized (inputHeight x inputWidth)
  std::vector<float> output(inputHeight * inputWidth, 0.0f);

  // Bilinear interpolation
  for (int y = 0; y < newH; y++)
  {
    float srcY = y / scale;
    int y0 = static_cast<int>(srcY);
    int y1 = std::min(y0 + 1, nRows - 1);
    float fy = srcY - y0;

    for (int x = 0; x < newW; x++)
    {
      float srcX = x / scale;
      int x0 = static_cast<int>(srcX);
      int x1 = std::min(x0 + 1, nCols - 1);
      float fx = srcX - x0;

      // Bilinear interpolation
      float val = (1 - fy) * (1 - fx) * input[y0 * nCols + x0]
                + (1 - fy) * fx       * input[y0 * nCols + x1]
                + fy       * (1 - fx) * input[y1 * nCols + x0]
                + fy       * fx       * input[y1 * nCols + x1];

      int outIdx = (y + padTop) * inputWidth + (x + padLeft);
      output[outIdx] = val;
    }
  }

  return output;
}

// ============================================================================
// Extract Detections from CenterNet Output
// ============================================================================

std::unique_ptr<Detection> CvDetector::extractDetections(
  const float* heatmap, const float* offsets, const float* sizes,
  int outH, int outW,
  float scaleY, float scaleX, int padTop, int padLeft,
  int origRows, int origCols,
  const std::deque<int>& delayAxis,
  const std::deque<double>& dopplerAxis,
  const std::vector<std::vector<double>>& mapDb,
  double noisePower)
{
  std::vector<double> detDelay;
  std::vector<double> detDoppler;
  std::vector<double> detSnr;

  // Local maximum filtering (pseudo-NMS)
  // A pixel is a peak if it equals the max in its nmsKernel neighborhood
  int pad = nmsKernel / 2;

  struct Peak {
    int y, x;
    float score;
  };
  std::vector<Peak> peaks;

  for (int y = 0; y < outH; y++)
  {
    for (int x = 0; x < outW; x++)
    {
      float val = heatmap[y * outW + x];
      if (val < confidenceThreshold) continue;

      // Check if local maximum
      bool isMax = true;
      for (int dy = -pad; dy <= pad && isMax; dy++)
      {
        for (int dx = -pad; dx <= pad && isMax; dx++)
        {
          int ny = y + dy;
          int nx = x + dx;
          if (ny < 0 || ny >= outH || nx < 0 || nx >= outW) continue;
          if (heatmap[ny * outW + nx] > val)
          {
            isMax = false;
          }
        }
      }

      if (isMax)
      {
        peaks.push_back({y, x, val});
      }
    }
  }

  // Sort by confidence and limit
  std::sort(peaks.begin(), peaks.end(),
    [](const Peak& a, const Peak& b) { return a.score > b.score; });

  if (static_cast<int>(peaks.size()) > maxDetections)
  {
    peaks.resize(maxDetections);
  }

  // Map each peak back to native map coordinates
  for (const auto& peak : peaks)
  {
    // Feature map is at 1/4 resolution of model input
    float inputY = peak.y * 4.0f;
    float inputX = peak.x * 4.0f;

    // Apply sub-pixel offset from offset head
    int offIdx = peak.y * outW + peak.x;
    inputY += offsets[0 * outH * outW + offIdx];  // offset_y
    inputX += offsets[1 * outH * outW + offIdx];  // offset_x

    // Remove padding and scale back to native map coordinates
    float nativeRow = (inputY - padTop) / scaleY;   // Doppler bin
    float nativeCol = (inputX - padLeft) / scaleX;   // Delay bin

    // Clamp to map bounds
    int rowIdx = static_cast<int>(std::round(nativeRow));
    int colIdx = static_cast<int>(std::round(nativeCol));

    if (rowIdx < 0 || rowIdx >= origRows) continue;
    if (colIdx < 0 || colIdx >= origCols) continue;

    // Get delay in bins and Doppler in Hz from the map axes
    double delayBin = 0;
    double dopplerHz = 0;

    if (colIdx < static_cast<int>(delayAxis.size()))
    {
      delayBin = static_cast<double>(delayAxis[colIdx]);
    }
    else
    {
      delayBin = static_cast<double>(colIdx);
    }

    if (rowIdx < static_cast<int>(dopplerAxis.size()))
    {
      dopplerHz = dopplerAxis[rowIdx];
    }

    // Apply minDelay and minDoppler filters (same as CFAR)
    if (std::abs(delayBin) < minDelay) continue;
    if (std::abs(dopplerHz) < minDoppler) continue;

    // Extract SNR from the actual map at the detection location
    double snrDb = mapDb[rowIdx][colIdx] - noisePower;

    detDelay.push_back(delayBin);
    detDoppler.push_back(dopplerHz);
    detSnr.push_back(snrDb);
  }

  return std::make_unique<Detection>(detDelay, detDoppler, detSnr);
}
