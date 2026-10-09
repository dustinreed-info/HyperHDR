#pragma once

#ifndef PCH_ENABLED
	#include <cassert>
	#include <sstream>
	#include <math.h>
	#include <algorithm>
	#include <vector>
	#include <map>
#endif

#include <image/Image.h>
#include <utils/Logger.h>
#include <base/LedString.h>

#include <linalg.h>

namespace hyperhdr
{	
	class ImageColorAveraging
	{
	public:
		ImageColorAveraging(
			const LoggerName& _log,
			const int mappingType,
			const bool sparseProcessing,
			const unsigned width,
			const unsigned height,
			const unsigned horizontalBorder,
			const unsigned verticalBorder,
			const quint8 instanceIndex,
			const std::vector<LedString::Led>& leds,
			const bool subtitleFilter = false);

		unsigned width() const;
		unsigned height() const;

		unsigned horizontalBorder() const;
		unsigned verticalBorder() const;

		void process(std::vector<linalg::aliases::float3>& ledColors, const Image<ColorRgb>& image);

	private:
		void getUnicolorForLeds(std::vector<linalg::aliases::float3>& ledColors, const Image<ColorRgb>& image) const;
		void getMulticolorForLeds(std::vector<linalg::aliases::float3>& ledColors, const Image<ColorRgb>& image) const;

		const unsigned _width;
		const unsigned _height;
		const bool	_sparseProcessing;
		const unsigned _horizontalBorder;
		const unsigned _verticalBorder;
		int _mappingType;
		const bool _subtitleFilter;

		std::vector<std::vector<uint32_t>> _colorsMap;
		std::vector<bool> _bottomEdge; // LED samples the bottom edge, where subtitles are drawn
		mutable std::vector<uint8_t> _subtitleHold; // frames the subtitle filter stays engaged per LED
		std::map<int, std::vector<uint32_t>> _colorGroups;

		linalg::aliases::float3 calcMulticolorForLeds(const Image<ColorRgb>& image, const std::vector<uint32_t>& colors) const;
		linalg::aliases::float3 calcSubtitleFilteredColor(const Image<ColorRgb>& image, const std::vector<uint32_t>& colors, uint8_t& hold) const;
		linalg::aliases::float3 calcUnicolorForLeds(const Image<ColorRgb>& image) const;
	};
}
