/* Sensor sharpening and document colour correction. */
#include "kodak_i2000_color.h"
#include <stdlib.h>
#include <string.h>

/* Estimate the paper colour per sensor column for document background cleanup. */
#define KODAK_I2000_COLOR_PROFILE_MIN 160U
#define KODAK_I2000_COLOR_PROFILE_SPREAD 80U
#define KODAK_I2000_COLOR_PROFILE_BINS (256U - KODAK_I2000_COLOR_PROFILE_MIN)
#define KODAK_I2000_COLOR_PROFILE_QUANTILE_NUMERATOR 9U
#define KODAK_I2000_COLOR_PROFILE_QUANTILE_DENOMINATOR 10U
#define KODAK_I2000_COLOR_PROFILE_MAX_SAMPLES 4096U
#define KODAK_I2000_COLOR_PROFILE_MIN_FRACTION 16U
#define KODAK_I2000_COLOR_WHITE_POINT_FLOOR 204U
#define KODAK_I2000_COLOR_BACKGROUND_MIN 224U
#define KODAK_I2000_COLOR_BACKGROUND_SPREAD 24U

/* Kodak's default RGB image chain compensates for softness in the feed
 * direction at sensor resolution, before the page is cropped and resized.
 * All three channels share the same five-tap mask. */
uint8_t *
sharpen_sensor_rgb(const uint8_t *source, size_t width, size_t height,
                   unsigned sensor_dpi)
{
  const size_t stride = width * 3U;
  const int center = sensor_dpi == 600U ? 1400 : 578;
  const int near = sensor_dpi == 600U ? -118 : -28;
  const int far = sensor_dpi == 600U ? -70 : -5;
  const int divisor = sensor_dpi == 600U ? 1024 : 512;
  uint8_t *output = malloc(stride * height);

  if (output == NULL)
    return NULL;
  memcpy(output, source, 2U * stride);
  for (size_t y = 2U; y + 2U < height; ++y) {
    const uint8_t *row = source + y * stride;
    const uint8_t *above = row - stride;
    const uint8_t *below = row + stride;
    const uint8_t *first = above - stride;
    const uint8_t *last = below + stride;
    uint8_t *dest = output + y * stride;
    for (size_t i = 0U; i < stride; ++i) {
      int sum =
          center * row[i] + near * (above[i] + below[i]) + far * (first[i] + last[i]);
      int value = sum / divisor;
      dest[i] = value < 0 ? 0 : value > 255 ? 255 : (uint8_t)value;
    }
  }
  memcpy(output + (height - 2U) * stride, source + (height - 2U) * stride, 2U * stride);
  return output;
}

/* Default document processing measured at Kodak's ColorPreferenceLUTsApply
 * on an i2400. Each entry is the first 12-bit index for output codes 1..255.
 * The three channel curves were identical within each side. */
