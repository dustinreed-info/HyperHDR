/* ImageColorAveraging.cpp
*
*  MIT License
*
*  Copyright (c) 2020-2026 awawa-dev
*
*  Project homesite: https://github.com/awawa-dev/HyperHDR
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*
*  The above copyright notice and this permission notice shall be included in all
*  copies or substantial portions of the Software.

*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*  SOFTWARE.
*/

#include <base/ImageColorAveraging.h>
#include <base/ImageToLedManager.h>
#include <infinite-color-engine/InfiniteProcessing.h>

#include <algorithm>
#include <array>
#include <ranges>
#include <iterator>

#define push_back_index(list, index) list.push_back((index) * 3)

using namespace hyperhdr;
using namespace linalg::aliases;

ImageColorAveraging::ImageColorAveraging(
				const LoggerName& _log,
				const int mappingType,
				const bool sparseProcessing,
				const unsigned width,
				const unsigned height,
				const unsigned horizontalBorder,
				const unsigned verticalBorder,
				const quint8 /*instanceIndex*/,
				const std::vector<LedString::Led>& leds,
				const bool subtitleFilter)
	: _width(width)
	, _height(height)
	, _sparseProcessing(sparseProcessing)
	, _horizontalBorder(horizontalBorder)
	, _verticalBorder(verticalBorder)
	, _subtitleFilter(subtitleFilter)
	, _colorsMap()
	, _colorGroups()
{
	Q_ASSERT(_width > 2 * _verticalBorder);
	Q_ASSERT(_height > 2 * _horizontalBorder);
	Q_ASSERT(_width < 10000);
	Q_ASSERT(_height < 10000);

	_mappingType = mappingType;

	_colorsMap.reserve(leds.size());
	_bottomEdge.reserve(leds.size());
	for (const LedString::Led& led : leds)
	{
		const double centerX = (led.minX_frac + led.maxX_frac) / 2;
		_bottomEdge.push_back(led.minY_frac >= 0.8);
		_bottomCenter.push_back(led.minY_frac >= 0.8 && centerX >= 0.15 && centerX <= 0.85);
	}
	_subtitleHold.assign(leds.size(), 0);
	_subtitleMode.assign(leds.size(), 0);

	const int32_t xOffset = _verticalBorder;
	const int32_t actualWidth = _width - 2 * _verticalBorder;
	const int32_t yOffset = _horizontalBorder;
	const int32_t actualHeight = _height - 2 * _horizontalBorder;

	size_t   totalCount = 0;
	size_t   totalCapasity = 0;

	for (int ledIndex = -1; const LedString::Led& led : leds)
	{
		ledIndex++;

		if ((led.maxX_frac - led.minX_frac) < 1e-6 || (led.maxY_frac - led.minY_frac) < 1e-6)
		{
			_colorsMap.emplace_back();
			continue;
		}

		int32_t minX_idx = xOffset + int32_t(qRound(actualWidth * led.minX_frac));
		int32_t maxX_idx = xOffset + int32_t(qRound(actualWidth * led.maxX_frac));
		int32_t minY_idx = yOffset + int32_t(qRound(actualHeight * led.minY_frac));
		int32_t maxY_idx = yOffset + int32_t(qRound(actualHeight * led.maxY_frac));

		minX_idx = qMin(minX_idx, xOffset + actualWidth - 1);
		if (minX_idx == maxX_idx)
		{
			maxX_idx++;
		}
		minY_idx = qMin(minY_idx, yOffset + actualHeight - 1);
		if (minY_idx == maxY_idx)
		{
			maxY_idx++;
		}

		const int32_t maxYLedCount = qMin(maxY_idx, yOffset + actualHeight);
		const int32_t maxXLedCount = qMin(maxX_idx, xOffset + actualWidth);
		const int32_t realYLedCount = qAbs(maxYLedCount - minY_idx);
		const int32_t realXLedCount = qAbs(maxXLedCount - minX_idx);

		bool   sparseIndexes = sparseProcessing;
		size_t totalSize = static_cast<size_t>(realYLedCount) * realXLedCount;

		if (!sparseIndexes && totalSize > 1600)
		{
			Warning(_log, "This is large image area for lamp: {:d}. It contains {:d} indexes for captured video frame so reduce it by four. Enabling 'sparse processing' option for you. Consider to enable it permanently in the processing configuration to hide that warning.", ledIndex, totalSize);
			sparseIndexes = true;
		}

		const int32_t increment = (sparseIndexes) ? 2 : 1;

		totalCount += _colorsMap.emplace_back([&]{
			std::vector<uint32_t> ledColor;
			if (!led.disabled)
			{
				ledColor.reserve((sparseIndexes) ? ((static_cast<unsigned long long>(realYLedCount / 2)) + (realYLedCount % 2)) * ((realXLedCount / 2) + (realXLedCount % 2)) : totalSize);

				for (int32_t y = minY_idx; y < maxYLedCount; y += increment)
				{
					for (int32_t x = minX_idx; x < maxXLedCount; x += increment)
					{
						push_back_index(ledColor, y * width + x);
					}
				}
			}
			return ledColor;
		}()).size();
		totalCapasity += _colorsMap.back().capacity();

		if (led.group > 0)
		{
			if (_colorGroups.contains(led.group))
			{
				int master = _colorGroups[led.group].front();
				auto& dest_vec = _colorsMap[master];
				auto& source_vec = _colorsMap.back();

				dest_vec.insert(
					dest_vec.end(),
					std::make_move_iterator(source_vec.begin()),
					std::make_move_iterator(source_vec.end())
				);

				source_vec.clear();
			}
			_colorGroups[led.group].push_back(ledIndex);
		}
	}
	Info(_log, "Total index number is: {:d} (memory: {:d}). User sparse processing is: {:s}, image size: {:d} x {:d}, area number: {:d}",
		totalCount, totalCapasity, (sparseProcessing) ? "enabled" : "disabled", width, height, leds.size());
}

