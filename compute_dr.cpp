// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Sami Boukortt
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "compute_dr.h"

#include <algorithm>
#include <cmath>
#include <vector>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "compute_dr.cpp"
#include <hwy/foreach_target.h>
#include <hwy/aligned_allocator.h>
#include <hwy/contrib/algo/transform-inl.h>
#include <hwy/highway.h>

namespace speedr {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

static int GetBlockSize(const SndfileHandle& input) {
	const int samplerate = input.samplerate();
	return std::lround(3.f * static_cast<float>(samplerate) * 44160.f / 44100);
}

HWY_ATTR float ComputeMonoDR(SndfileHandle& input) {
	HWY_FULL(float) d;
	using V = decltype(hn::Zero(d));
	const int block_size = GetBlockSize(input);
	const auto num_blocks = std::max<std::size_t>(1, (input.frames() + block_size - 1) / block_size);
	const auto num_top_blocks = std::max<std::size_t>(1, num_blocks / 5);
	hwy::AlignedFreeUniquePtr<float[]> block_samples = hwy::AllocateAligned<float>(block_size);
	std::vector<float> block_mean_square;
	std::vector<float> block_peak;
	block_mean_square.reserve(num_blocks);
	block_peak.reserve(num_blocks);

	for (std::size_t i_block = 0; i_block < num_blocks; ++i_block) {
		const std::size_t samples_read = input.readf(block_samples.get(), block_size);

		V sums_of_squares = hn::Zero(d);
		V peaks = hn::Zero(d);
		hn::Foreach(d, block_samples.get(), samples_read, hn::Zero(d), [&](auto d, const V samples) HWY_ATTR {
			sums_of_squares = hn::MulAdd(samples, samples, sums_of_squares);
			peaks = hn::Max(peaks, hn::Abs(samples));
		});
		const float sum_of_squares = hn::GetLane(hn::SumOfLanes(d, sums_of_squares));
		block_mean_square.push_back(sum_of_squares / samples_read);
		block_peak.push_back(hn::GetLane(hn::MaxOfLanes(d, peaks)));
	}

	std::nth_element(block_mean_square.begin(), block_mean_square.begin() + num_top_blocks - 1, block_mean_square.end(), std::greater());
	float average_mean_square = 0.f;
	for (std::size_t i = 0; i < num_top_blocks; ++i) {
		average_mean_square += block_mean_square[i];
	}
	// The doubling corresponds to AES17 calibration (+3dB)
	average_mean_square *= 2.f / num_top_blocks;

	std::nth_element(block_peak.begin(), block_peak.begin() + 1, block_peak.end(), std::greater());
	const float peak = block_peak[std::min<std::size_t>(1, block_peak.size() - 1)];

	return 10 * std::log10(peak * peak / average_mean_square);
}

HWY_ATTR std::pair<float, float> ComputeStereoDR(SndfileHandle& input) {
	HWY_FULL(float) d;
	using V = decltype(hn::Zero(d));
	const int block_size = GetBlockSize(input);
	const auto num_blocks = std::max<std::size_t>(1, (input.frames() + block_size - 1) / block_size);
	const auto num_top_blocks = std::max<std::size_t>(1, num_blocks / 5);
	hwy::AlignedFreeUniquePtr<float[]> block_samples = hwy::AllocateAligned<float>(2 * block_size);
	std::vector<float> left_block_mean_square;
	std::vector<float> left_block_peak;
	std::vector<float> right_block_mean_square;
	std::vector<float> right_block_peak;
	left_block_mean_square.reserve(num_blocks);
	left_block_peak.reserve(num_blocks);
	right_block_mean_square.reserve(num_blocks);
	right_block_peak.reserve(num_blocks);

	for (std::size_t i_block = 0; i_block < num_blocks; ++i_block) {
		const std::size_t frames_read = input.readf(block_samples.get(), block_size);

		V left_sums_of_squares = hn::Zero(d);
		V right_sums_of_squares = hn::Zero(d);
		V left_peaks = hn::Zero(d);
		V right_peaks = hn::Zero(d);
		std::size_t i;
		for (i = 0; i + hn::Lanes(d) <= frames_read; i += hn::Lanes(d)) {
			V left, right;
			hn::LoadInterleaved2(d, &block_samples[2 * i], left, right);
			left_sums_of_squares = hn::MulAdd(left, left, left_sums_of_squares);
			right_sums_of_squares = hn::MulAdd(right, right, right_sums_of_squares);
			left_peaks = hn::Max(left_peaks, hn::Abs(left));
			right_peaks = hn::Max(right_peaks, hn::Abs(right));
		}
		if (i != frames_read) {
			const std::size_t remaining = 2 * (frames_read - i);
			const V a = hn::LoadNOr(hn::Zero(d), d, &block_samples[2 * i], std::min<std::size_t>(hn::Lanes(d), remaining));
			const V b = remaining > hn::Lanes(d)
				? hn::LoadNOr(hn::Zero(d), d, &block_samples[2 * i + hn::Lanes(d)], remaining - hn::Lanes(d))
				: hn::Zero(d);
			const V left = hn::ConcatEven(d, b, a);
			const V right = hn::ConcatOdd(d, b, a);
			left_sums_of_squares = hn::MulAdd(left, left, left_sums_of_squares);
			right_sums_of_squares = hn::MulAdd(right, right, right_sums_of_squares);
			left_peaks = hn::Max(left_peaks, hn::Abs(left));
			right_peaks = hn::Max(right_peaks, hn::Abs(right));
		}
		const float left_sum_of_squares = hn::GetLane(hn::SumOfLanes(d, left_sums_of_squares));
		const float right_sum_of_squares = hn::GetLane(hn::SumOfLanes(d, right_sums_of_squares));
		left_block_mean_square.push_back(left_sum_of_squares / frames_read);
		right_block_mean_square.push_back(right_sum_of_squares / frames_read);
		left_block_peak.push_back(hn::GetLane(hn::MaxOfLanes(d, left_peaks)));
		right_block_peak.push_back(hn::GetLane(hn::MaxOfLanes(d, right_peaks)));
	}

	std::nth_element(left_block_mean_square.begin(), left_block_mean_square.begin() + num_top_blocks - 1, left_block_mean_square.end(), std::greater());
	std::nth_element(right_block_mean_square.begin(), right_block_mean_square.begin() + num_top_blocks - 1, right_block_mean_square.end(), std::greater());
	float left_average_mean_square = 0.f;
	float right_average_mean_square = 0.f;
	for (std::size_t i = 0; i < num_top_blocks; ++i) {
		left_average_mean_square += left_block_mean_square[i];
		right_average_mean_square += right_block_mean_square[i];
	}
	left_average_mean_square *= 2.f / num_top_blocks;
	right_average_mean_square *= 2.f / num_top_blocks;

	std::nth_element(left_block_peak.begin(), left_block_peak.begin() + 1, left_block_peak.end(), std::greater());
	std::nth_element(right_block_peak.begin(), right_block_peak.begin() + 1, right_block_peak.end(), std::greater());
	const float left_peak = left_block_peak[std::min<std::size_t>(1, left_block_peak.size() - 1)];
	const float right_peak = right_block_peak[std::min<std::size_t>(1, right_block_peak.size() - 1)];

	return {
		10 * std::log10(left_peak * left_peak / left_average_mean_square),
		10 * std::log10(right_peak * right_peak / right_average_mean_square),
	};
}

}

#if HWY_ONCE

HWY_EXPORT(ComputeMonoDR);
HWY_EXPORT(ComputeStereoDR);

float ComputeMonoDR(SndfileHandle& input) {
	return HWY_DYNAMIC_DISPATCH(ComputeMonoDR)(input);
}

std::pair<float, float> ComputeStereoDR(SndfileHandle& input) {
	return HWY_DYNAMIC_DISPATCH(ComputeStereoDR)(input);
}

#endif
}