static const uint16_t document_tone_thresholds[2][255] = {
    {
        689,  690,  692,  693,  694,  696,  698,  700,  702,  704,  707,  710,  712,
        715,  718,  722,  725,  729,  733,  736,  739,  741,  743,  745,  747,  750,
        752,  757,  762,  767,  773,  778,  784,  790,  796,  801,  804,  807,  810,
        814,  817,  824,  831,  838,  846,  853,  861,  866,  870,  874,  878,  884,
        892,  901,  909,  915,  919,  924,  928,  937,  947,  956,  965,  975,  981,
        985,  990,  998,  1008, 1018, 1028, 1039, 1045, 1050, 1056, 1066, 1077, 1088,
        1099, 1108, 1113, 1119, 1129, 1141, 1153, 1159, 1165, 1174, 1186, 1199, 1211,
        1220, 1227, 1233, 1246, 1259, 1272, 1283, 1290, 1297, 1310, 1324, 1333, 1340,
        1350, 1365, 1379, 1393, 1400, 1407, 1421, 1436, 1450, 1461, 1468, 1480, 1495,
        1510, 1523, 1531, 1542, 1557, 1571, 1579, 1590, 1606, 1623, 1636, 1644, 1656,
        1672, 1689, 1701, 1710, 1725, 1742, 1752, 1761, 1779, 1796, 1811, 1820, 1834,
        1852, 1870, 1880, 1890, 1909, 1927, 1941, 1951, 1968, 1986, 1995, 2009, 2029,
        2048, 2058, 2071, 2090, 2110, 2121, 2134, 2154, 2174, 2185, 2199, 2219, 2232,
        2244, 2265, 2286, 2298, 2311, 2332, 2353, 2364, 2380, 2401, 2412, 2429, 2451,
        2469, 2480, 2501, 2523, 2537, 2551, 2574, 2594, 2606, 2626, 2645, 2656, 2679,
        2702, 2715, 2732, 2755, 2774, 2786, 2810, 2825, 2842, 2866, 2885, 2898, 2922,
        2946, 2957, 2979, 3003, 3018, 3037, 3059, 3072, 3096, 3121, 3134, 3156, 3181,
        3195, 3216, 3242, 3258, 3277, 3300, 3313, 3340, 3363, 3376, 3402, 3427, 3440,
        3466, 3483, 3504, 3531, 3547, 3569, 3595, 3612, 3634, 3662, 3677, 3701, 3721,
        3741, 3769, 3787, 3808, 3837, 3853, 3878, 3897,
    },
    {
        609,  610,  612,  613,  614,  616,  618,  620,  622,  624,  626,  627,  628,
        630,  631,  633,  635,  637,  639,  640,  643,  645,  647,  649,  651,  654,
        656,  661,  666,  671,  675,  677,  680,  683,  686,  689,  692,  695,  698,
        702,  705,  712,  719,  723,  727,  731,  735,  738,  742,  746,  750,  756,
        764,  771,  775,  779,  783,  788,  792,  797,  803,  812,  819,  824,  829,
        833,  838,  843,  848,  853,  858,  864,  873,  882,  888,  893,  899,  904,
        910,  919,  930,  942,  969,  997,  1009, 1021, 1050, 1078, 1106, 1119, 1147,
        1172, 1179, 1185, 1191, 1198, 1208, 1219, 1226, 1233, 1239, 1246, 1253, 1260,
        1270, 1283, 1290, 1297, 1304, 1311, 1325, 1334, 1341, 1349, 1356, 1368, 1380,
        1387, 1395, 1403, 1414, 1427, 1435, 1443, 1451, 1459, 1468, 1479, 1492, 1500,
        1508, 1517, 1530, 1542, 1551, 1559, 1568, 1585, 1594, 1602, 1611, 1624, 1637,
        1646, 1655, 1664, 1673, 1685, 1700, 1709, 1719, 1728, 1746, 1755, 1765, 1775,
        1792, 1802, 1812, 1821, 1838, 1849, 1859, 1869, 1879, 1890, 1908, 1918, 1928,
        1940, 1957, 1967, 1978, 1991, 2006, 2017, 2028, 2044, 2057, 2068, 2079, 2090,
        2105, 2120, 2131, 2142, 2161, 2172, 2183, 2196, 2214, 2225, 2237, 2256, 2268,
        2279, 2293, 2310, 2322, 2334, 2345, 2362, 2377, 2389, 2402, 2421, 2433, 2445,
        2466, 2477, 2490, 2507, 2522, 2535, 2547, 2560, 2580, 2593, 2606, 2626, 2639,
        2651, 2672, 2685, 2698, 2717, 2732, 2745, 2758, 2774, 2792, 2805, 2821, 2840,
        2853, 2869, 2888, 2902, 2918, 2937, 2950, 2964, 2978, 2999, 3013, 3029, 3049,
        3063, 3081, 3099, 3112, 3133, 3149, 3163, 3177,
    },
};