unsigned ImageColorAveraging::width() const
{
	return _width;
}

unsigned ImageColorAveraging::height() const
{
	return _height;
}

unsigned ImageColorAveraging::horizontalBorder() const
{
	return _horizontalBorder;
}

unsigned ImageColorAveraging::verticalBorder() const {
	return _verticalBorder;
}

void ImageColorAveraging::process(std::vector<float3>& ledColors, const Image<ColorRgb>& image)
{
	ledColors.clear();
	ledColors.reserve(_colorsMap.size());

	switch (_mappingType)
	{
		case 1: getUnicolorForLeds(ledColors, image); break;
		default: getMulticolorForLeds(ledColors, image);
	}

	if (!_colorGroups.empty() && _mappingType != 1)
	{
		for (auto& group : _colorGroups)
		{
			const auto& combined = ledColors[group.second.front()];
			for (int g = 1; g < static_cast<int>(group.second.size()); g++)
			{
				ledColors[group.second[g]] = combined;
			}
		}
	}
}

void  ImageColorAveraging::getUnicolorForLeds(std::vector<float3>& ledColors, const Image<ColorRgb>& image) const
{
	ledColors.resize(_colorsMap.size(), calcUnicolorForLeds(image));
}


void ImageColorAveraging::getMulticolorForLeds(std::vector<float3>& ledColors, const Image<ColorRgb>& image) const
{
	for (size_t i = 0; i < _colorsMap.size(); ++i)
	{
		if (_subtitleFilter && _bottomEdge[i])
			ledColors.push_back(calcSubtitleFilteredColor(image, _colorsMap[i], _bottomCenter[i], _subtitleHold[i], _subtitleMode[i]));
		else
			ledColors.push_back(calcMulticolorForLeds(image, _colorsMap[i]));
	}
}

