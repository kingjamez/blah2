var timestamp = -1;
var nRows = 3;
var host = window.location.hostname;
var isLocalHost = is_localhost(host);
var range_x = [];
var range_y = [];

// setup API
var urlTimestamp;
var urlDetection;
var urlAdsb;
var urlConfig;
var urlSave;
if (isLocalHost) {
  urlTimestamp = '//' + host + ':3000/api/timestamp';
} else {
  urlTimestamp = '//' + host + '/api/timestamp';
}
if (isLocalHost) {
  urlDetection = '//' + host + ':3000/api/detection';
} else {
  urlDetection = '//' + host + '/api/detection';
}
if (isLocalHost) {
  urlMap = '//' + host + ':3000' + urlMap;
} else {
  urlMap = '//' + host + urlMap;
}
if (isLocalHost) {
  urlAdsb = '//' + host + ':3000/api/adsb';
} else {
  urlAdsb = '//' + host + '/api/adsb';
}
if (isLocalHost) {
  urlConfig = '//' + host + ':3000/api/config';
} else {
  urlConfig = '//' + host + '/api/config';
}
if (isLocalHost) {
  urlSave = '//' + host + ':3000/api/save';
} else {
  urlSave = '//' + host + '/api/save';
}

// get truth flag
var isTruth = false;
$.getJSON(urlConfig, function () { })
.done(function (data_config) {
  if (data_config.truth.adsb.enabled === true) {
    isTruth = true;
  }
});

// setup plotly
var layout = {
  autosize: true,
  margin: {
    l: 50,
    r: 50,
    b: 50,
    t: 10,
    pad: 0
  },
  hoverlabel: {
    namelength: 0
  },
  plot_bgcolor: "rgba(0,0,0,0)",
  paper_bgcolor: "rgba(0,0,0,0)",
  annotations: [],
  displayModeBar: false,
  xaxis: {
    title: {
      text: 'Bistatic Range (km)',
      font: {
        size: 24
      }
    },
    ticks: '',
    side: 'bottom'
  },
  yaxis: {
    title: {
      text: 'Bistatic Doppler (Hz)',
      font: {
        size: 24
      }
    },
    ticks: '',
    ticksuffix: ' ',
    autosize: false,
    categoryorder: "total descending"
  },
  showlegend: false
};
var config = {
  responsive: true,
  displayModeBar: false
  //scrollZoom: true
}

// setup plotly data
var data = [
  {
    z: [[0, 0, 0], [0, 0, 0], [0, 0, 0]],
    colorscale: 'Jet',
    type: 'heatmap'
  }
];
var detection = [];
var adsb = {};

Plotly.newPlot('data', data, layout, config);

// save status indicator
var saveIndicator = document.createElement('div');
saveIndicator.id = 'save-indicator';
saveIndicator.style.cssText = 'position:fixed;top:10px;right:10px;z-index:1000;' +
  'padding:8px 14px;border-radius:6px;font-family:monospace;font-size:13px;' +
  'display:none;pointer-events:none;line-height:1.5;min-width:180px;';
document.body.appendChild(saveIndicator);

function formatBytes(bytes) {
  if (bytes === 0) return '0 B';
  var units = ['B', 'KB', 'MB', 'GB', 'TB'];
  var i = Math.floor(Math.log(bytes) / Math.log(1024));
  return (bytes / Math.pow(1024, i)).toFixed(1) + ' ' + units[i];
}

function updateSaveIndicator() {
  $.getJSON(urlSave, function () { })
    .done(function (data) {
      if (!data.active) {
        saveIndicator.style.display = 'none';
        return;
      }
      saveIndicator.style.display = 'block';
      var diskPct = data.disk.percent || 0;
      // color based on disk usage: green < 70%, yellow 70-90%, red > 90%
      var bgColor, borderColor;
      if (diskPct >= 90) {
        bgColor = 'rgba(220,53,69,0.9)'; borderColor = '#dc3545';
      } else if (diskPct >= 70) {
        bgColor = 'rgba(255,193,7,0.9)'; borderColor = '#ffc107';
      } else {
        bgColor = 'rgba(25,135,84,0.9)'; borderColor = '#198754';
      }
      saveIndicator.style.background = bgColor;
      saveIndicator.style.border = '2px solid ' + borderColor;
      saveIndicator.style.color = '#fff';
      saveIndicator.style.textShadow = '0 1px 2px rgba(0,0,0,0.3)';

      var html = '<b>\u23FA REC</b>';
      if (data.file) {
        html += '<br>File: ' + formatBytes(data.fileSize);
      }
      html += '<br>Disk: ' + formatBytes(data.disk.free) + ' free (' + diskPct + '% used)';
      saveIndicator.innerHTML = html;
    })
    .fail(function () {
      saveIndicator.style.display = 'none';
    });
}
// poll save status every 2 seconds (no need for 100ms)
window.setInterval(updateSaveIndicator, 2000);
updateSaveIndicator();

// callback function
var intervalId = window.setInterval(function () {

  // check if timestamp is updated
  $.get(urlTimestamp, function () { })

    .done(function (data) {
      if (timestamp != data) {
        timestamp = data;

        // get detection data (no detection lag)
        $.getJSON(urlDetection, function () { })
          .done(function (data_detection) {
            detection = data_detection;
          });

        // get ADS-B data if enabled in config
        if (isTruth) {
          $.getJSON(urlAdsb, function () { })
            .done(function (data_adsb) {
              adsb['delay'] = [];
              adsb['doppler'] = [];
              adsb['flight'] = [];
              for (const aircraft in data_adsb) {
                if ('doppler' in data_adsb[aircraft]) {
                  adsb['delay'].push(data_adsb[aircraft]['delay'])
                  adsb['doppler'].push(data_adsb[aircraft]['doppler'])
                  adsb['flight'].push(data_adsb[aircraft]['flight'])
                }
              }
            });
        }

        // get new map data
        $.getJSON(urlMap, function () { })
          .done(function (data) {

            // case draw new plot
            if (data.nRows != nRows) {
              nRows = data.nRows;

              // lock range before other trace
              var layout_update = {
                'xaxis.range': [data.delay[0], data.delay.slice(-1)[0]],
                'yaxis.range': [data.doppler[0], data.doppler.slice(-1)[0]]
              };
              Plotly.relayout('data', layout_update);

              var trace1 = {
                  z: data.data,
                  x: data.delay,
                  y: data.doppler,
                  colorscale: 'Viridis',
                  zauto: false,
                  zmin: 0,
                  zmax: Math.max(13, data.maxPower),
                  type: 'heatmap'
              };
              var trace2 = {
                  x: detection.delay,
                  y: detection.doppler,
                  mode: 'markers',
                  type: 'scatter',
                  marker: {
                    size: 16,
                    opacity: 0.6
                  }
              };
              var trace3 = {
                x: adsb.delay,
                y: adsb.doppler,
                mode: 'markers',
                type: 'scatter',
                marker: {
                  size: 16,
                  opacity: 0.6
                }
            };
              
              var data_trace = [trace1, trace2, trace3];
              Plotly.newPlot('data', data_trace, layout, config);
            }
            // case update plot
            else {
              var trace_update = {
                x: [data.delay, detection.delay, adsb.delay],
                y: [data.doppler, detection.doppler, adsb.doppler],
                z: [data.data, [], []],
                zmax: [Math.max(13, data.maxPower), [], []],
                text: [[], [], adsb.flight]
              };
              Plotly.update('data', trace_update);
            }

          })
          .fail(function () {
          })
          .always(function () {
          });
      }
    })
    .fail(function () {
    })
    .always(function () {
    });
}, 100);