int
apply_color_document_correction(uint8_t *image, size_t width, size_t height,
                                unsigned rear)
{
  const size_t bins = KODAK_I2000_COLOR_PROFILE_BINS;
  size_t histogram_entries;
  uint16_t *histogram;
  uint16_t *sample_counts;
  uint8_t *white_points;
  size_t sample_step;
  size_t sample_rows = 0U;
  size_t minimum_samples;
  size_t valid_columns = 0U;
  /* Kodak's camera RGB to sRGB matrix, in Q12. */
  static const int matrix[3][3] = {
      {8454, -3261, -1082}, {-871, 5135, -153}, {-62, -578, 4751}};
  uint8_t tone[4096];
  unsigned code = 0U;
  size_t y;
  size_t x;

  if (image == NULL || width == 0U || height == 0U || width > SIZE_MAX / (3U * bins) ||
      width * 3U * bins > SIZE_MAX / sizeof(*histogram))
    return 0;
  histogram_entries = width * 3U * bins;
  histogram = (uint16_t *)calloc(histogram_entries, sizeof(*histogram));
  sample_counts = (uint16_t *)calloc(width, sizeof(*sample_counts));
  white_points = (uint8_t *)malloc(width * 3U);
  if (histogram == NULL || sample_counts == NULL || white_points == NULL) {
    free(white_points);
    free(sample_counts);
    free(histogram);
    return 0;
  }
  memset(white_points, 255, width * 3U);

  /* Bound each 16-bit histogram to at most 4096 sampled rows.  The storage is
   * laid out as [channel][code][column], so the nearly-white document codes
   * touched by a row remain cache-local while retaining a true percentile. */
  sample_step = height / KODAK_I2000_COLOR_PROFILE_MAX_SAMPLES + 1U;
  for (y = 0U; y < height; y += sample_step) {
    ++sample_rows;
    for (x = 0U; x < width; ++x) {
      const uint8_t *pixel = image + (y * width + x) * 3U;
      unsigned minimum = pixel[0];
      unsigned maximum = pixel[0];
      size_t channel;

      for (channel = 1U; channel < 3U; ++channel) {
        if (pixel[channel] < minimum)
          minimum = pixel[channel];
        if (pixel[channel] > maximum)
          maximum = pixel[channel];
      }
      if (minimum < KODAK_I2000_COLOR_PROFILE_MIN ||
          maximum - minimum > KODAK_I2000_COLOR_PROFILE_SPREAD)
        continue;
      ++sample_counts[x];
      for (channel = 0U; channel < 3U; ++channel) {
        size_t bin = (size_t)pixel[channel] - KODAK_I2000_COLOR_PROFILE_MIN;
        ++histogram[(channel * bins + bin) * width + x];
      }
    }
  }

  minimum_samples = sample_rows / KODAK_I2000_COLOR_PROFILE_MIN_FRACTION;
  if (minimum_samples < 8U)
    minimum_samples = 8U;
  for (x = 0U; x < width; ++x) {
    size_t channel;

    if ((size_t)sample_counts[x] < minimum_samples)
      continue;
    ++valid_columns;
    for (channel = 0U; channel < 3U; ++channel) {
      unsigned rank =
          ((unsigned)sample_counts[x] * KODAK_I2000_COLOR_PROFILE_QUANTILE_NUMERATOR +
           KODAK_I2000_COLOR_PROFILE_QUANTILE_DENOMINATOR - 1U) /
          KODAK_I2000_COLOR_PROFILE_QUANTILE_DENOMINATOR;
      unsigned count = 0U;
      size_t bin;

      for (bin = 0U; bin < bins; ++bin) {
        count += histogram[(channel * bins + bin) * width + x];
        if (count >= rank)
          break;
      }
      white_points[x * 3U + channel] =
          (uint8_t)(KODAK_I2000_COLOR_PROFILE_MIN + (bin < bins ? bin : bins - 1U));
      if (white_points[x * 3U + channel] < KODAK_I2000_COLOR_WHITE_POINT_FLOOR)
        white_points[x * 3U + channel] = KODAK_I2000_COLOR_WHITE_POINT_FLOOR;
    }
  }
  free(sample_counts);
  free(histogram);

  /* A scan dominated by a photograph or dark stock has no defensible paper
   * profile.  Requiring evidence in half the columns prevents page-derived
   * white balance from inventing one in that case. */
  if (valid_columns < (width + 1U) / 2U) {
    free(white_points);
    return 0;
  }

  for (size_t i = 0U; i < sizeof(tone); ++i) {
    while (code < 255U && document_tone_thresholds[rear][code] <= i)
      ++code;
    tone[i] = (uint8_t)code;
  }
  for (y = 0U; y < height; ++y) {
    for (x = 0U; x < width; ++x) {
      uint8_t *pixel = image + (y * width + x) * 3U;
      unsigned minimum = 255U;
      unsigned maximum = 0U;
      int r = pixel[0], g = pixel[1], b = pixel[2];

      /* Compare against the local paper only for background removal. The
       * colour transform takes the scanner's original calibrated RGB. */
      for (size_t c = 0U; c < 3U; ++c) {
        unsigned white = white_points[x * 3U + c];
        unsigned value = ((unsigned)pixel[c] * 255U + white / 2U) / white;
        if (value > 255U)
          value = 255U;
        if (value < minimum)
          minimum = value;
        if (value > maximum)
          maximum = value;
      }
      if (minimum >= KODAK_I2000_COLOR_BACKGROUND_MIN &&
          maximum - minimum <= KODAK_I2000_COLOR_BACKGROUND_SPREAD) {
        memset(pixel, 255, 3U);
        continue;
      }
      for (size_t c = 0U; c < 3U; ++c) {
        int index =
            (matrix[c][0] * r + matrix[c][1] * g + matrix[c][2] * b + 128) / 256;
        pixel[c] = tone[index < 0 ? 0 : index > 4095 ? 4095 : index];
      }
    }
  }
  free(white_points);
  return 1;
}