// Subtitle filter for bottom-edge LEDs, so caption text does not light the LEDs.
// Caption text is light and only weakly coloured; capture chroma subsampling tints it with the
// background colour, so "white" is judged by the channel ratio rather than exact neutrality.
// Dark scenes: an area made of dark pixels plus such light pixels (including dimmer ones from words
// fading in) is caption text over the scene; leave the text pixels out of the average.
// Bright scenes (middle of the bottom edge only, where captions are drawn): a minority of pixels
// much brighter than the area's median is caption text; leave it out. Large light areas are kept.
// Once engaged, the filter is held for a few frames so new words or scrolling lines cannot flash.
float3 ImageColorAveraging::calcSubtitleFilteredColor(const Image<ColorRgb>& image, const std::vector<uint32_t>& colors, bool center, uint8_t& hold, uint8_t& mode) const
{
	constexpr uint8_t HOLD_FRAMES = 12;
	constexpr uint8_t DARK_TEXT = 1, BRIGHT_TEXT = 2;
	if (colors.empty())
		return float3{ 0, 0, 0 };

	const uint8_t* imgData = image.rawMem();
	const auto luma = [](unsigned r, unsigned g, unsigned b) { return (54 * r + 183 * g + 19 * b) >> 8; };
	const auto weaklyColoured = [](unsigned r, unsigned g, unsigned b) {
		const unsigned hi = std::max({ r, g, b }), lo = std::min({ r, g, b });
		return lo * 10 >= hi * 6 || (r > 150 && g > 150 && b < r / 2); // light grey/white (possibly tinted) or yellow
	};

	std::array<uint16_t, 256> histogram{};
	for (const uint32_t offset : colors)
		histogram[luma(imgData[offset], imgData[offset + 1], imgData[offset + 2])]++;
	const size_t n = colors.size();
	unsigned median = 0;
	for (size_t seen = 0; median < 256; ++median)
		if ((seen += histogram[median]) * 2 >= n) break;
	const unsigned brightText = std::max(150u, median + 60);

	const auto isDarkText = [&](unsigned r, unsigned g, unsigned b) { return luma(r, g, b) >= 26 && weaklyColoured(r, g, b); };
	const auto isBrightText = [&](unsigned r, unsigned g, unsigned b) { return luma(r, g, b) >= brightText && weaklyColoured(r, g, b); };
	// Anti-aliased letter edges: slightly brighter than the area and next to a text pixel.
	const uint32_t rowBytes = image.width() * 3, imageBytes = rowBytes * image.height();
	const auto isTextAt = [&](uint32_t offset) { return isBrightText(imgData[offset], imgData[offset + 1], imgData[offset + 2]); };
	const auto isTextEdge = [&](uint32_t offset, unsigned r, unsigned g, unsigned b) {
		if (luma(r, g, b) < median + 15 || !weaklyColoured(r, g, b))
			return false;
		const uint32_t x = (offset % rowBytes) / 3;
		return (x > 0 && isTextAt(offset - 3)) || (x + 1 < image.width() && isTextAt(offset + 3)) ||
			(offset >= rowBytes && isTextAt(offset - rowBytes)) || (offset + rowBytes < imageBytes && isTextAt(offset + rowBytes));
	};

	size_t dark = 0, darkText = 0, brightCount = 0;
	for (const uint32_t offset : colors)
	{
		const unsigned r = imgData[offset], g = imgData[offset + 1], b = imgData[offset + 2];
		if (luma(r, g, b) < 26) dark++;
		else if (isDarkText(r, g, b)) darkText++;
		if (isBrightText(r, g, b)) brightCount++;
	}

	const bool mostlyDark = dark * 10 >= n * 3;
	const bool contrast = center && median <= 150;
	if (darkText > 0 && mostlyDark && (dark + darkText) * 10 >= n * 9)
		{ hold = HOLD_FRAMES; mode = DARK_TEXT; }
	else if (contrast && brightCount > 0 && brightCount * 10 <= n * 4)
		{ hold = HOLD_FRAMES; mode = BRIGHT_TEXT; }
	else if (hold > 0 && (mode == DARK_TEXT ? mostlyDark : contrast))
		hold--;
	else
		hold = 0;
	if (hold == 0)
		return calcMulticolorForLeds(image, colors);

	linalg::vec<uint_fast64_t, 3> sumLinear(0, 0, 0);
	size_t kept = 0;
	for (const uint32_t offset : colors)
	{
		const unsigned r = imgData[offset], g = imgData[offset + 1], b = imgData[offset + 2];
		if (mode == DARK_TEXT ? isDarkText(r, g, b) : (isBrightText(r, g, b) || isTextEdge(offset, r, g, b)))
			continue;
		sumLinear += InfiniteProcessing::srgbNonlinearToLinear(byte3(r, g, b));
		kept++;
	}
	if (kept == 0)
		return calcMulticolorForLeds(image, colors);
	return (static_cast<float3>(sumLinear) / static_cast<float>(kept)) / 65535.0f;
}

float3 ImageColorAveraging::calcMulticolorForLeds(const Image<ColorRgb>& image, const std::vector<uint32_t>& colors) const
{
	if (colors.empty())
	{
		return float3{ 0, 0, 0 };
	}

	linalg::vec<uint_fast64_t, 3> sumLinear(0, 0, 0);

	const uint8_t* imgData = image.rawMem();

	for (const uint32_t colorOffset : colors)
	{		
		sumLinear += InfiniteProcessing::srgbNonlinearToLinear(byte3(imgData[colorOffset], imgData[colorOffset + 1], imgData[colorOffset + 2]));
	}

	auto averageLinear = (static_cast<float3>(sumLinear) / static_cast<float>(colors.size())) / 65535.0f;

	return averageLinear;
}

float3 ImageColorAveraging::calcUnicolorForLeds(const Image<ColorRgb>& image) const
{
	uint_fast64_t sum = 0;
	linalg::vec<uint_fast64_t, 3> sumLinear(0, 0, 0);

	const uint8_t* imgData = image.rawMem();
	const uint32_t rowSize = image.width() * static_cast<uint32_t>(sizeof(ColorRgb));
	const uint32_t increment = (_sparseProcessing) ? 2 : 1;

	for (uint32_t y = 0; y < image.height(); y += increment)
	{
		for (uint32_t colorOffset = y * rowSize; colorOffset < y * rowSize + rowSize; colorOffset += increment * static_cast<uint32_t>(sizeof(ColorRgb)))
		{
			sumLinear += InfiniteProcessing::srgbNonlinearToLinear(byte3(imgData[colorOffset], imgData[colorOffset + 1], imgData[colorOffset + 2]));
			sum++;
		}
	}

	auto averageLinear = (static_cast<float3>(sumLinear) / static_cast<float>(sum)) / 65535.0f;

	return averageLinear;
}

