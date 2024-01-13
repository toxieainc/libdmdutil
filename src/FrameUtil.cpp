/*
 * Portions of this code was derived from DMDExt
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Common/FrameUtil.cs
 */

#include "FrameUtil.h"

#include <sstream>
#include <iomanip>
#include <cstring>

namespace DMDUtil {

inline int FrameUtil::MapAdafruitIndex(int x, int y, int width, int height, int numLogicalRows)
{
   int logicalRowLengthPerMatrix = 32 * 32 / 2 / numLogicalRows;
   int logicalRow = y % numLogicalRows;
   int dotPairsPerLogicalRow = width * height / numLogicalRows / 2;
   int widthInMatrices = width / 32;
   int matrixX = x / 32;
   int matrixY = y / 32;
   int totalMatrices = width * height / 1024;
   int matrixNumber = totalMatrices - ((matrixY + 1) * widthInMatrices) + matrixX;
   int indexWithinMatrixRow = x % logicalRowLengthPerMatrix;
   int index = logicalRow * dotPairsPerLogicalRow
     + matrixNumber * logicalRowLengthPerMatrix + indexWithinMatrixRow;
   return index;
}

void FrameUtil::SplitIntoRgbPlanes(const uint16_t* rgb565, int rgb565Size, int width, int numLogicalRows, uint8_t* dest, ColorMatrix colorMatrix)
{
   constexpr int pairOffset = 16;
   int height = rgb565Size / width;
   int subframeSize = rgb565Size / 2;

   for (int x = 0; x < width; ++x) {
      for (int y = 0; y < height; ++y) {
         if (y % (pairOffset * 2) >= pairOffset)
            continue;
           
         int inputIndex0 = y * width + x;
         int inputIndex1 = (y + pairOffset) * width + x;

         uint16_t color0 = rgb565[inputIndex0];
         uint16_t color1 = rgb565[inputIndex1];

         int r0 = 0, r1 = 0, g0 = 0, g1 = 0, b0 = 0, b1 = 0;
         switch (colorMatrix) {
            case ColorMatrix::Rgb:
               r0 = (color0 >> 13) & 0x7;
               g0 = (color0 >> 8) & 0x7;
               b0 = (color0 >> 2) & 0x7;
               r1 = (color1 >> 13) & 0x7;
               g1 = (color1 >> 8) & 0x7;
               b1 = (color1 >> 2) & 0x7;
               break;

            case ColorMatrix::Rbg:
               r0 = (color0 >> 13) & 0x7;
               b0 = (color0 >> 8) & 0x7;
               g0 = (color0 >> 2) & 0x7;
               r1 = (color1 >> 13) & 0x7;
               b1 = (color1 >> 8) & 0x7;
               g1 = (color1 >> 2) & 0x7;
               break;
         }

         for (int subframe = 0; subframe < 3; ++subframe) {
            uint8_t dotPair =
               (r0 & 1) << 5 |
               (g0 & 1) << 4 |
               (b0 & 1) << 3 |
               (r1 & 1) << 2 |
               (g1 & 1) << 1 |
               (b1 & 1);
            int indexWithinSubframe = MapAdafruitIndex(x, y, width, height, numLogicalRows);
            int indexWithinOutput = subframe * subframeSize + indexWithinSubframe;
            dest[indexWithinOutput] = dotPair;
            r0 >>= 1;
            g0 >>= 1;
            b0 >>= 1;
            r1 >>= 1;
            g1 >>= 1;
            b1 >>= 1;
         }
      }
   }
}

inline uint16_t FrameUtil::InterpolateRgb565Color(uint16_t color1, uint16_t color2, float ratio)
{
   int red1 = (color1 >> 11) & 0x1F;
   int green1 = (color1 >> 5) & 0x3F;
   int blue1 = color1 & 0x1F;

   int red2 = (color2 >> 11) & 0x1F;
   int green2 = (color2 >> 5) & 0x3F;
   int blue2 = color2 & 0x1F;

   int red = red1 + static_cast<int>((float)(red2 - red1) * ratio);
   int green = green1 + static_cast<int>((float)(green2 - green1) * ratio);
   int blue = blue1 + static_cast<int>((float)(blue2 - blue1) * ratio);

   red = std::min(std::max(red, 0), 0x1F);
   green = std::min(std::max(green, 0), 0x3F);
   blue = std::min(std::max(blue, 0), 0x1F);

   return (uint16_t)((red << 11) | (green << 5) | blue);
}

inline uint16_t FrameUtil::InterpolatedRgb565Pixel(const uint16_t* src, float srcX, float srcY, int srcWidth, int srcHeight)
{
   int x = (int)srcX;
   int y = (int)srcY;
   float xDiff = srcX - (float)x;
   float yDiff = srcY - (float)y;

   uint16_t a = src[y * srcWidth + x];
   uint16_t b = x < srcWidth - 1 ? src[y * srcWidth + (x + 1)] : a;
   uint16_t c = y < srcHeight - 1 ? src[(y + 1) * srcWidth + x] : a;
   uint16_t d = (x < srcWidth - 1 && y < srcHeight - 1) ? src[(y + 1) * srcWidth + (x + 1)] : a;

   uint16_t ab = InterpolateRgb565Color(a, b, xDiff);
   uint16_t cd = InterpolateRgb565Color(c, d, xDiff);

   return InterpolateRgb565Color(ab, cd, yDiff);
}

void FrameUtil::ResizeRgb565Bilinear(const uint16_t* src, int srcWidth, int srcHeight, uint16_t* dest, int destWidth, int destHeight)
{
   memset(dest, 0, destWidth * destHeight * sizeof(uint16_t));

   float srcAspect = (float)srcWidth / (float)srcHeight;
   float destAspect = (float)destWidth / (float)destHeight;
   int scaledWidth, scaledHeight;

   if (srcAspect > destAspect) {
      scaledWidth = destWidth;
      scaledHeight = (int)((float)destWidth / srcAspect);
   }
   else {
      scaledHeight = destHeight;
      scaledWidth = (int)((float)destHeight * srcAspect);
   }

   int offsetX = (destWidth - scaledWidth) / 2;
   int offsetY = (destHeight - scaledHeight) / 2;

   for (int y = 0; y < scaledHeight; ++y) {
      for (int x = 0; x < scaledWidth; ++x) {
         float srcX = ((float)x + 0.5f) * ((float)srcWidth  / (float)scaledWidth)  - 0.5f;
         float srcY = ((float)y + 0.5f) * ((float)srcHeight / (float)scaledHeight) - 0.5f;

         srcX = std::max(0.0f, std::min(srcX, static_cast<float>(srcWidth - 1)));
         srcY = std::max(0.0f, std::min(srcY, static_cast<float>(srcHeight - 1)));

         dest[(offsetY + y) * destWidth + (offsetX + x)] = InterpolatedRgb565Pixel(src, srcX, srcY, srcWidth, srcHeight);
      }
   }
}

float FrameUtil::CalcBrightness(float x)
{
   // function to improve the brightness with fx=ax²+bc+c, f(0)=0, f(1)=1, f'(1.1)=0
   return (-x * x + 2.1f * x) / 1.1f;
}

std::string FrameUtil::HexDump(const uint8_t* data, size_t size)
{
   constexpr int bytesPerLine = 32;

   std::stringstream ss;

   for (size_t i = 0; i < size; i += bytesPerLine) {
      for (size_t j = i; j < i + bytesPerLine && j < size; ++j)
         ss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(data[j]) << ' ';

      for (size_t j = i; j < i + bytesPerLine && j < size; ++j) {
         char ch = data[j];
         if (ch < 32 || ch > 126)
             ch = '.';
         ss << ch;
      }

      ss << std::endl;
   }

   return ss.str();
}

}